# Armband PPG + 940nm Experimental Device

Arm-worn device for exercise use, built around the Seeed XIAO ESP32C3.

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

## Planned Features

### Reliable
- Heart Rate
- SpO₂
- Motion / activity detection
- Motion artifact rejection
- Battery-powered arm-worn operation
- Wireless data streaming (MQTT)

### Experimental
- 940 nm optical reflectance measurement
- Personal calibration against FreeStyle Libre readings
- Feature extraction + simple personal ML model (on Pi)

## Planned Repository Structure