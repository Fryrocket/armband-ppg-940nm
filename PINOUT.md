# Armband Pinout Reference Card

**Print this page** and keep it next to the soldering iron.

Target: **Seeed XIAO ESP32C3**  
Firmware: `firmware/Armband_Full.ino`  
Wire: 28 AWG silicone + JST-SH

**Official board schematic (PDF):**  
[XIAO ESP32-C3 Schematic v1.3](https://files.seeedstudio.com/wiki/XIAO_WiFi/Resources/XIAO_ESP32C3_v1.3_SCH_260116.pdf)  
Wiki / pin map: [XIAO ESP32C3 Getting Started](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)

---

## Project Pin Map (Firmware Defines)

| Function              | Arduino Pin / Pad      | Chip GPIO | Wire Color (recommended) | Notes |
|-----------------------|------------------------|-----------|---------------------------|-------|
| **Battery +**         | **Onboard battery pads / JST** | —    | **Red**                   | LiPo+ **only** to XIAO battery input (never 3V3) |
| **GND**               | GND                    | —         | **Black**                 | Common ground |
| **I²C SDA**           | **D4**                 | GPIO6     | **White**                 | MAX30102 + LIS3DH + OLED |
| **I²C SCL**           | **D5**                 | GPIO7     | **Yellow**                | MAX30102 + LIS3DH + OLED |
| **LIS3DH INT1**       | **D2**                 | GPIO4     | **Green**                 | Hardware motion wake (active-low) |
| **940 nm Emitter**    | **D6**                 | GPIO21    | **Blue**                  | TSAL6200 drive |
| **940 nm ADC**        | **A0**                 | GPIO2     | Green / leftover          | BPW34 → resistor → ADC |
| **Battery ADC**       | **A1**                 | GPIO3     | Yellow / leftover         | Voltage divider from LiPo |

> **⚠ Critical power rule**  
> Connect the LiPo **only** to the XIAO’s onboard battery pads / JST connector.  
> Do **not** solder raw LiPo+ to the 3V3 pin. A charged cell sits at ~4.2 V and will over-voltage the 3.3 V rail and all peripherals (MAX30102 / LIS3DH / OLED). Using the battery pads also keeps the onboard charge management working.

> **Color rule:** same color on both ends of every wire.  
> Power pair (Red/Black) on its own 2-pin JST-SH for easy battery disconnect.

---

## XIAO ESP32C3 Full Pinout (Arduino names)

```
                    USB-C
                 ┌─────────┐
           5V ───┤         ├─── D10 / MOSI
          GND ───┤         ├─── D9  / MISO
         3V3 ───┤         ├─── D8  / SCK
       A0/D0 ───┤         ├─── D7  / RX
       A1/D1 ───┤         ├─── D6  / TX   ← 940 nm Emitter
       A2/D2 ───┤         ├─── D5  / SCL  ← I²C Clock
       A3/D3 ───┤         ├─── D4  / SDA  ← I²C Data
                 └─────────┘

  Battery pads / JST on the underside (or board edge) — use these for LiPo+
```

| Arduino | GPIO  | Default / Project use          |
|---------|-------|--------------------------------|
| A0 / D0 | GPIO2 | **940 nm ADC**                 |
| A1 / D1 | GPIO3 | **Battery ADC**                |
| A2 / D2 | GPIO4 | **LIS3DH INT1**                |
| A3 / D3 | GPIO5 | Free                           |
| D4      | GPIO6 | **SDA** (I²C)                  |
| D5      | GPIO7 | **SCL** (I²C)                  |
| D6      | GPIO21| **940 nm Emitter** (also TX)   |
| D7      | GPIO20| RX (free if not using UART)    |
| D8      | GPIO8 | SCK                            |
| D9      | GPIO9 | MISO / Boot                    |
| D10     | GPIO10| MOSI                           |

---

## I²C Addresses (expected)

| Device     | Address     |
|------------|-------------|
| MAX30102   | `0x57`      |
| LIS3DH     | `0x18` or `0x19` |
| SSD1306    | `0x3C` (sometimes `0x3D`) |

Run the I²C scanner in `SETUP.md` before full assembly.

---

## Battery Voltage Divider (A1)

The divider senses the raw LiPo voltage (tapped from the battery side of the onboard charge circuit):

```
LiPo+ ── 100 kΩ ──┬── A1
                  │
                100 kΩ
                  │
                 GND
```

`BATTERY_SCALE = 2.0` in firmware (adjust after multimeter calibration).

---

## Quick Soldering Order (MCU side)

1. **Red**  → **Onboard battery pads / JST** (LiPo+) — **never** the 3V3 header pin
2. **Black** → GND
3. **White** → D4 (SDA)
4. **Yellow** → D5 (SCL)
5. **Green** → D2 (INT1)
6. **Blue**  → D6 (940 nm emitter)
7. Remaining colors → A0 (940 nm) and A1 (battery sense)

Leave a small service loop and secure JST-SH shells later so flexing the armband does not stress the solder joints.

---

*Part of [BGM](https://github.com/Fryrocket/BGM) · Firmware: [armband-ppg-940nm](https://github.com/Fryrocket/armband-ppg-940nm)*
