# Armband PPG + 940nm Experimental Device

> **Part of [BGM](https://github.com/Fryrocket/BGM)** – the umbrella wearable blood-glucose monitoring project.  
> Companion edge-AI host: **[armband-ai](https://github.com/Fryrocket/armband-ai)** (Pi 5 + Hailo).

Arm-worn device for exercise use, built around the **Seeed XIAO ESP32C3**.

## Goals

- **Primary**: Reliable Heart Rate + SpO₂ using MAX30102
- **Motion handling**: LIS3DH accelerometer for activity detection, motion artifact rejection, and hardware wake
- **Experimental**: 940 nm reflectance channel (TSAL6200 + BPW34) for personal glucose estimation experiments
- Calibrated against FreeStyle Libre
- Data sent via MQTT to a Raspberry Pi for logging and later personal ML models

> **Important**: The 940 nm glucose estimate is experimental only. It is **not** a medical device and is not expected to match FreeStyle Libre accuracy, especially during exercise.

## Hardware

| Component              | Details                                      |
|------------------------|----------------------------------------------|
| Microcontroller        | Seeed Studio XIAO ESP32C3                    |
| PPG Sensor             | MAX30102                                     |
| Accelerometer          | LIS3DH (INT1 → GPIO D2)                      |
| 940 nm Emitter         | TSAL6200                                     |
| 940 nm Detector        | BPW34 Photodiode                             |
| Battery                | Liter 3.7V 500 mAh LiPo (502535) with JST    |
| Wire                   | 28 AWG Silicone                              |
| Connectors             | JST-SH (2/4/6/8/10 pin)                      |
| Board                  | 2×8 cm Double-sided Protoboard               |
| Mount                  | HYS Adjustable Elastic Armband               |
| Solder                 | MAIYUM 63/37 0.8 mm rosin-core               |

## Current Firmware Status

| Feature                              | Status          | File |
|--------------------------------------|-----------------|------|
| Heart Rate                           | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| SpO₂                                 | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| Temperature                          | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| OLED Display                         | Working         | `MAX30102_Full_Monitor.ino` / `Armband_Full.ino` |
| LIS3DH Accelerometer                 | Working         | `Armband_Full.ino` |
| Software Motion Threshold + Filtering| Working         | `Armband_Full.ino` |
| **Hardware INT1 Motion Wake**        | **Working**     | `Armband_Full.ino` |
| 940 nm Reflectance Channel           | Implemented     | `Armband_Full.ino` |
| 940 nm Multi-sample + EMA            | Implemented     | `Armband_Full.ino` |
| MQTT Streaming (user/pass)           | Implemented     | `Armband_Full.ino` |
| Battery Voltage Monitoring           | Implemented     | `Armband_Full.ino` |
| Deep Sleep (timer + GPIO INT1)       | Working         | `Armband_Full.ino` |
| RTC motion state + wake-skip         | Working         | `Armband_Full.ino` |
| Motion Artifact Rejection            | Basic (threshold)| `Armband_Full.ino` |

## Deep Sleep Strategy

Battery life is critical on the 500 mAh LiPo. Current approach in `Armband_Full.ino`:

### Wake Sources
- **Hardware motion wake** via LIS3DH INT1 (active-low, latched, high-pass filtered) on GPIO D2.
- **Timer wake** every 3 minutes (`PERIODIC_WAKE_US`) as a backup for periodic status / battery reports when the device is quiet.

### INT1 Configuration
- Range: ±2g
- High-pass filter enabled on the interrupt path (rejects gravity / slow tilt)
- OR combination of X/Y/Z high events
- Threshold: 10 LSB ≈ 160 mg (tunable via `INT1_THRESHOLD_LSB`)
- Debounce: 5 samples (tunable via `INT1_DURATION_LSB`)
- Latched interrupt (cleared by reading `INT1_SRC`)
- Active-low polarity

### Flow
1. On wake → detect cause (EXT0 motion / timer / power-on).
2. Clear any latched INT1 interrupt.
3. Restore RTC motion state (EMA + isMoving).
4. Read battery, 940 nm, and run PPG if finger present.
5. Publish full JSON packet over MQTT (or skip network on quiet wakes via `QUIET_WAKE_SKIP`).
6. If motion was detected this wake → stay awake longer so data can stream.
7. Clear interrupt again, power down sensors / OLED / WiFi → deep sleep.

### Power Notes for XIAO ESP32C3
- MAX30102 LEDs are forced off before sleep.
- OLED is commanded off.
- WiFi is fully powered down (`WIFI_OFF`).
- Motion EMA state and isMoving flag survive deep sleep via RTC memory.
- Quiet wakes can skip WiFi/MQTT (`QUIET_WAKE_SKIP`).
- Expect low tens of µA in deep sleep once peripherals are quiet; measure with a real meter.

## Documentation

| File | Purpose |
|------|---------|
| **[PINOUT.md](PINOUT.md)** | **Printable pinout + wire color card** – keep next to the soldering iron |
| [SETUP.md](SETUP.md) | **Detailed setup instructions** – hardware, libraries, config, first run, troubleshooting |
| [NOTES.md](NOTES.md) | Development log, tuning guides, open questions |
| `firmware/Armband_Full.ino` | Main firmware |

## Repository Structure

```
armband-ppg-940nm/
├── README.md
├── PINOUT.md                      # Printable pinout + wire colors
├── SETUP.md                       # Detailed setup guide
├── NOTES.md                       # Development notes & tuning
├── platformio.ini
├── firmware/
│   ├── README.md
│   ├── MAX30102_Full_Monitor.ino
│   ├── MAX30102_HeartRate_Temp_OLED.ino
│   └── Armband_Full.ino           # Main firmware (with INT1 wake)
└── firmware/pi-side/
```

## Quick Start

For a full walkthrough see **[SETUP.md](SETUP.md)**.

Short version:

1. Install the libraries (Arduino IDE Library Manager or PlatformIO)
2. Open `firmware/Armband_Full.ino`
3. Edit the **USER CONFIG** section (WiFi, MQTT, battery scale, INT1 threshold if needed)
4. Wire LIS3DH INT1 to XIAO D2
5. Upload to XIAO ESP32C3
6. Open Serial Monitor at 115200 and confirm:
   - Boot → configure INT1 → sleep
   - Motion on arm → immediate wake → publish → sleep
   - Or 3-minute timer wake when quiet
