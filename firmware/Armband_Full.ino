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
#define PIN_LIS3DH_INT     D2          // LIS3DH INT1 for hardware motion wake

// --- Motion threshold (software EMA / hysteresis) ---
const float MOTION_THRESHOLD     = 11.5;   // magnitude above ~g to count as moving
const float MOTION_HYSTERESIS    = 0.8;    // to prevent chatter
const float MOTION_EMA_ALPHA     = 0.25;   // filtering on magnitude (0.1–0.4)

// --- Hardware INT1 threshold (tune these) ---
// At ±2g, 1 LSB ≈ 16 mg. 10 LSB ≈ 160 mg
const uint8_t INT1_THRESHOLD_LSB = 10;     // 5–30 recommended
const uint8_t INT1_DURATION_LSB  = 5;      // debounce samples (1–10)

// --- 940nm filtering ---
const int   RAW940_SAMPLES       = 8;     // multi-sample average
const float RAW940_EMA_ALPHA     = 0.2;   // exponential filter

// --- Battery ---
const float BATTERY_SCALE        = 2.0;
const float BATTERY_OFFSET       = 0.0;
const int   BATTERY_SAMPLES      = 12;

// --- Deep sleep / power ---
const uint64_t PERIODIC_WAKE_US  = 180ULL * 1000000ULL;  // 3 minutes timer wake (backup)
const unsigned long AWAKE_WINDOW_MS = 12000;             // stay awake after motion
const unsigned long QUIET_AWAKE_MS  = 5500;              // shorter awake when quiet
const unsigned long SETTLE_MS       = 120;

// How many quiet (no-motion) wakes to skip before forcing a network connect
// 0 = connect every wake. 2 = connect on every 3rd quiet wake, etc.
const uint8_t QUIET_WAKE_SKIP = 2;

// =============================================================================
// END USER CONFIG
// =============================================================================
