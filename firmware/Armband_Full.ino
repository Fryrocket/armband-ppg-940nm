// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Fryrocket

/*
 * Armband PPG + 940nm Full Firmware
 * Target: Seeed XIAO ESP32C3
 *
 * Features:
 *  - MAX30102 Heart Rate + SpO2 + Temp
 *  - LIS3DH motion detection with tunable threshold + filtering
 *  - Hardware INT1 motion wake (active-low, latched, high-pass)
 *  - 940nm reflectance channel with multi-sample averaging + EMA filter
 *  - Battery voltage monitoring (ADC + divider)
 *  - Full WiFi + MQTT (user/pass support)
 *  - Deep sleep with 3-minute timer + GPIO INT1 wake
 *  - RTC-persistent motion EMA + isMoving state (survives deep sleep)
 *  - Wake-skip counter for quiet periods (saves WiFi power)
 *  - Connection-time measurement + motion state-transition logging
 *  - maxOk/lisOk sensor gates; gpio hold on 940 nm emitter; suppress phantom GPIO-wake transition
 *  - RTC EMA seed flags (no magic thresholds); static_assert on wake GPIO range
 *
 * Edit the USER CONFIG section for your network, pins, and thresholds.
 */

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_Sensor.h>
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_SERVER   = "192.168.1.100";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "armband";
const char* MQTT_PASSWORD = "your_mqtt_pass";
const char* MQTT_CLIENT_ID = "armband_ppg";
const char* MQTT_TOPIC    = "armband/ppg";

#define PIN_940NM_EMITTER  D6
#define PIN_940NM_ADC      A0
#define PIN_BATTERY_ADC    A1
#define PIN_LIS3DH_INT     D2

const float MOTION_THRESHOLD     = 11.5;
const float MOTION_HYSTERESIS    = 0.8;
const float MOTION_EMA_ALPHA     = 0.25;

const uint8_t INT1_THRESHOLD_LSB = 10;
const uint8_t INT1_DURATION_LSB  = 5;

const int   RAW940_SAMPLES       = 8;
const float RAW940_EMA_ALPHA     = 0.2;

const float BATTERY_SCALE        = 2.0;
const float BATTERY_OFFSET       = 0.0;
const int   BATTERY_SAMPLES      = 12;

const uint64_t PERIODIC_WAKE_US  = 180ULL * 1000000ULL;
const unsigned long AWAKE_WINDOW_MS = 12000;
const unsigned long QUIET_AWAKE_MS  = 5500;
const unsigned long SETTLE_MS       = 120;

const uint8_t QUIET_WAKE_SKIP = 2;

MAX30105 particleSensor;
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
Adafruit_SSD1306 display(128, 64, &Wire, -1);
uint8_t lis3dhAddr = 0x18;

WiFiClient espClient;
PubSubClient mqtt(espClient);

const byte RATE_SIZE = 8;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int beatAvg = 0;

#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

float temperature = 0;
bool  tempValid   = false;
bool fingerDetected = false;
bool maxOk = false;
bool lisOk = false;
bool suppressTransition = false;

RTC_DATA_ATTR float    rtcFilteredMotion = 0;
RTC_DATA_ATTR bool     rtcIsMoving       = false;
RTC_DATA_ATTR uint32_t rtcBootCount      = 0;
RTC_DATA_ATTR uint8_t  rtcQuietSkipCount = 0;
RTC_DATA_ATTR bool     rtcHave940        = false;
RTC_DATA_ATTR bool     rtcHaveMotion     = false;

float accelX = 0, accelY = 0, accelZ = 0;
float motionMagnitude = 0;
float filteredMotion = 0;
bool  isMoving = false;
bool  prevIsMoving = false;
bool  motionTransition = false;
const char* transitionStr = "none";

int raw940 = 0;
float filtered940 = 0;
float batteryVoltage = 0;

unsigned long lastTempRead = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long wakeStart = 0;
bool motionEventThisWake = false;
bool doNetworkThisWake = true;
unsigned long connectTimeMs = 0;
bool wokeFromMotion = false;

void clearLIS3DH_INT1() {
  Wire.beginTransmission(lis3dhAddr);
  Wire.write(0x31);
  Wire.endTransmission(false);
  Wire.requestFrom(lis3dhAddr, (uint8_t)1);
  if (Wire.available()) {
    uint8_t status = Wire.read();
    Serial.printf("[LIS3DH] INT1 cleared (status=0x%02X)\n", status);
  }
}

void setupLIS3DH_INT1() {
  lis.setRange(LIS3DH_RANGE_2_G);
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x20); Wire.write(0x57); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x21); Wire.write(0x09); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x30); Wire.write(0x2A); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x32); Wire.write(INT1_THRESHOLD_LSB); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x33); Wire.write(INT1_DURATION_LSB); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x24); Wire.write(0x08); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x25); Wire.write(0x02); Wire.endTransmission();
  Wire.beginTransmission(lis3dhAddr); Wire.write(0x22); Wire.write(0x40); Wire.endTransmission();
  pinMode(PIN_LIS3DH_INT, INPUT);
  Serial.println("[LIS3DH] INT1 configured: active-low, latched, HP on, 160 mg");
}

float readBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    sum += analogRead(PIN_BATTERY_ADC);
    delayMicroseconds(40);
  }
  float raw = sum / (float)BATTERY_SAMPLES;
  return (raw / 4095.0f) * 3.3f * BATTERY_SCALE + BATTERY_OFFSET;
}

float read940Filtered() {
  long sum = 0;
  digitalWrite(PIN_940NM_EMITTER, HIGH);
  delayMicroseconds(30);
  for (int i = 0; i < RAW940_SAMPLES; i++) {
    sum += analogRead(PIN_940NM_ADC);
    delayMicroseconds(20);
  }
  digitalWrite(PIN_940NM_EMITTER, LOW);
  float avg = sum / (float)RAW940_SAMPLES;
  if (!rtcHave940) {
    filtered940 = avg;
    rtcHave940 = true;
  } else {
    filtered940 = RAW940_EMA_ALPHA * avg + (1.0f - RAW940_EMA_ALPHA) * filtered940;
  }
  raw940 = (int)avg;
  return filtered940;
}

void updateMotion() {
  sensors_event_t event;
  lis.getEvent(&event);
  accelX = event.acceleration.x;
  accelY = event.acceleration.y;
  accelZ = event.acceleration.z;
  float mag = sqrtf(accelX*accelX + accelY*accelY + accelZ*accelZ);
  motionMagnitude = mag;
  if (!rtcHaveMotion) {
    filteredMotion = mag;
    rtcHaveMotion = true;
  } else {
    filteredMotion = MOTION_EMA_ALPHA * mag + (1.0f - MOTION_EMA_ALPHA) * filteredMotion;
  }
  prevIsMoving = isMoving;
  if (isMoving) {
    if (filteredMotion < (MOTION_THRESHOLD - MOTION_HYSTERESIS)) isMoving = false;
  } else {
    if (filteredMotion > MOTION_THRESHOLD) {
      isMoving = true;
      motionEventThisWake = true;
    }
  }
  if (suppressTransition) {
    motionTransition = false;
    transitionStr = "none";
    suppressTransition = false;
  } else {
    motionTransition = (isMoving != prevIsMoving);
    if (motionTransition) {
      if (isMoving) {
        transitionStr = "still_to_moving";
        Serial.println("[MOTION] still → MOVING");
      } else {
        transitionStr = "moving_to_still";
        Serial.println("[MOTION] MOVING → still");
      }
    } else {
      transitionStr = "none";
    }
  }
  rtcFilteredMotion = filteredMotion;
  rtcIsMoving       = isMoving;
}

void setupWiFi() {
  Serial.print("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK"); Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED");
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  bool ok;
  if (strlen(MQTT_USER) > 0) ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
  else ok = mqtt.connect(MQTT_CLIENT_ID);
  if (ok) Serial.println("MQTT connected");
  else { Serial.print("MQTT fail rc="); Serial.println(mqtt.state()); }
}

void publishData() {
  if (!mqtt.connected()) return;
  char payload[280];
  snprintf(payload, sizeof(payload),
    "{\"bpm\":%d,\"spo2\":%d,\"temp\":%.1f,\"motion\":%.2f,\"moving\":%s,"
    "\"raw940\":%d,\"filt940\":%.1f,\"batt\":%.2f,"
    "\"trans\":\"%s\",\"conn_ms\":%lu,\"boot\":%u}",
    fingerDetected ? beatAvg : -1,
    validSPO2 ? spo2 : -1,
    tempValid ? temperature : -1.0f,
    filteredMotion,
    isMoving ? "true" : "false",
    raw940, filtered940, batteryVoltage,
    transitionStr, connectTimeMs, (unsigned)rtcBootCount
  );
  mqtt.publish(MQTT_TOPIC, payload);
  Serial.println(payload);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (!fingerDetected) {
    display.setTextSize(2); display.setCursor(18, 20); display.println("No Finger");
  } else {
    display.setTextSize(1); display.setCursor(0, 0); display.print("BPM ");
    display.setTextSize(2); display.print(beatAvg);
    display.setTextSize(1); display.setCursor(70, 0); display.print("SpO2");
    display.setTextSize(2); display.setCursor(70, 12);
    if (validSPO2) display.print(spo2); else display.print("--");
    display.setTextSize(1); display.setCursor(0, 40);
    display.print("Mot:"); display.print(filteredMotion, 1);
    display.print(isMoving ? " MOV" : " still");
    display.setCursor(0, 52);
    display.print("940:"); display.print((int)filtered940);
    display.print(" V:"); display.print(batteryVoltage, 2);
  }
  display.display();
}

void prepareForSleep() {
  digitalWrite(PIN_940NM_EMITTER, LOW);
  gpio_hold_en((gpio_num_t)PIN_940NM_EMITTER);
  gpio_deep_sleep_hold_en();
  if (maxOk) {
    particleSensor.setPulseAmplitudeRed(0);
    particleSensor.setPulseAmplitudeIR(0);
    particleSensor.setPulseAmplitudeGreen(0);
  }
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (mqtt.connected()) mqtt.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(40);
}

void goToDeepSleep() {
  rtcFilteredMotion = filteredMotion;
  rtcIsMoving       = isMoving;
  clearLIS3DH_INT1();
  prepareForSleep();
  esp_sleep_enable_timer_wakeup(PERIODIC_WAKE_US);
  static_assert(PIN_LIS3DH_INT <= 5, "ESP32-C3 deep-sleep wake requires GPIO0-5");
  esp_deep_sleep_enable_gpio_wakeup(BIT(PIN_LIS3DH_INT), ESP_GPIO_WAKEUP_GPIO_LOW);
  Serial.printf("Deep sleep... (boot #%u, quietSkip=%u)\n",
                (unsigned)rtcBootCount, (unsigned)rtcQuietSkipCount);
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(SETTLE_MS);
  rtcBootCount++;
  Serial.printf("\n=== Armband wake  boot #%u  reset=%d ===\n",
                (unsigned)rtcBootCount, (int)esp_reset_reason());
  wakeStart = millis();
  motionEventThisWake = false;
  motionTransition = false;
  transitionStr = "none";
  connectTimeMs = 0;
  wokeFromMotion = false;
  suppressTransition = false;
  filteredMotion = rtcFilteredMotion;
  isMoving       = rtcIsMoving;
  prevIsMoving   = isMoving;

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    Serial.println("[WAKE] Motion detected on INT1 (active-low)");
    wokeFromMotion = true;
    motionEventThisWake = true;
    isMoving = true;
    prevIsMoving = true;
    suppressTransition = true;
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("[WAKE] 3-minute timer");
  } else {
    Serial.println("[WAKE] Power-on / reset");
  }

  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_940NM_EMITTER);
  pinMode(PIN_940NM_EMITTER, OUTPUT);
  digitalWrite(PIN_940NM_EMITTER, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED not found");
  else {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.println("Armband waking..."); display.display();
  }

  maxOk = false;
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found — PPG path disabled this wake");
  } else {
    particleSensor.setup(60, 4, 2, 100, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeIR(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
    maxOk = true;
    Serial.println("MAX30102 OK");
  }

  lisOk = false;
  if (lis.begin(0x18)) {
    lis3dhAddr = 0x18; lisOk = true;
    Serial.println("LIS3DH OK (0x18)"); setupLIS3DH_INT1();
  } else if (lis.begin(0x19)) {
    lis3dhAddr = 0x19; lisOk = true;
    Serial.println("LIS3DH OK (0x19)"); setupLIS3DH_INT1();
  } else {
    Serial.println("LIS3DH not found — motion/wake degraded this wake");
  }

  clearLIS3DH_INT1();
  batteryVoltage = readBatteryVoltage();
  if (lisOk) updateMotion();
  read940Filtered();

  if (motionEventThisWake || isMoving || wokeFromMotion) {
    doNetworkThisWake = true;
    rtcQuietSkipCount = 0;
  } else {
    if (rtcQuietSkipCount >= QUIET_WAKE_SKIP) {
      doNetworkThisWake = true;
      rtcQuietSkipCount = 0;
    } else {
      doNetworkThisWake = false;
      rtcQuietSkipCount++;
      Serial.printf("Quiet wake – skipping network (%u/%u)\n",
                    (unsigned)rtcQuietSkipCount, (unsigned)QUIET_WAKE_SKIP);
    }
  }

  if (doNetworkThisWake) {
    unsigned long t0 = millis();
    setupWiFi();
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    reconnectMQTT();
    connectTimeMs = millis() - t0;
    Serial.printf("Connect time: %lu ms\n", connectTimeMs);
    publishData();
  } else {
    Serial.println("No network this wake");
  }
}

void loop() {
  if (doNetworkThisWake) {
    if (!mqtt.connected()) reconnectMQTT();
    mqtt.loop();
  }

  if (maxOk) {
    int samplesCollected = 0;
    unsigned long fillStart = millis();
    while (samplesCollected < BUFFER_SIZE && (millis() - fillStart < 2500)) {
      particleSensor.check();
      while (particleSensor.available() && samplesCollected < BUFFER_SIZE) {
        irBuffer[samplesCollected]  = particleSensor.getFIFOIR();
        redBuffer[samplesCollected] = particleSensor.getFIFORed();
        particleSensor.nextSample();
        samplesCollected++;
      }
      if (samplesCollected < BUFFER_SIZE) delay(4);
    }
    if (samplesCollected < BUFFER_SIZE) {
      Serial.printf("[PPG] collected only %d / %d samples\n", samplesCollected, BUFFER_SIZE);
    }
    maxim_heart_rate_and_oxygen_saturation(
      irBuffer, BUFFER_SIZE, redBuffer,
      &spo2, &validSPO2, &heartRate, &validHeartRate
    );
    long irValue = (samplesCollected > 0) ? (long)irBuffer[samplesCollected - 1] : particleSensor.getIR();
    fingerDetected = (irValue >= 50000);
    if (fingerDetected && checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      float bpm = 60.0f / (delta / 1000.0f);
      if (bpm > 40 && bpm < 180) {
        rates[rateSpot++] = (byte)bpm;
        rateSpot %= RATE_SIZE;
        beatAvg = 0;
        for (byte i = 0; i < RATE_SIZE; i++) beatAvg += rates[i];
        beatAvg /= RATE_SIZE;
      }
    }
    if (validHeartRate && heartRate > 40 && heartRate < 180) beatAvg = heartRate;
    if (millis() - lastTempRead > 4000) {
      temperature = particleSensor.readTemperature();
      tempValid = true;
      lastTempRead = millis();
    }
  } else {
    fingerDetected = false;
    validSPO2 = 0;
    validHeartRate = 0;
  }

  if (lisOk) updateMotion();
  read940Filtered();
  batteryVoltage = readBatteryVoltage();

  if (millis() - lastDisplayUpdate > 250) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  if (doNetworkThisWake && (millis() - lastMqttPublish > 1500)) {
    publishData();
    lastMqttPublish = millis();
  }

  unsigned long awake = millis() - wakeStart;
  unsigned long targetAwake = (motionEventThisWake || wokeFromMotion) ? AWAKE_WINDOW_MS : QUIET_AWAKE_MS;
  if (!maxOk) targetAwake = min(targetAwake, 3000UL);

  if (awake > targetAwake) goToDeepSleep();
  delay(20);
}
