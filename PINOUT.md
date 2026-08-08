# Armband Pinout Reference Card

**Print this page** and keep it next to the soldering iron.

Target: **Seeed XIAO ESP32C3**  
Firmware: `firmware/Armband_Full.ino`  
Wire: 28 AWG silicone + JST-SH

### Direct schematic (PDF download)
[https://files.seeedstudio.com/wiki/XIAO_WiFi/Resources/XIAO_ESP32C3_v1.3_SCH_260116.pdf](https://files.seeedstudio.com/wiki/XIAO_WiFi/Resources/XIAO_ESP32C3_v1.3_SCH_260116.pdf)

Wiki / pin map: [https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/)

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
| **940 nm ADC**        | **A0**                 | GPIO2     | **Spare (label ends)**    | BPW34 → resistor → ADC; do **not** reuse INT1 Green |
| **Battery ADC**       | **A1**                 | GPIO3     | **Spare (label ends)**    | Voltage divider from LiPo; do **not** reuse SCL Yellow |

> **⚠ Critical power rule**  
> Connect the LiPo **only** to the XIAO’s onboard battery pads / JST connector.  
> Do **not** solder raw LiPo+ to the 3V3 pin. A charged cell sits at ~4.2 V and will over-voltage the 3.3 V rail and all peripherals (MAX30102 / LIS3DH / OLED). Using the battery pads also keeps the onboard charge management working.

> **Color rule:** same color on both ends of every wire.  
> Power pair (Red/Black) on its own 2-pin JST-SH for easy battery disconnect.  
> Primary colors (Red/Black/White/Yellow/Green/Blue) are reserved as above — use any remaining spool color for A0/A1 and label both ends.

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
| D6      | GPIO21| **940 nm Emitter** — also UART0 TX. Serial **must** run over USB CDC (see SETUP.md / platformio.ini) or debug output is driven onto the emitter. |
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

Run the I²C scanner in `SETUP.md` before full assembly. Firmware stores the address that responds and uses it for INT1 config.
