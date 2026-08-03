# Firmware

## Files

| File | Description |
|------|-------------|
| `Armband_Full.ino` | **Main development firmware** – HR, SpO2, LIS3DH, 940nm, MQTT, OLED |
| `MAX30102_Full_Monitor.ino` | Clean standalone MAX30102 (HR + SpO2 + Temp + OLED) |
| `MAX30102_HeartRate_Temp_OLED.ino` | Minimal version (HR + Temp + OLED only) |

## Required Libraries

- SparkFun MAX3010x Pulse and Proximity Sensor
- Adafruit SSD1306
- Adafruit GFX
- Adafruit LIS3DH
- PubSubClient

## Hardware Notes (XIAO ESP32C3)

| Function          | Pin     |
|-------------------|---------| 
| I2C SDA           | D4      |
| I2C SCL           | D5      |
| 940nm Emitter     | D6      |
| 940nm ADC (BPW34) | A0      |

## MQTT Payload Example

```json
{
  "bpm": 72,
  "spo2": 97,
  "temp": 36.4,
  "motion": 9.85,
  "raw940": 1842,
  "moving": false
}
```
