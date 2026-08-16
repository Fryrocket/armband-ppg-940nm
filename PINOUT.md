# Armband Pinout Reference Card

**Print this page** and keep it next to the soldering iron.

Target: **Seeed XIAO ESP32C3**  
Firmware: `firmware/Armband_Full.ino`  
Wire: 28 AWG silicone + JST-SH

**Remap 2026-08-16 (Claude ASKs 114/126):** GPIO2 left floating (strapping pin). BPW34 → D2, LIS3DH INT1 → D3, TSAL6200 → D10 + series R.

### Direct schematic (PDF download)
[https://files.seeedstudio.com/wiki/XIAO_WiFi/Resources/XIAO_ESP32C3_v1.3_SCH_260116.pdf](https://files.seeedstudio.com/wiki/XIAO_WiFi/Resources/XIAO_ESP32C3_v1.3_SCH_260116.pdf)

Wiki / pin map: [https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)

---

## Project Pin Map (Firmware Defines) — CORRECTED 2026-08-16

| Function              | Arduino Pin / Pad      | Chip GPIO | Wire Color (recommended) | Notes |
|-----------------------|------------------------|-----------|---------------------------|-------|
| **Battery +**         | **Onboard battery pads / JST** | —    | **Red**                   | LiPo+ **only** to XIAO battery input (never 3V3) |
| **GND**               | GND                    | —         | **Black**                 | Common ground |
| **I²C SDA**           | **D4**                 | GPIO6     | **White**                 | MAX30102 + LIS3DH + OLED |
| **I²C SCL**           | **D5**                 | GPIO7     | **Yellow**                | MAX30102 + LIS3DH + OLED |
| **LIS3DH INT1**       | **D3**                 | GPIO5     | **Green**                 | Hardware motion wake (active-low). Moved from D2. Still GPIO0–5 for deep-sleep wake. |
| **940 nm Emitter**    | **D10**                | GPIO10    | **Blue**                  | TSAL6200 drive + ~100 Ω series resistor. Moved from D6 to avoid UART0 TX boot pulses. |
| **940 nm ADC**        | **D2**                 | GPIO4     | **Spare (label ends)**    | BPW34 → load resistor → ADC. Moved from A0 (GPIO2 is a strapping pin — leave floating). |
| **Battery ADC**       | **A1**                 | GPIO3     | **Spare (label ends)**    | Voltage divider from LiPo |
| **GPIO2 (A0)**        | —                      | GPIO2     | —                         | **LEAVE UNCONNECTED**. Strapping pin must read high at reset. |

> **⚠ Critical power rule**  
> Connect the LiPo **only** to the XIAO’s onboard battery pads / JST connector.  
> All sensor modules (MAX30102, LIS3DH, OLED) are powered from the XIAO’s regulated **3V3** pin.  
> Do **not** feed raw LiPo (up to 4.2 V) into the modules — many breakouts tie I²C pull-ups to VIN and would put 4.2 V on the C3 pins (absolute max ≈ 3.6 V).

> **Color rule:** same color on both ends of every wire.  
> Power pair (Red/Black) on its own 2-pin JST-SH for easy battery disconnect.  
> Primary colors (Red/Black/White/Yellow/Green/Blue) are reserved as above — use any remaining spool color for D2/A1 and label both ends.

---

## XIAO ESP32C3 Full Pinout (Arduino names) — after remap

```
                    USB-C
                 ┌─────────┐
           5V ───┤         ├─── D10 / MOSI   ← 940 nm Emitter (+ series R)
          GND ───┤         ├─── D9  / MISO
         3V3 ───┤         ├─── D8  / SCK
       A0/D0 ───┤         ├─── D7  / RX     ← leave free / floating (strapping)
       A1/D1 ───┤         ├─── D6  / TX     ← free (was emitter)
       A2/D2 ───┤         ├─── D5  / SCL    ← I²C Clock
       A3/D3 ───┤         ├─── D4  / SDA    ← I²C Data
                 └─────────┘

  Battery pads / JST on the underside — use these for LiPo+
```

| Arduino | GPIO  | Project use (after 2026-08-16 remap) |
|---------|-------|--------------------------------------|
| A0 / D0 | GPIO2 | **LEAVE FLOATING** (strapping pin)  |
| A1 / D1 | GPIO3 | **Battery ADC**                      |
| A2 / D2 | GPIO4 | **940 nm ADC (BPW34)**               |
| A3 / D3 | GPIO5 | **LIS3DH INT1**                      |
| D4      | GPIO6 | **SDA** (I²C)                        |
| D5      | GPIO7 | **SCL** (I²C)                        |
| D6      | GPIO21| Free (was emitter)                   |
| D7      | GPIO20| Free                                 |
| D8      | GPIO8 | Free                                 |
| D9      | GPIO9 | Free / Boot                          |
| D10     | GPIO10| **940 nm Emitter** (+ ~100 Ω)        |

---

## I²C Addresses (expected)

| Device     | Address     |
|------------|-------------|
| MAX30102   | `0x57`      |
| LIS3DH     | `0x18` or `0x19` |
| SSD1306    | `0x3C` (sometimes `0x3D`) |

**LIS3DH CS** must be tied HIGH (to 3V3) to select I²C mode. Firmware auto-detects the address once CS is correct.

Run an I²C scanner before full assembly. Firmware stores the address that responds and uses it for INT1 config.
