/*
 * Armband PPG + 940nm Full Firmware
 * Target: Seeed XIAO ESP32C3
 *
 * Features:
 *  - MAX30102 Heart Rate + SpO2
 *  - LIS3DH Accelerometer (motion detection)
 *  - 940nm reflectance channel (experimental)
 *  - OLED display
 *  - MQTT data streaming (skeleton)
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

// ====================== USER CONFIG ======================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_SERVER   = "192.168.1.100";   // Raspberry Pi IP
const int   MQTT_PORT     = 1883;
const char* MQTT_TOPIC    = "armband/ppg";

// ====================== PINS ======================
#define PIN_940NM_EMITTER  D6          // TSAL6200 drive pin
#define PIN_940NM_ADC      A0          // BPW34 via resistor to ADC

// ====================== OBJECTS ======================
MAX30105 particleSensor;
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
Adafruit_SSD1306 display(128, 64, &Wire, -1);

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ====================== HR / SpO2 ======================
const byte RATE_SIZE = 8;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int beatAvg = 0;

#define BUFFER_SIZE 100
uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
int bufferIndex = 0;

int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

float temperature = 0;
bool fingerDetected = false;

// ====================== Motion ======================
float accelX, accelY, accelZ;
float motionMagnitude = 0;
bool isMoving = false;

// ====================== 940nm ======================
int raw940 = 0;

// ====================== Timing ======================
unsigned long lastTempRead = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastDisplayUpdate = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Armband Full Firmware ===");

  // 940nm emitter pin
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
    display.println("Armband Booting...");
    display.display();
  }

  // MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
    while (1);
  }
  particleSensor.setup(60, 4, 2, 100, 411, 4096);
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("MAX30102 OK");

  // LIS3DH
  if (!lis.begin(0x18)) {          // try 0x18 or 0x19
    Serial.println("LIS3DH not found");
  } else {
    lis.setRange(LIS3DH_RANGE_4_G);
    Serial.println("LIS3DH OK");
  }

  // WiFi + MQTT
  setupWiFi();
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);

  Serial.println("Ready");
}

void loop() {
  // ---------- MQTT keep-alive ----------
  if (!mqtt.connected()) reconnectMQTT();
  mqtt.loop();

  // ---------- Collect PPG samples ----------
  while (bufferIndex < BUFFER_SIZE) {
    irBuffer[bufferIndex] = particleSensor.getIR();
    redBuffer[bufferIndex] = particleSensor.getRed();
    bufferIndex++;
  }
  bufferIndex = 0;

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE, redBuffer,
    &spo2, &validSPO2, &heartRate, &validHeartRate
  );

  long irValue = particleSensor.getIR();
  fingerDetected = (irValue >= 50000);

  if (fingerDetected && checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    float bpm = 60.0 / (delta / 1000.0);
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

  // ---------- Accelerometer ----------
  sensors_event_t event;
  lis.getEvent(&event);
  accelX = event.acceleration.x;
  accelY = event.acceleration.y;
  accelZ = event.acceleration.z;
  motionMagnitude = sqrt(accelX*accelX + accelY*accelY + accelZ*accelZ);
  isMoving = (motionMagnitude > 11.0);   // rough threshold (gravity ~9.8)

  // ---------- 940nm Channel ----------
  digitalWrite(PIN_940NM_EMITTER, HIGH);
  delayMicroseconds(50);
  raw940 = analogRead(PIN_940NM_ADC);
  digitalWrite(PIN_940NM_EMITTER, LOW);

  // ---------- Temperature ----------
  if (millis() - lastTempRead > 5000) {
    temperature = particleSensor.readTemperature();
    lastTempRead = millis();
  }

  // ---------- Serial debug ----------
  Serial.print("BPM="); Serial.print(beatAvg);
  Serial.print(" SpO2="); Serial.print(validSPO2 ? spo2 : -1);
  Serial.print(" Motion="); Serial.print(motionMagnitude, 1);
  Serial.print(" 940="); Serial.print(raw940);
  Serial.print(" Temp="); Serial.println(temperature, 1);

  // ---------- OLED ----------
  if (millis() - lastDisplayUpdate > 200) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  // ---------- MQTT publish ----------
  if (millis() - lastMqttPublish > 1000) {
    publishData();
    lastMqttPublish = millis();
  }
}

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
    display.print(motionMagnitude, 1);
    display.print(isMoving ? " MOV" : " still");

    display.setCursor(0, 52);
    display.print("940:");
    display.print(raw940);
    display.print(" T:");
    display.print(temperature, 1);
  }
  display.display();
}

void publishData() {
  if (!mqtt.connected()) return;

  char payload[160];
  snprintf(payload, sizeof(payload),
    "{\"bpm\":%d,\"spo2\":%d,\"temp\":%.1f,\"motion\":%.2f,\"raw940\":%d,\"moving\":%s}",
    beatAvg,
    validSPO2 ? spo2 : -1,
    temperature,
    motionMagnitude,
    raw940,
    isMoving ? "true" : "false"
  );

  mqtt.publish(MQTT_TOPIC, payload);
}

void setupWiFi() {
  Serial.print("WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" FAILED");
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connect("armband_ppg")) {
    Serial.println("MQTT connected");
  }
}
