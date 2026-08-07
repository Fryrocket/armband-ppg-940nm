// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Fryrocket

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
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
float temperature = 0;
bool fingerDetected = false;

unsigned long lastTempRead = 0;
const unsigned long TEMP_READ_INTERVAL = 5000;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== MAX30102 Heart Rate & Temperature Monitor ===");
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

  particleSensor.setup();
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

  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();

  // Finger detection
  if (irValue < 50000) {
    fingerDetected = false;
  } else {
    fingerDetected = true;
  }

  // Beat detection
  if (fingerDetected && checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60.0 / (delta / 1000.0);

    if (beatsPerMinute > 40 && beatsPerMinute < 180) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      // Calculate average
      beatAvg = 0;
      for (byte i = 0; i < RATE_SIZE; i++) {
        beatAvg += rates[i];
      }
      beatAvg /= RATE_SIZE;
    }
  }

  // Temperature every 5 seconds
  if (millis() - lastTempRead > TEMP_READ_INTERVAL) {
    temperature = particleSensor.readTemperature();
    lastTempRead = millis();
  }

  // ---------- Serial Output ----------
  Serial.print("IR=");
  Serial.print(irValue);
  Serial.print(" | Red=");
  Serial.print(redValue);
  Serial.print(" | BPM=");
  Serial.print(beatAvg);
  Serial.print(" | Temp=");
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
    // Title
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Heart Rate");

    // Big BPM number
    display.setTextSize(3);
    display.setCursor(0, 16);
    display.print(beatAvg);

    display.setTextSize(1);
    display.setCursor(70, 28);
    display.print("BPM");

    // Temperature
    display.setTextSize(1);
    display.setCursor(0, 52);
    display.print("Temp: ");
    display.print(temperature, 1);
    display.print(" C");
  }

  display.display();
  delay(20);
}
