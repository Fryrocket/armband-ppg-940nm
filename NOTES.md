# Project Notes – Armband PPG + 940nm

## Session 2026-08-08 – MLP training gate consistency (armband-ai)

Second calibration-path fix applied to companion `armband-ai`:

`scripts/train_mlp_onnx.py` previously:

1. Called `build_calibration_pairs()` **without** `min_clean_streak`, so the consecutive-clean gate used by `calibrate.py` and `train_multifeature.py` was silently ignored for the MLP path.
2. Re-pulled raw PPG rows and re-filtered stillness with an ad-hoc rule (`>=4 still samples`) that did **not** match `prefer_still` (any still sample ⇒ filter to still-only). The `quality_score` attached to a training row could therefore describe a different candidate window than the feature vector that was actually trained on.

Fix:

- Passes `min_clean_streak` (CLI default 10; 0 = off) through to `build_calibration_pairs`.
- Re-filter now mirrors `prefer_still` exactly.
- Added `--min-clean-streak` and `--no-prefer-still` CLI flags for parity with the other training scripts.

See `armband-ai` commit and BGM `docs/STATUS.md`.

---

## Session 2026-08-08 – AI calibration gate-order fixes (armband-ai)

The companion `armband-ai` repo received the final quality-score ordering fix so all three calibration gates now evaluate the **raw** window:

1. **still_fraction** – computed on the unfiltered window before prefer-still.
2. **quality_score** – now scored via `score_window(raw_feats)` before prefer-still (previously ran on the already-cleaned subset and was artificially inflated).
3. **max_clean_streak / clean_fraction** – consecutive still + optically stable run length, also gated on the raw window.

Prefer-still still controls which samples are averaged into `filt940_mean`; it no longer affects what is scored or gated. See `armband-ai` `src/armband_ai/calibration.py` and BGM `docs/STATUS.md` for the full write-up.

These changes only affect the Pi-side pairing / model training path. Firmware behaviour is unchanged.

---

## Session 2026-08-06 / 2026-08-07

### Fixes applied to `firmware/Armband_Full.ino`

1. **Deep-sleep wake (ESP32-C3)**  
   - Replaced `esp_sleep_enable_ext0_wakeup()` with `esp_deep_sleep_enable_gpio_wakeup(BIT(PIN_LIS3DH_INT), ESP_GPIO_WAKEUP_GPIO_LOW)`.  
   - Updated wake-cause check from `ESP_SLEEP_WAKEUP_EXT0` to `ESP_SLEEP_WAKEUP_GPIO`.  
   - EXT0 is not the right path on C3; the GPIO deep-sleep API is what the XIAO ESP32-C3 needs. Requires a reasonably current Arduino-ESP32 core (≥ ~2.0.9).

2. **MAX30102 PPG FIFO reading**  
   - Buffer-fill loop now properly calls `check()` / `available()` / `getFIFOIR()` / `getFIFORed()` / `nextSample()` so new samples are pulled from the sensor over I2C and the read pointer advances.  
   - Previously the loop just re-read the same cached value.  
   - Finger-detect / beat-detect `irValue` now comes from the freshly filled buffer.

These two changes should make motion INT1 wake reliable and stop the “same PPG sample forever” behavior.

---

## Session 2026-08-03

### What was done

Full implementation pushed into `firmware/Armband_Full.ino`:

- **Deep sleep**
  - Timer wake (default 3 minutes)
  - GPIO wake ready (LIS3DH INT1, currently commented)
  - Motion events keep the device awake longer so data can stream
  - Clean power-down of MAX30102 LEDs, OLED, and WiFi before sleep

- **Battery monitoring**
  - Multi-sample ADC + scale/offset (user-configurable)
  - Published on every wake

- **Motion handling**
  - EMA-filtered magnitude
  - Hysteresis threshold (tunable)
  - `isMoving` flag used for both local decisions and MQTT payload

- **940 nm channel**
  - Multi-sample average + EMA filter
  - Both raw and filtered values published

- **MQTT**
  - Full user/password support
  - JSON payload now includes: bpm, spo2, temp, motion, moving, raw940, filt940, batt

All tunable values live in the **USER CONFIG** block at the top of `Armband_Full.ino`.

### Libraries added

Created `platformio.ini` at repo root with exact library dependencies:

- sparkfun/SparkFun MAX3010x Pulse and Proximity Sensor
- adafruit/Adafruit SSD1306
- adafruit/Adafruit GFX Library
- adafruit/Adafruit LIS3DH
- knolleary/PubSubClient

README Quick Start updated with:
- Precise Arduino IDE Library Manager search terms
- PlatformIO one-command install/upload instructions

### Later same session – power & state improvements

Implemented the three items raised in review:

1. **RTC-persistent motion state**
   - `rtcFilteredMotion` and `rtcIsMoving` now live in RTC memory (`RTC_DATA_ATTR`)
   - EMA and hysteresis state survive deep sleep – no more ramp-from-zero on every wake

2. **Wake-skip counter + connection timing**
   - `QUIET_WAKE_SKIP` (default 2) – quiet wakes can skip WiFi/MQTT to save power
   - Counter itself is also in RTC memory
   - `connectTimeMs` measures WiFi + MQTT handshake duration and is published in the JSON

3. **Motion state-transition logging**
   - Detects `still → moving` and `moving → still`
   - Prints clear Serial messages
   - Adds `"trans":"still_to_moving"|"moving_to_still"|"none"` to the MQTT payload

New JSON fields: `trans`, `conn_ms`, `boot`

---

## Detailed Next Steps

### 1. Fill real configuration values

Open `firmware/Armband_Full.ino` and edit the **USER CONFIG** section at the top:

```cpp
// WiFi
const char* WIFI_SSID     = "your_real_ssid";
const char* WIFI_PASSWORD = "your_real_password";

// MQTT
const char* MQTT_SERVER   = "192.168.x.x";     // Raspberry Pi IP
const char* MQTT_USER     = "armband";         // or "" if no auth
const char* MQTT_PASSWORD = "your_mqtt_pass";  // or "" if no auth

// Battery – measure with a multimeter
// Example: if ADC reads 1.65 V when battery is 3.30 V → scale = 2.0
const float BATTERY_SCALE  = 2.0;
const float BATTERY_OFFSET = 0.0;

// Power / skip behaviour
const uint8_t QUIET_WAKE_SKIP = 2;   // 0 = connect every wake
```

Also confirm these pins match your wiring:

- `PIN_940NM_EMITTER` (currently D6)
- `PIN_940NM_ADC` (currently A0)
- `PIN_BATTERY_ADC` (currently A1)

### 2. First upload & basic verification

**Arduino IDE**
1. Select board: `Seeed XIAO ESP32C3`
2. Open `firmware/Armband_Full.ino`
3. Upload
4. Open Serial Monitor at **115200**

**PlatformIO**
```bash
pio run -t upload
pio device monitor
```

What you should see:
- Boot message with boot count and reset reason
- Motion state restored from RTC (no longer starts at zero)
- Either a network connect + `conn_ms` value, or “Quiet wake – skipping network”
- JSON payload (when network is used)
- “Deep sleep...” message

After ~3 minutes it should wake again and repeat.

### 3. Verify deep-sleep current

1. Power the board from a bench supply or battery with a multimeter in series (µA range).
2. Let it enter deep sleep.
3. Target: low tens of µA once peripherals are quiet.
4. If current is high (hundreds of µA or mA):
   - Confirm MAX30102 LEDs are off
   - Confirm OLED is commanded off
   - Confirm `WiFi.mode(WIFI_OFF)` is executing
   - Check for floating pins or continuously powered external parts

### 4. Tune motion detection while wearing

Wear the armband and move normally (walking, arm swings, still periods).

Watch Serial for `[MOTION] still → MOVING` / `[MOTION] MOVING → still` and the `trans` field in MQTT.

Adjust these in USER CONFIG:

| Constant              | Effect                                      | Typical starting range |
|-----------------------|---------------------------------------------|------------------------|
| `MOTION_THRESHOLD`    | Magnitude above which motion is declared    | 10.5 – 13.0            |
| `MOTION_HYSTERESIS`   | Prevents rapid on/off chatter               | 0.5 – 1.2              |
| `MOTION_EMA_ALPHA`    | How fast the filter tracks changes (higher = faster) | 0.15 – 0.35     |

Because the EMA now survives sleep, tuning should feel more consistent across wakes.

### 5. Tune 940 nm filtering

Watch `raw940` vs `filt940` in the JSON.

| Constant              | Effect                                      | Notes |
|-----------------------|---------------------------------------------|-------|
| `RAW940_SAMPLES`      | Number of ADC samples averaged              | 4–16  |
| `RAW940_EMA_ALPHA`    | Smoothing of the filtered value             | 0.1–0.3 |

### 6. Battery reading accuracy check

1. Measure real battery voltage with a multimeter.
2. Compare to the `batt` value published over MQTT.
3. Adjust `BATTERY_SCALE` and `BATTERY_OFFSET` until they match across the useful range (≈3.3 V – 4.2 V).

### 7. Optional – enable pure motion wake

1. Wire LIS3DH INT1 to pin D2 (or change `PIN_LIS3DH_INT`).
2. In `goToDeepSleep()`, the GPIO wake is now active (ESP32-C3 path).
3. Test that motion wakes the device even before the timer expires.

### 8. Later improvements (after basics are solid)

- Better motion-artifact rejection for PPG (currently only a magnitude gate)
- Adaptive sleep interval (shorter when battery is low or activity is high)
- INA219 for accurate voltage + current
- Raspberry Pi side: MQTT subscriber + logging + simple plots of 940 nm vs FreeStyle Libre
- Decide whether to keep the OLED long-term or remove it for better battery life

---

### Open design questions

- Wire LIS3DH INT1 for pure motion wake?
- Keep OLED powered only during the awake window, or remove it for longer battery life?
- Adaptive sleep interval based on battery voltage / activity level?
