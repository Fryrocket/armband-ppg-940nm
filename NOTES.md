# Project Notes – Armband PPG + 940nm

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

### README
Status table and Deep Sleep Strategy section updated to match the new code.

### Next actions (priority order)
1. Fill real WiFi / MQTT credentials and battery scale/offset
2. Upload and verify wake → publish → sleep cycle on Serial
3. Measure deep-sleep current with a real meter
4. Tune motion threshold and 940 nm filter while wearing
5. Decide whether ADC battery reading is sufficient or switch to INA219
6. Improve motion-artifact rejection for PPG (currently only magnitude gate)
7. Start Pi-side logging of the MQTT stream

### Open design questions
- Wire LIS3DH INT1 for pure motion wake?
- Keep OLED powered only during the awake window, or remove it for longer battery life?
- Adaptive sleep interval based on battery voltage / activity level?
