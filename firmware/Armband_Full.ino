/*
 * Armband PPG + 940nm Full Firmware
 * Target: Seeed XIAO ESP32C3
 *
 * Features:
 *  - MAX30102 Heart Rate + SpO2 + Temp
 *  - LIS3DH motion detection with tunable threshold + filtering
 *  - 940nm reflectance channel with multi-sample averaging + EMA filter
 *  - Battery voltage monitoring (ADC + divider)
 *  - Full WiFi + MQTT (user/pass support)
 *  - Deep sleep with timer + GPIO wake
 *  - RTC-persistent motion EMA + isMoving state (survives deep sleep)
 *  - Wake-skip counter for quiet periods (saves WiFi power)
 *  - Connection-time measurement + motion state-transition logging
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

// =============================================================================
// USER CONFIG – edit these
// =============================================================================

// --- WiFi ---
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// --- MQTT ---
const char* MQTT_SERVER   = "192.168.1.100";   // Raspberry Pi IP
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "armband";         // leave "" if no auth
const char* MQTT_PASSWORD = "your_mqtt_pass";  // leave "" if no auth
const char* MQTT_CLIENT_ID = "armband_ppg";
const char* MQTT_TOPIC    = "armband/ppg";

// --- Pins (XIAO ESP32C3) ---
#define PIN_940NM_EMITTER  D6          // TSAL6200 drive pin
#define PIN_940NM_ADC      A0          // BPW34 via resistor to ADC
#define PIN_BATTERY_ADC    A1          // Voltage divider from LiPo (hypothetical)
#define PIN_LIS3DH_INT     D2          // LIS3DH INT1 for wake (optional)

// --- Motion threshold (tune these) ---
const float MOTION_THRESHOLD     = 11.5;   // magnitude above ~g to count as moving
const float MOTION_HYSTERESIS    = 0.8;    // to prevent chatter
const float MOTION_EMA_ALPHA     = 0.25;   // filtering on magnitude (0.1–0.4)

// --- 940nm filtering ---
const int   RAW940_SAMPLES       = 8;     // multi-sample average
const float RAW940_EMA_ALPHA     = 0.2;   // exponential filter

// --- Battery ---
const float BATTERY_SCALE        = 2.0;
const float BATTERY_OFFSET       = 0.0;
const int   BATTERY_SAMPLES      = 12;

// --- Deep sleep / power ---
const uint64_t PERIODIC_WAKE_US  = 180ULL * 1000000ULL;  // 3 minutes timer wake
const unsigned long AWAKE_WINDOW_MS = 12000;             // stay awake after motion
const unsigned long QUIET_AWAKE_MS  = 5500;              // shorter awake when quiet
const unsigned long SETTLE_MS       = 120;

// How many quiet (no-motion) wakes to skip before forcing a network connect
// 0 = connect every wake. 2 = connect on every 3rd quiet wake, etc.
const uint8_t QUIET_WAKE_SKIP = 2;

// =============================================================================
// END USER CONFIG
// =============================================================================

// Objects
MAX30105 particleSensor;
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
Adafruit_SSD1306 display(128, 64, &Wire, -1);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// HR / SpO2
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
bool fingerDetected = false;

// ---------- RTC-persistent state (survives deep sleep) ----------
RTC_DATA_ATTR float    rtcFilteredMotion = 0;
RTC_DATA_ATTR bool     rtcIsMoving       = false;
RTC_DATA_ATTR uint32_t rtcBootCount      = 0;
RTC_DATA_ATTR uint8_t  rtcQuietSkipCount = 0;   // how many quiet wakes we have skipped

// Working copies (loaded from RTC at boot)
float accelX = 0, accelY = 0, accelZ = 0;
float motionMagnitude = 0;
float filteredMotion = 0;
bool  isMoving = false;
bool  prevIsMoving = false;          // for transition detection
bool  motionTransition = false;      // true if state changed this wake
const char* transitionStr = "none";  // "still_to_moving" / "moving_to_still" / "none"

// 940nm
int raw940 = 0;
float filtered940 = 0;

// Battery
float batteryVoltage = 0;

// Timing & power
unsigned long lastTempRead = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long wakeStart = 0;
bool motionEventThisWake = false;
bool doNetworkThisWake = true;       // decided in setup()
unsigned long connectTimeMs = 0;     // WiFi + MQTT connect duration

// ---------------------------------------------------------------------------
// Battery voltage (ADC + multi-sample)
// ---------------------------------------------------------------------------
float readBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    sum += analogRead(PIN_BATTERY_ADC);
    delayMicroseconds(40);
  }
  float raw = sum / (float)BATTERY_SAMPLES;
  float volts = (raw / 4095.0f) * 3.3f * BATTERY_SCALE + BATTERY_OFFSET;
  return volts;
}

// ---------------------------------------------------------------------------
// 940 nm with multi-sample + EMA
// ---------------------------------------------------------------------------
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

  if (filtered940 < 1.0f) {
    filtered940 = avg;
  } else {
    filtered940 = RAW940_EMA_ALPHA * avg + (1.0f - RAW940_EMA_ALPHA) * filtered940;
  }
  raw940 = (int)avg;
  return filtered940;
}

// ---------------------------------------------------------------------------
// Motion with EMA + hysteresis + transition logging
// Uses RTC-persisted filteredMotion / isMoving
// ---------------------------------------------------------------------------
void updateMotion() {
  sensors_event_t event;
  lis.getEvent(&event);
  accelX = event.acceleration.x;
  accelY = event.acceleration.y;
  accelZ = event.acceleration.z;

  float mag = sqrtf(accelX*accelX + accelY*accelY + accelZ*accelZ);
  motionMagnitude = mag;

  // Continue EMA from the value that survived deep sleep
  if (filteredMotion < 0.05f) {
    filteredMotion = mag;          // first-ever sample
  } else {
    filteredMotion = MOTION_EMA_ALPHA * mag + (1.0f - MOTION_EMA_ALPHA) * filteredMotion;
  }

  prevIsMoving = isMoving;

  // Hysteresis
  if (isMoving) {
    if (filteredMotion < (MOTION_THRESHOLD - MOTION_HYSTERESIS)) {
      isMoving = false;
    }
  } else {
    if (filteredMotion > MOTION_THRESHOLD) {
      isMoving = true;
      motionEventThisWake = true;
    }
  }

  // Detect and log transitions
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

  // Keep RTC copies up to date so they survive the next sleep
  rtcFilteredMotion = filteredMotion;
  rtcIsMoving       = isMoving;
}

// ---------------------------------------------------------------------------
// WiFi + MQTT with timing
// ---------------------------------------------------------------------------
void setupWiFi() {
  Serial.print("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED");
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
  } else {
    ok = mqtt.connect(MQTT_CLIENT_ID);
  }
  if (ok) {
    Serial.println("MQTT connected");
  } else {
    Serial.print("MQTT fail rc=");
    Serial.println(mqtt.state());
  }
}

void publishData() {
  if (!mqtt.connected()) return;

  char payload[280];
  snprintf(payload, sizeof(payload),
    "{\"bpm\":%d,\"spo2\":%d,\"temp\":%.1f,\"motion\":%.2f,\"moving\":%s,"
    "\"raw940\":%d,\"filt940\":%.1f,\"batt\":%.2f,"
    "\"trans\":\"%s\",\"conn_ms\":%lu,\"boot\":%u}",
    beatAvg,
    validSPO2 ? spo2 : -1,
    temperature,
    filteredMotion,
    isMoving ? "true" : "false",
    raw940,
    filtered940,
    batteryVoltage,
    transitionStr,
    connectTimeMs,
    (unsigned)rtcBootCount
  );

  mqtt.publish(MQTT_TOPIC, payload);
  Serial.println(payload);
}

// ---------------------------------------------------------------------------
// OLED (brief use only)
// ---------------------------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!fingerDetected) {
    display.setTextSize(2);
    display.setCursor(18, 20);
    display.println("No Finger");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("BPM ");
    display.setTextSize(2);
    display.print(beatAvg);

    display.setTextSize(1);
    display.setCursor(70, 0);
    display.print("SpO2");
    display.setTextSize(2);
    display.setCursor(70, 12);
    if (validSPO2) display.print(spo2);
    else display.print("--");

    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Mot:");
    display.print(filteredMotion, 1);
    display.print(isMoving ? " MOV" : " still");

    display.setCursor(0, 52);
    display.print("940:");
    display.print((int)filtered940);
    display.print(" V:");
    display.print(batteryVoltage, 2);
  }
  display.display();
}

// ---------------------------------------------------------------------------
// Power-down helpers before deep sleep
// ---------------------------------------------------------------------------
void prepareForSleep() {
  digitalWrite(PIN_940NM_EMITTER, LOW);

  particleSensor.setPulseAmplitudeRed(0);
  particleSensor.setPulseAmplitudeIR(0);
  particleSensor.setPulseAmplitudeGreen(0);

  display.ssd1306_command(SSD1306_DISPLAYOFF);

  if (mqtt.connected()) {
    mqtt.disconnect();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(40);
}

void goToDeepSleep() {
  // Make sure latest motion state is stored in RTC
  rtcFilteredMotion = filteredMotion;
  rtcIsMoving       = isMoving;

  prepareForSleep();

  esp_sleep_enable_timer_wakeup(PERIODIC_WAKE_US);

  // Optional GPIO wake from LIS3DH INT pin (active high)
  // Uncomment and wire INT1 if you want pure motion wake
  // gpio_wakeup_enable((gpio_num_t)PIN_LIS3DH_INT, GPIO_INTR_HIGH_LEVEL);
  // esp_sleep_enable_gpio_wakeup();

  Serial.printf("Deep sleep... (boot #%u, quietSkip=%u)\n",
                (unsigned)rtcBootCount, (unsigned)rtcQuietSkipCount);
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
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

  // Restore motion state that survived deep sleep
  filteredMotion = rtcFilteredMotion;
  isMoving       = rtcIsMoving;
  prevIsMoving   = isMoving;

  // 940 nm emitter
  pinMode(PIN_940NM_EMITTER, OUTPUT);
  digitalWrite(PIN_940NM_EMITTER, LOW);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Armband waking...");
    display.display();
  }

  // MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
  } else {
    particleSensor.setup(60, 4, 2, 100, 411, 4096);
    particleSensor.setPulseAmplitudeRed(0x1F);
    particleSensor.setPulseAmplitudeIR(0x1F);
    particleSensor.setPulseAmplitudeGreen(0);
    Serial.println("MAX30102 OK");
  }

  // LIS3DH
  if (!lis.begin(0x18)) {
    if (!lis.begin(0x19)) {
      Serial.println("LIS3DH not found");
    }
  } else {
    lis.setRange(LIS3DH_RANGE_4_G);
    Serial.println("LIS3DH OK");
  }

  // First sensor reads (motion uses restored RTC state)
  batteryVoltage = readBatteryVoltage();
  updateMotion();
  read940Filtered();

  // Decide whether this wake should use the network
  // Always connect on motion events or when skip counter expires
  if (motionEventThisWake || isMoving) {
    doNetworkThisWake = true;
    rtcQuietSkipCount = 0;          // reset skip counter on activity
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

  // ---------- PPG buffer collection ----------
  for (int i = 0; i < BUFFER_SIZE; i++) {
    irBuffer[i]  = particleSensor.getIR();
    redBuffer[i] = particleSensor.getRed();
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE, redBuffer,
    &spo2, &validSPO2, &heartRate, &validHeartRate
  );

  long irValue = particleSensor.getIR();
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
  if (validHeartRate && heartRate > 40 && heartRate < 180) {
    beatAvg = heartRate;
  }

  // ---------- Sensors ----------
  updateMotion();
  read940Filtered();

  if (millis() - lastTempRead > 4000) {
    temperature = particleSensor.readTemperature();
    lastTempRead = millis();
  }

  batteryVoltage = readBatteryVoltage();

  // ---------- Display ----------
  if (millis() - lastDisplayUpdate > 250) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  // ---------- MQTT (only if we decided to network this wake) ----------
  if (doNetworkThisWake && (millis() - lastMqttPublish > 1500)) {
    publishData();
    lastMqttPublish = millis();
  }

  // ---------- Decision to sleep ----------
  unsigned long awake = millis() - wakeStart;
  unsigned long targetAwake = motionEventThisWake ? AWAKE_WINDOW_MS : QUIET_AWAKE_MS;

  if (awake > targetAwake) {
    goToDeepSleep();
  }

  delay(20);
}
