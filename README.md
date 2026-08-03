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
