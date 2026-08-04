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

| Feature                        | Status          | File |
|--------------------------------|-----------------|------|
| Heart Rate                     | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| SpO₂                           | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| Temperature                    | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| OLED Display                   | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| LIS3DH Accelerometer           | Implemented     | `Armband_Full.ino` |
| Motion Threshold + Filtering   | Implemented     | `Armband_Full.ino` |
| 940 nm Reflectance Channel     | Implemented     | `Armband_Full.ino` |
| 940 nm Multi-sample + EMA      | Implemented     | `Armband_Full.ino` |
| MQTT Streaming (user/pass)     | Implemented     | `Armband_Full.ino` |
| Battery Voltage Monitoring     | Implemented     | `Armband_Full.ino` |
| Deep Sleep (timer + GPIO)      | Implemented     | `Armband_Full.ino` |
| RTC motion state + wake-skip   | Implemented     | `Armband_Full.ino` |
| Motion Artifact Rejection      | Basic (threshold) | `Armband_Full.ino` |

## Deep Sleep Strategy

Battery life is critical on the 500 mAh LiPo. Implemented approach in `Armband_Full.ino`:

### Wake Sources
- **Timer wake** every 3 minutes (configurable via `PERIODIC_WAKE_US`) for periodic voltage + status reports.
- **GPIO wake** ready (commented) for LIS3DH INT1 – wire the interrupt pin and uncomment the two lines in `goToDeepSleep()` when ready.

### Flow
1. On wake → short settle delay.
2. Read LIS3DH (filtered magnitude + hysteresis threshold).
3. Read battery voltage (multi-sample ADC).
4. Read 940 nm (multi-sample + EMA filter).
5. Run PPG acquisition window if finger present.
6. Publish full JSON packet over MQTT (or skip network on quiet wakes).
7. If motion was detected this wake → stay awake longer so data can stream.
8. Power down sensors, OLED, WiFi/MQTT → deep sleep.

### Power Notes for XIAO ESP32C3
- MAX30102 LEDs are forced off before sleep.
- OLED is commanded off.
- WiFi is fully powered down (`WIFI_OFF`).
- Motion EMA state survives deep sleep via RTC memory.
- Quiet wakes can skip WiFi/MQTT (`QUIET_WAKE_SKIP`).
- Expect low tens of µA in deep sleep once peripherals are quiet; measure with a real meter.

## Documentation

| File | Purpose |
|------|---------|
| [SETUP.md](SETUP.md) | **Detailed setup instructions** – hardware, libraries, config, first run, troubleshooting |
| [NOTES.md](NOTES.md) | Development log, tuning guides, open questions |
| `firmware/Armband_Full.ino` | Main firmware |

## Repository Structure

```
armband-ppg-940nm/
├── README.md
├── SETUP.md                       # Detailed setup guide
├── NOTES.md                       # Development notes & tuning
├── platformio.ini
├── firmware/
│   ├── README.md
│   ├── MAX30102_Full_Monitor.ino
│   ├── MAX30102_HeartRate_Temp_OLED.ino
│   └── Armband_Full.ino           # Main firmware
└── firmware/pi-side/
```

## Quick Start

For a full walkthrough see **[SETUP.md](SETUP.md)**.

Short version:

1. Install the libraries (Arduino IDE Library Manager or PlatformIO)
2. Open `firmware/Armband_Full.ino`
3. Edit the **USER CONFIG** section (WiFi, MQTT, battery scale)
4. Upload to XIAO ESP32C3
5. Open Serial Monitor at 115200 and confirm wake → publish → sleep cycles
