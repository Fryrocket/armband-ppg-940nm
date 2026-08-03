# Firmware

## Files

| File | Description |
|------|-------------|
| `MAX30102_Full_Monitor.ino` | Full version with Heart Rate, SpO2, Temperature + OLED |
| `MAX30102_HeartRate_Temp_OLED.ino` | Simpler version (HR + Temp + OLED only) |

## Required Libraries

Install these via Arduino Library Manager:

- **SparkFun MAX3010x** (includes `MAX30105.h`, `heartRate.h`, `spo2_algorithm.h`)
- **Adafruit SSD1306**
- **Adafruit GFX**

## Hardware

- Seeed XIAO ESP32C3 (or any ESP32 / Arduino with I2C)
- MAX30102 sensor
- SSD1306 128x64 OLED (I2C address 0x3C)

## Wiring

| MAX30102 / OLED | XIAO ESP32C3 |
|-----------------|--------------|
| VIN / VCC       | 3.3V         |
| GND             | GND          |
| SDA             | D4 (SDA)     |
| SCL             | D5 (SCL)     |

## Serial Commands

- `r` → Reset BPM average
- `t` → Force temperature reading
