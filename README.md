# Armband PPG + 940nm Experimental Device

Arm-worn device for exercise use, built around the **Seeed XIAO ESP32C3**.

## Goals

- **Primary**: Reliable Heart Rate + SpO₂ using MAX30102
- **Motion handling**: LIS3DH accelerometer for activity detection and motion artifact rejection
- **Experimental**: 940 nm reflectance channel (TSAL6200 + BPW34) for personal glucose estimation experiments
- Calibrated against FreeStyle Libre
- Data sent via MQTT to a Raspberry Pi 4 for logging and later personal ML models

> **Important**: The 940 nm glucose estimate is experimental only. It is **not** a medical device and is not expected to match FreeStyle Libre accuracy, especially during exercise.

## Hardware

| Component              | Details                                      |
|------------------------|----------------------------------------------|
| Microcontroller        | Seeed Studio XIAO ESP32C3                    |
| PPG Sensor             | MAX30102                                     |
| Accelerometer          | LIS3DH                                       |
| 940 nm Emitter         | TSAL6200                                     |
| 940 nm Detector        | BPW34 Photodiode                             |
| Battery                | Liter 3.7V 500 mAh LiPo (502535) with JST    |
| Wire                   | 28 AWG Silicone                              |
| Connectors             | JST-SH (2/4/6/8/10 pin)                      |
| Board                  | 2×8 cm Double-sided Protoboard               |
| Mount                  | HYS Adjustable Elastic Armband               |
| Solder                 | MAIYUM 63/37 0.8 mm rosin-core               |

## Current Firmware Status

| Feature                        | Status      | File |
|--------------------------------|-------------|------|
| Heart Rate                     | Working     | `MAX30102_Full_Monitor.ino` |
| SpO₂                           | Working     | `MAX30102_Full_Monitor.ino` |
| Temperature                    | Working     | `MAX30102_Full_Monitor.ino` |
| OLED Display                   | Working     | `MAX30102_Full_Monitor.ino` |
| LIS3DH Accelerometer           | Skeleton    | `Armband_Full.ino` |
| 940 nm Reflectance Channel     | Skeleton    | `Armband_Full.ino` |
| MQTT Streaming                 | Skeleton    | `Armband_Full.ino` |
| Motion Artifact Rejection      | Planned     | - |
| Battery Monitoring             | Planned     | - |
| Deep Sleep                     | Notes added | See below |

## Deep Sleep Strategy (Notes)

Battery life is critical on the 500 mAh LiPo. Target approach for `Armband_Full.ino`:

### Wake Sources
- **Ext0 / GPIO wake** on LIS3DH interrupt pin (or a dedicated motion threshold pin) for activity-triggered sampling.
- **Timer wake** for periodic background reports (e.g. every 2–5 minutes) so MQTT still gets voltage + status even when still.
- Combine both: motion events wake immediately; timer provides the heartbeat.

### Recommended Flow
1. On wake → quick settle delay (50–150 ms).
2. Read LIS3DH (check motion magnitude / activity level).
3. If motion above threshold → full PPG + 940 nm acquisition window, then publish.
4. Always read battery voltage (ADC or future INA219) and publish a short status packet.
5. Disconnect WiFi/MQTT cleanly.
6. Re-arm wake sources and call `esp_deep_sleep_start()`.

### Power Notes for XIAO ESP32C3
- Use RTC-capable GPIO for the wake pin.
- Turn off the MAX30102 LED and put sensors into low-power / sleep modes before deep sleep.
- WiFi should be fully powered down (`WiFi.mode(WIFI_OFF)` or equivalent) before sleeping.
- Expect ~10–30 µA range in deep sleep if peripherals are properly shut down; validate with a real meter.
- Consider a short “post-motion awake” window (a few seconds) so multiple MQTT messages can finish before sleeping again.

### Battery Monitoring
- Current plan: use the ESP32-C3 ADC with a simple voltage divider on the LiPo (or add a small INA219 later for better accuracy + current).
- Publish voltage (and current if available) on both motion wakes and periodic timer wakes.
- Optional future: low-voltage cutoff that forces a permanent deep sleep until charged.

These notes are the starting point. Implementation details will be added to `Armband_Full.ino` as the skeleton is filled out.

## Repository Structure

```
armband-ppg-940nm/
├── README.md
├── firmware/
│   ├── README.md
│   ├── MAX30102_Full_Monitor.ino      # Clean HR + SpO2 + Temp + OLED
│   ├── MAX30102_HeartRate_Temp_OLED.ino
│   └── Armband_Full.ino               # Main development firmware (all sensors)
└── firmware/pi-side/                 # Raspberry Pi side code (future)
```

## Quick Start

1. Install libraries (Arduino Library Manager):
   - SparkFun MAX3010x Pulse and Proximity Sensor
   - Adafruit SSD1306
   - Adafruit GFX
   - Adafruit LIS3DH
   - PubSubClient (for MQTT)

2. Open `firmware/Armband_Full.ino`
3. Update WiFi + MQTT credentials
4. Upload to XIAO ESP32C3
