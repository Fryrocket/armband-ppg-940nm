# Detailed Setup Instructions

This guide walks through getting `Armband_Full.ino` running on a Seeed XIAO ESP32C3 for the first time.

---

## 1. Hardware Checklist

| Item | Notes |
|------|-------|
| Seeed XIAO ESP32C3 | Main board |
| MAX30102 breakout | I²C (SDA/SCL) |
| LIS3DH breakout | I²C (same bus) |
| TSAL6200 (940 nm LED) | Driven by `D6` |
| BPW34 photodiode | Through resistor to `A0` |
| LiPo 3.7 V (500 mAh) | With JST |
| Voltage divider for battery | Two resistors → `A1` (see section 5) |
| OLED SSD1306 128×64 (optional) | I²C address 0x3C |

**Default pins used in the firmware**

| Function | Pin |
|----------|-----|
| 940 nm emitter | D6 |
| 940 nm ADC | A0 |
| Battery ADC | A1 |
| LIS3DH INT1 (optional wake) | D2 |
| I²C SDA / SCL | Board default (D4 / D5 on XIAO ESP32C3) |

If your wiring is different, change the defines at the top of `Armband_Full.ino`.

---

## 2. Install Libraries

### Option A – Arduino IDE

1. Open **Library Manager** (`Tools → Manage Libraries…`)
2. Install these exact libraries:

| Library | Search term |
|---------|-------------|
| SparkFun MAX3010x | `SparkFun MAX3010x Pulse and Proximity Sensor` |
| Adafruit SSD1306 | `Adafruit SSD1306` |
| Adafruit GFX | `Adafruit GFX Library` |
| Adafruit LIS3DH | `Adafruit LIS3DH` |
| PubSubClient | `PubSubClient` (Nick O'Leary) |

3. Install the **esp32** board package if you haven’t already (`Boards Manager` → search “esp32” by Espressif).

### Option B – PlatformIO (recommended)

A `platformio.ini` is already in the repo root. From the project folder:

```bash
pio run          # downloads libraries automatically
pio run -t upload
pio device monitor
```

---

## 3. Board & Port Settings (Arduino IDE)

- Board: **Seeed XIAO ESP32C3**
- Upload Speed: 921600 (or 460800 if unstable)
- USB CDC On Boot: Enabled (helps Serial after reset)
- Port: select the XIAO’s serial port

---

## 4. Configure the Firmware

Open `firmware/Armband_Full.ino` and edit the **USER CONFIG** block at the top.

### Required changes

```cpp
const char* WIFI_SSID     = "your_ssid";
const char* WIFI_PASSWORD = "your_password";

const char* MQTT_SERVER   = "192.168.x.x";   // Raspberry Pi IP
const char* MQTT_USER     = "armband";       // or "" if no auth
const char* MQTT_PASSWORD = "your_pass";     // or "" if no auth
```

### Recommended first-run settings

```cpp
const uint8_t QUIET_WAKE_SKIP = 0;   // connect every wake while testing
const uint64_t PERIODIC_WAKE_US = 60ULL * 1000000ULL;  // 1 minute for faster testing
```

Once everything works, change back to:

```cpp
const uint8_t QUIET_WAKE_SKIP = 2;
const uint64_t PERIODIC_WAKE_US = 180ULL * 1000000ULL;  // 3 minutes
```

---

## 5. Battery Voltage Divider

The firmware expects a voltage divider on `A1`.

Example (safe for 4.2 V LiPo):

```
Battery+ ── 100 kΩ ──┬── A1
                     |
                   100 kΩ
                     |
                   GND
```

With equal resistors the scale is ≈ 2.0:

```cpp
const float BATTERY_SCALE  = 2.0;
const float BATTERY_OFFSET = 0.0;
```

**Calibration steps**
1. Measure real battery voltage with a multimeter.
2. Upload the firmware and read the `batt` value from Serial/MQTT.
3. Adjust `BATTERY_SCALE` (and tiny `BATTERY_OFFSET` if needed) until they match.

---

## 6. First Upload & What You Should See

1. Upload `Armband_Full.ino`.
2. Open Serial Monitor at **115200** baud.
3. Expected output (approximate):

```
=== Armband wake  boot #1  reset=1 ===
MAX30102 OK
LIS3DH OK
WiFi........ OK
192.168.x.x
MQTT connected
Connect time: 1840 ms
{"bpm":0,"spo2":-1,"temp":0.0,"motion":9.81,"moving":false,"raw940":123,"filt940":123.0,"batt":3.97,"trans":"none","conn_ms":1840,"boot":1}
Deep sleep... (boot #1, quietSkip=0)
```

After the timer (or motion) the device wakes again, restores the previous motion EMA from RTC memory, and continues.

---

## 7. Verifying Key Behaviours

| Behaviour | How to check |
|-----------|--------------|
| RTC motion state | Move the armband, let it sleep, wake it – `motion` value should not restart near 0 |
| State transitions | Move / stop while watching Serial for `[MOTION] still → MOVING` |
| Wake-skip | Set `QUIET_WAKE_SKIP = 2`, keep still – every 3rd quiet wake should connect |
| Connection time | Look at `conn_ms` in the JSON |
| Deep sleep current | Measure with a multimeter in series (target: low tens of µA) |

---

## 8. Common Problems

**WiFi never connects**
- Double-check SSID/password (case sensitive)
- Move closer to the AP for first tests
- Confirm 2.4 GHz (ESP32-C3 has no 5 GHz)

**MQTT connects but nothing appears on the Pi**
- Verify broker IP and that Mosquitto (or whatever you use) is listening on 1883
- Check username/password
- Subscribe manually: `mosquitto_sub -h <ip> -t armband/ppg -v`

**MAX30102 or LIS3DH not found**
- Confirm I²C wiring and pull-ups
- Run an I²C scanner sketch to see which addresses respond

**Battery reading wildly wrong**
- Confirm divider ratio and that `A1` is the correct pin
- Make sure the divider ground is common with the XIAO

**High current in deep sleep**
- Confirm LEDs are off and `WiFi.mode(WIFI_OFF)` ran
- Disconnect any USB-UART adapter while measuring
- Check for continuously powered sensors or pull-ups to 3V3 that stay active

---

## 9. Returning to Normal Operation

After testing:

```cpp
const uint8_t QUIET_WAKE_SKIP = 2;
const uint64_t PERIODIC_WAKE_US = 180ULL * 1000000ULL;  // 3 min
```

Re-upload. The device will now spend most of its time in deep sleep and only connect when there is motion or every few quiet wakes.

---

## 10. Next After Setup Works

See `NOTES.md` for tuning motion thresholds, 940 nm filtering, and later improvements (INA219, better artifact rejection, Pi-side logging, etc.).
