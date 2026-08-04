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

### Pre-flight: I²C Scanner (strongly recommended)

Before uploading the main firmware, run a quick I²C scanner so you know which addresses are actually present. This prevents silent “sensor not found” failures caused by address conflicts or wiring mistakes.

Typical addresses:
- MAX30102 → usually `0x57`
- LIS3DH → `0x18` or `0x19`
- SSD1306 OLED → `0x3C` (sometimes `0x3D`)

Paste this into a blank sketch, upload, and open Serial Monitor at **115200**:

```cpp
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Serial.println("\nI2C scanner");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found device at 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
    }
  }
  Serial.println("Done");
  // If MAX30102 (0x57) is missing, check wiring and pull-ups first
}

void loop() {}
```

If a sensor is missing from the list, fix the wiring or address before continuing.

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

A `platformio.ini` is already in the repo root.

- **CLI**: from the project folder run `pio run -t upload` then `pio device monitor`
- **GUI**: open the folder in VS Code with the PlatformIO extension installed – it will use the same `platformio.ini` and pull the libraries automatically.

---

## 3. Board & Port Settings (Arduino IDE)

- Board: **Seeed XIAO ESP32C3**
- Upload Speed: **921600** (works on most machines). If the upload fails or is extremely slow, drop to **460800**.
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
const char* MQTT_USER     = "armband";       // or "" if broker has no auth
const char* MQTT_PASSWORD = "your_pass";     // or "" if broker has no auth
```

If your MQTT broker does **not** require authentication, set both `MQTT_USER` and `MQTT_PASSWORD` to empty strings (`""`). The firmware already handles that case.

### Recommended first-run settings

```cpp
const uint8_t QUIET_WAKE_SKIP = 0;   // connect every wake while testing
const uint64_t PERIODIC_WAKE_US = 60ULL * 1000000ULL;  // 1 minute for faster testing
```

> **⚠️ Battery warning**  
> `QUIET_WAKE_SKIP = 0` makes the device connect to WiFi on **every** wake. This is excellent for debugging but will drain a 500 mAh cell in roughly 30–60 minutes of continuous testing.  
> Unplug USB / stop the test when you are not actively watching Serial. Switch back to the production values (section 9) as soon as basic function is confirmed.

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
2. Open Serial Monitor.

> **⚠️ MUST be 115200 baud**  
> Set the Serial Monitor to **115200**. If you leave it at 9600 you will only see garbage characters.

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

First connection is often slower (1.5–2.5 s) because of the full WiFi handshake. Later wakes are typically 800–1200 ms if the access point still has the device cached.

**Why the motion value does not reset to zero**  
The motion EMA (`filteredMotion`) and `isMoving` flag are stored in RTC memory. RTC memory survives deep sleep, so the filter continues from where it left off. This prevents a false “still” reading every time the device wakes.

---

## 7. Verifying Key Behaviours

| Behaviour | How to check |
|-----------|--------------|
| RTC motion state | Move the armband, let it sleep, wake it – `motion` value should **not** restart near 0 |
| State transitions | Move / stop while watching Serial for `[MOTION] still → MOVING` |
| Wake-skip | Set `QUIET_WAKE_SKIP = 2` and keep the armband **perfectly still**. WiFi should skip 2 quiet wakes, then connect on the 3rd. Any small motion resets the skip counter. |
| Connection time | Look at `conn_ms` in the JSON |
| Deep sleep current | See exact measurement method below |

### Measuring deep-sleep current

1. Disconnect the USB cable / USB-UART adapter completely.
2. Power the board from the LiPo only.
3. Put a multimeter in series between the battery positive terminal and the board’s power input (or the 3V3 rail if you prefer).
4. Set the meter to µA range.
5. Let the device enter deep sleep.
6. Expect roughly **20–50 µA** once everything is quiet.  
   I²C pull-ups draw a few µA by design and are normal. Total should still stay under ~100 µA. If you see **>200 µA**, suspect WiFi or the MAX30102 not shutting down properly.

---

## 8. Common Problems

**WiFi never connects**
- Double-check SSID/password (case sensitive)
- Move closer to the AP for first tests
- Confirm 2.4 GHz (ESP32-C3 has no 5 GHz)

**MQTT connects but nothing appears on the Pi**
- Verify broker IP and that Mosquitto (or whatever you use) is listening on 1883
- Check username/password (or that both are `""` if the broker has no auth)
- Subscribe manually: `mosquitto_sub -h <ip> -t armband/ppg -v`

**MAX30102 or LIS3DH not found**
- Run the I²C scanner from section 1
- Confirm wiring and pull-ups
- Some breakouts use different addresses – update the code if needed

**Battery reading wildly wrong**
- Confirm divider ratio and that `A1` is the correct pin
- Make sure the divider ground is common with the XIAO

**High current in deep sleep**
- Confirm LEDs are off and `WiFi.mode(WIFI_OFF)` ran
- Disconnect any USB-UART adapter while measuring
- I²C pull-ups are normal (a few µA). >200 µA usually means WiFi or MAX30102 is still active

**Garbage on Serial**
- Baud rate is almost always wrong – set Monitor to **115200**

---

## 9. Returning to Normal Operation

After testing, change the two values back to production settings:

```cpp
const uint8_t QUIET_WAKE_SKIP = 2;
const uint64_t PERIODIC_WAKE_US = 180ULL * 1000000ULL;  // 3 min
```

**You must re-upload** the sketch for these changes to take effect. Editing the defines alone does nothing until the new binary is flashed.

With these settings the device spends most of its time in deep sleep and only connects when there is motion or every few quiet wakes.

**Message volume & QoS**  
At 3-minute wakes with `QUIET_WAKE_SKIP = 2` you get roughly 400–500 MQTT messages per day. The firmware uses **QoS 0** (PubSubClient default). This is intentional for non-critical health telemetry (fire-and-forget). If you later need guaranteed delivery you would change the publish call to QoS 1 in the firmware itself.

---

## 10. Next After Setup Works

See **[NOTES.md](NOTES.md)** for:

- Tuning motion thresholds and hysteresis
- Tuning 940 nm filtering
- Battery accuracy checks
- Optional pure motion wake via LIS3DH INT
- Later improvements (INA219, better artifact rejection, Pi-side logging, etc.)
