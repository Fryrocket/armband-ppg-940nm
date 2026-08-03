#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

// ====================== OLED ======================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ====================== SENSOR ======================
MAX30105 particleSensor;

const byte RATE_SIZE = 8;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

float beatsPerMinute = 0;
int beatAvg = 0;

// SpO2 buffers
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

unsigned long lastTempRead = 0;
const unsigned long TEMP_READ_INTERVAL = 5000;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== MAX30102 Full Monitor (HR + SpO2 + Temp) ===");
  Serial.println("Commands: r = reset average | t = force temp read");

  // ---------- OLED ----------
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("WARNING: SSD1306 OLED not found at 0x3C");
    Serial.println("Continuing without display...");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("MAX30102 Starting...");
    display.display();
    delay(1000);
  }

  // ---------- Sensor ----------
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX3010x not found! Check wiring.");
    while (1);
  }

  Serial.println("Sensor initialized successfully");

  // Recommended settings for SpO2 + Heart Rate
  byte ledBrightness = 60;
  byte sampleAverage = 4;
  byte ledMode = 2;          // Red + IR
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);

  Serial.println("Ready. Place finger on sensor.\n");
}

void loop() {
  // ---------- Serial Commands ----------
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      beatAvg = 0;
      rateSpot = 0;
      memset(rates, 0, RATE_SIZE);
      Serial.println("Average reset");
    }
    if (c == 't' || c == 'T') {
      temperature = particleSensor.readTemperature();
      Serial.print("Temperature: ");
      Serial.print(temperature, 1);
      Serial.println(" C");
    }
  }

  // Collect samples for SpO2 algorithm
  while (bufferIndex < BUFFER_SIZE) {
    irBuffer[bufferIndex] = particleSensor.getIR();
    redBuffer[bufferIndex] = particleSensor.getRed();
    bufferIndex++;
  }
  bufferIndex = 0;

  // Calculate SpO2 and HR
  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, BUFFER_SIZE,
    redBuffer,
    &spo2, &validSPO2,
    &heartRate, &validHeartRate
  );

  long irValue = particleSensor.getIR();

  // Finger detection
  if (irValue < 50000) {
    fingerDetected = false;
  } else {
    fingerDetected = true;
  }

  // Beat detection (refinement)
  if (fingerDetected && checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60.0 / (delta / 1000.0);

    if (beatsPerMinute > 40 && beatsPerMinute < 180) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      beatAvg = 0;
      for (byte i = 0; i < RATE_SIZE; i++) {
        beatAvg += rates[i];
      }
      beatAvg /= RATE_SIZE;
    }
  }

  // Prefer algorithm HR if valid
  if (validHeartRate && heartRate > 40 && heartRate < 180) {
    beatAvg = heartRate;
  }

  // Temperature every 5 seconds
  if (millis() - lastTempRead > TEMP_READ_INTERVAL) {
    temperature = particleSensor.readTemperature();
    lastTempRead = millis();
  }

  // ---------- Serial Output ----------
  Serial.print("IR=");
  Serial.print(irValue);
  Serial.print(" | BPM=");
  Serial.print(beatAvg);
  Serial.print(" | SpO2=");
  if (validSPO2) Serial.print(spo2);
  else Serial.print("--");
  Serial.print("% | Temp=");
  Serial.print(temperature, 1);
  Serial.println("C");

  // ---------- OLED Display ----------
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!fingerDetected) {
    display.setTextSize(2);
    display.setCursor(18, 18);
    display.println("Place");
    display.setCursor(10, 42);
    display.println("Finger");
  } else {
    // BPM
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("BPM");
    display.setTextSize(2);
    display.setCursor(0, 12);
    display.print(beatAvg);

    // SpO2
    display.setTextSize(1);
    display.setCursor(70, 0);
    display.print("SpO2");
    display.setTextSize(2);
    display.setCursor(70, 12);
    if (validSPO2) {
      display.print(spo2);
      display.print("%");
    } else {
      display.print("--");
    }

    // Temperature
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print("Temp: ");
    display.print(temperature, 1);
    display.print(" C");
  }

  display.display();
  delay(20);
}
