# KeDei 6.2 — 3.5″ SPI TFT LCD Display Module — Reverse-Engineered Datasheet

> **Disclaimer**: KeDei has never published an official datasheet for this board.
> All information below was reverse-engineered through logic-analyser captures,
> kernel driver development, and iterative hardware testing on Raspberry Pi 3
> Model B.  It applies to the **version 6.2** revision of the board.

---

## Table of Contents

1.  [Overview](#1-overview)
2.  [Absolute Maximum Ratings](#2-absolute-maximum-ratings)
3.  [Display Panel Specifications](#3-display-panel-specifications)
4.  [LCD Controller — Renesas R61581](#4-lcd-controller--renesas-r61581)
5.  [Board Architecture & Block Diagram](#5-board-architecture--block-diagram)
6.  [74HC595 Shift Register Bus (CPLD)](#6-74hc595-shift-register-bus-cpld)
7.  [Pin Assignment (40-Pin RPi Header)](#7-pin-assignment-40-pin-rpi-header)
8.  [SPI Bus Configuration](#8-spi-bus-configuration)
9.  [Communication Protocol](#9-communication-protocol)
10. [Timing Diagrams](#10-timing-diagrams)
11. [Reset Sequence](#11-reset-sequence)
12. [LCD Initialisation Sequence](#12-lcd-initialisation-sequence)
13. [Rotation (Memory Access Control — Register 0x36)](#13-rotation)
14. [Pixel Data Transfer (RGB565)](#14-pixel-data-transfer-rgb565)
15. [Address Windowing](#15-address-windowing)
16. [Touchscreen — ADS7846](#16-touchscreen--ads7846)
17. [Power Supply](#17-power-supply)
18. [Mechanical Dimensions](#18-mechanical-dimensions)
19. [Device Tree Binding](#19-device-tree-binding)
20. [Kernel Driver Interface](#20-kernel-driver-interface)
21. [Performance Characteristics](#21-performance-characteristics)
22. [Known Errata & Quirks](#22-known-errata--quirks)
23. [Register Reference (R61581 Subset Used)](#23-register-reference)
24. [Revision History](#24-revision-history)

---

## 1. Overview

| Parameter         | Value                                      |
|-------------------|--------------------------------------------|
| Product           | KeDei 3.5″ TFT LCD for Raspberry Pi       |
| Board Revision    | v6.2                                       |
| LCD Controller    | Renesas R61581                             |
| Panel Resolution  | 480 × 320 pixels (native portrait)         |
| Colour Depth      | 16-bit RGB565 (65,536 colours)             |
| Interface         | SPI (via 3× 74HC595 shift-register CPLD)   |
| Touchscreen       | Resistive, TI ADS7846 controller           |
| Connector         | 40-pin header (Raspberry Pi HAT form factor)|
| Operating Voltage | 3.3 V logic, 5 V board power (from RPi)    |
| SPI Clock         | Up to 39 MHz tested                        |

---

## 2. Absolute Maximum Ratings

| Parameter            | Min  | Typ  | Max   | Unit |
|----------------------|------|------|-------|------|
| Supply Voltage (VCC) | 4.75 | 5.0  | 5.25  | V    |
| Logic Level (SPI)    | 0    | 3.3  | 3.3   | V    |
| SPI Clock Frequency  | —    | 39   | 39†   | MHz  |
| Operating Temp.      | 0    | 25   | 50‡   | °C   |
| Storage Temp.        | −20  | —    | 70‡   | °C   |

† Higher SPI clocks have not been tested; artefacts may appear above 39 MHz.
‡ Estimated; no official thermal specifications exist.

---

## 3. Display Panel Specifications

| Parameter              | Value                |
|------------------------|----------------------|
| Active Area            | ~49 mm × 73 mm (est.)|
| Panel Type             | TFT LCD, transmissive|
| Native Orientation     | Portrait (320 × 480) |
| Pixel Format           | RGB565 (16 bpp)      |
| Also Supports          | RGB666 (18 bpp) — not used by driver |
| Colour Order           | BGR (bit 3 of reg 0x36 = 1) |
| Backlight              | LED, always on (active-low control pin available) |
| Viewing Angle          | ~160° (typical for TN TFT) |
| Contrast Ratio         | ~400:1 (est.)        |
| Response Time          | ~25 ms (est.)        |

---

## 4. LCD Controller — Renesas R61581

The LCD panel is driven by a **Renesas R61581** (also known as R61581B0/B1)
TFT controller.  The R61581 features:

- Built-in 480 × 320 × 18-bit GRAM (Graphics RAM)
- Parallel 8/16/18-bit interface (directly to the panel)
- Configurable pixel format: 16-bit (RGB565) or 18-bit (RGB666)
- Hardware rotation via Memory Access Control (register 0x36)
- Programmable gamma correction (register 0xC8)
- Tearing-effect output (register 0x35)
- Internal oscillator (no external crystal required)
- Power-management modes: Normal, Idle, Sleep, Deep Standby
- VCOM voltage control for contrast adjustment (register 0xD1)

The R61581 does **not** have a direct SPI interface.  Communication is
via the board's 74HC595 shift-register bridge (see §6).

---

## 5. Board Architecture & Block Diagram

```
                        KeDei v6.2 Board
 ┌──────────────────────────────────────────────────────────────┐
 │                                                              │
 │  Raspberry Pi 40-Pin Header                                  │
 │  ═══════════════════════════                                 │
 │    │ SPI0 MOSI (GPIO 10) ─────────────┐                     │
 │    │ SPI0 SCLK (GPIO 11) ─────────────┤                     │
 │    │ SPI0 CE1  (GPIO  7) ─────────────┤  ┌──────────────┐   │
 │    │                                   ├──┤ 3× 74HC595   │   │
 │    │ SPI0 CE0  (GPIO  8) ─── RCLK ────┘  │ Shift Regs   │   │
 │    │                                      │ (24-bit      │   │
 │    │ GPIO 25 ──── PENIRQ (touch) ──┐      │  serial →    │   │
 │    │                               │      │  parallel)   │   │
 │    │ 3.3V, 5V, GND ───── Power     │      └──────┬───────┘   │
 │                                    │             │  8-bit     │
 │  ┌─────────────┐                   │             │  parallel  │
 │  │  ADS7846    │◄──────────────────┘             │  bus       │
 │  │ Touch Ctrl  │                                 ▼            │
 │  └──────┬──────┘                        ┌────────────────┐   │
 │         │                               │    R61581      │   │
 │         │ (directly on                  │  LCD Controller │   │
 │         │  touch panel)                 │   + TFT Panel   │   │
 │         │                               │   480 × 320    │   │
 │         ▼                               └────────────────┘   │
 │    Touch Panel                                               │
 │    (resistive overlay)                                       │
 └──────────────────────────────────────────────────────────────┘
```

**Key architectural insight**: The Raspberry Pi SPI bus does **not**
connect directly to the R61581 LCD controller.  Instead, 3× cascaded
74HC595 8-bit shift registers convert the SPI serial stream into a
24-bit parallel word.  Each 3-byte SPI transfer clocks 24 bits into the
shift registers, then a latch pulse on GPIO 8 (RCLK) transfers the
parallel data to the R61581's bus interface.

---

## 6. 74HC595 Shift Register Bus (CPLD)

### 6.1 Component: 74HC595

The **74HC595** is an 8-bit serial-in, parallel-out shift register with
an output register (latch).  Three are cascaded for 24-bit width.

| Pin     | Function                           |
|---------|------------------------------------|
| SER     | Serial data input (from MOSI)      |
| SRCLK   | Shift-register clock (from SCLK)   |
| RCLK    | Register clock / latch (GPIO 8)    |
| OE#     | Output enable (active-low, tied LOW)|
| QA–QH   | Parallel outputs (to R61581)       |
| QH'     | Serial out (daisy-chain to next)   |

### 6.2 Bus Timing

The SPI framework handles MOSI and SCLK.  The driver is only responsible
for the RCLK latch signal:

```
           ┌─────────────────────────────┐
           │  3-byte SPI transaction     │
           │  (24 bits shift in)         │
    ───────┤                             ├──────────
           │                             │
RCLK  ─────┘                             └──────────
      HIGH during SPI transfer     LOW after transfer
```

The latch pulse is **active-HIGH**: RCLK goes HIGH before `spi_sync()`
and returns LOW after.  This rising→falling edge captures the 24-bit
shift register contents into the output registers, presenting them on
the parallel bus to the R61581.

### 6.3 24-Bit Word Format

The 24-bit parallel word sent to the R61581 is encoded into 3 SPI bytes:

| Byte 0 (first shifted in) | Byte 1 | Byte 2 (last shifted in) |
|---------------------------|--------|--------------------------|
| Packet type identifier    | Data high / zero | Data low / command |

The 74HC595 chain maps these 24 bits to control and data lines on the
R61581's parallel interface.  The exact pin mapping is undocumented, but
the protocol layer is described in §9.

---

## 7. Pin Assignment (40-Pin RPi Header)

### 7.1 Pins Used by the Display

| RPi Header Pin | BCM GPIO | Function              | Direction | Active |
|----------------|----------|-----------------------|-----------|--------|
| 19             | GPIO 10  | SPI0 MOSI             | Output    | —      |
| 23             | GPIO 11  | SPI0 SCLK             | Output    | —      |
| 26             | GPIO  7  | SPI0 CE1 (chip select)| Output    | LOW    |
| 24             | GPIO  8  | 74HC595 RCLK (latch)  | Output    | HIGH   |

### 7.2 Pins Used by the Touchscreen (ADS7846)

| RPi Header Pin | BCM GPIO | Function        | Direction | Active |
|----------------|----------|-----------------|-----------|--------|
| 19             | GPIO 10  | SPI0 MOSI       | Output    | —      |
| 21             | GPIO  9  | SPI0 MISO       | Input     | —      |
| 23             | GPIO 11  | SPI0 SCLK       | Output    | —      |
| 24             | GPIO  8  | SPI0 CE0 (touch)| Output    | LOW    |
| 22             | GPIO 25  | PENIRQ          | Input     | LOW    |

### 7.3 GPIO Conflict

GPIO 8 serves dual purpose: the display uses it as the 74HC595 RCLK
latch, while the touchscreen uses it as SPI CE0.  **These cannot operate
simultaneously** in the current board revision.  The display driver must
have exclusive control of GPIO 8.

### 7.4 Power Pins

| RPi Header Pin | Function                 |
|----------------|--------------------------|
| 1              | 3.3 V (logic supply)     |
| 2 or 4         | 5 V (backlight, display) |
| 6, 9, etc.     | GND                      |

---

## 8. SPI Bus Configuration

| Parameter             | Value                                  |
|-----------------------|----------------------------------------|
| SPI Controller        | SPI0 (`/dev/spidev0.1`)                |
| Chip Select           | CE1 (GPIO 7)                           |
| SPI Mode              | Mode 0 (CPOL=0, CPHA=0)               |
| Clock Frequency       | 39,000,000 Hz (39 MHz) max tested      |
| Bit Order             | MSB first                              |
| Word Size             | 8 bits                                 |
| Chip Select Polarity  | **Active-LOW** (critical — see §22.1)  |
| Full-Duplex           | No (MISO not used for display)         |

### 8.1 SPI Controller CS-GPIO Override

The SPI controller's default chip-select assignments must be overridden
in the Device Tree to free GPIO 8 from the SPI core:

```dts
cs-gpios = <0>,              /* CE0: not managed by SPI core */
           <&gpio 7 1>;      /* CE1: GPIO 7, active-low      */
```

The `<0>` (null phandle) for CE0 tells the SPI framework to skip
managing GPIO 8, allowing the display driver to control it directly.

---

## 9. Communication Protocol

All communication with the R61581 is tunnelled through the 74HC595
shift-register bridge.  Each transaction is a 3-byte (or 4-byte for
reset) SPI transfer, framed by an RCLK latch pulse.

### 9.1 Packet Types

| Type        | Byte 0 | Byte 1       | Byte 2       | Total |
|-------------|--------|--------------|--------------|-------|
| **Command** | `0x11` | `0x00`       | command byte | 3     |
| **Data**    | `0x15` | data high    | data low     | 3     |
| **Pixel**   | `0x15` | pixel high   | pixel low    | 3     |
| **Reset**   | `0x00` | assert flag  | `0x00`       | 4     |

### 9.2 Command Packet

Sends a register address / command to the R61581.

```
SPI bytes: [0x11] [0x00] [CMD]
```

- `0x11` = command prefix identifier
- `0x00` = padding (high byte unused for commands)
- `CMD`  = R61581 register address (e.g., `0x36` for Memory Access Control)

### 9.3 Data Packet

Sends a parameter byte following a command, or a pixel data word.

```
SPI bytes: [0x15] [HIGH] [LOW]
```

- `0x15` = data prefix identifier
- `HIGH` = high byte of 16-bit data (or `0x00` for 8-bit parameters)
- `LOW`  = low byte of data (or the parameter value)

For single-byte register parameters:

```
SPI bytes: [0x15] [0x00] [PARAM]
```

### 9.4 Pixel Data Packet

RGB565 pixels are sent as data packets.  The framebuffer stores pixels
in **little-endian** byte order, but the R61581 expects **big-endian**,
so the driver byte-swaps each pair:

```
Framebuffer (little-endian):  [low_byte] [high_byte]
SPI packet  (big-endian):     [0x15] [high_byte] [low_byte]
```

### 9.5 Reset Packet

The reset sequence uses a 4-byte packet (unique among packet types):

```
Assert reset:   [0x00] [0x01] [0x00] [0x00]
Release reset:  [0x00] [0x00] [0x00] [0x00]
```

### 9.6 RCLK Latch Framing

Every packet (regardless of type) is wrapped in an RCLK latch:

```c
gpiod_set_value(cs_latch, 1);  /* RCLK HIGH — begin */
spi_sync(spi, &msg);           /* clock data into shift registers */
gpiod_set_value(cs_latch, 0);  /* RCLK LOW  — latch data onto bus */
```

The SPI framework separately manages GPIO 7 (CE1) as the SPI chip-select
(active-low) around `spi_sync()`.  Thus, during each packet, two GPIOs
toggle:

```
GPIO 7 (CE1):   ‾‾‾‾\__________/‾‾‾‾  (active-low, SPI framework)
GPIO 8 (RCLK):  ____/‾‾‾‾‾‾‾‾‾‾\____  (active-high, driver code)
SPI data:       ----<24 bits clocked>--
```

---

## 10. Timing Diagrams

### 10.1 Single Command Transaction

```
Time ─────────────────────────────────────────────────────────►

CE1 (GPIO 7)   ‾‾‾‾‾‾\______________________________________/‾‾‾‾
               active-low chip-select

RCLK (GPIO 8)  _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_____
               latch pulse (active-high)

SCLK           _______⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬⌐¬_____
               24 clock pulses (3 bytes × 8 bits)

MOSI           _______[  0x11  ][  0x00  ][  CMD   ]_________
               24 bits: prefix, padding, command
```

### 10.2 Command + Data Sequence (e.g., set register 0x36 to 0xAA)

```
    Command packet              Data packet
├──────────────────────────├──────────────────────────┤

CE1    ‾\_________________/‾\_________________/‾
RCLK   _/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\_
MOSI    [0x11][0x00][0x36]   [0x15][0x00][0xAA]
```

Each packet is an independent SPI transaction with its own CE1/RCLK
framing.

---

## 11. Reset Sequence

The R61581 requires a hardware reset before initialisation.  The KeDei
board routes the reset signal through the 74HC595 bus (there is no
dedicated reset GPIO line).

### 11.1 Protocol

```
Step 1: Assert reset    → send [0x00, 0x01, 0x00, 0x00], wait 50 ms
Step 2: Release reset   → send [0x00, 0x00, 0x00, 0x00], wait 100 ms
Step 3: Assert again    → send [0x00, 0x01, 0x00, 0x00], wait 50 ms
```

The double-pulse pattern (assert → release → assert) ensures a clean
reset even if the shift registers contain stale data from a previous
session.

### 11.2 Timing

```
         50ms      100ms      50ms
        ├────┤   ├──────┤   ├────┤
RESET:  ‾‾‾‾‾\__/‾‾‾‾‾‾‾\__/‾‾‾‾‾
        assert  release   assert
```

Total reset time: ~200 ms minimum.

---

## 12. LCD Initialisation Sequence

After reset, the R61581 must be programmed with a specific register
sequence.  The values below were extracted from the working KeDei driver
and annotated with R61581 datasheet register names.

### 12.1 Synchronisation Preamble

```
CMD  0x00              NOP
     wait 10 ms
CMD  0xFF              NOP (undocumented sync)
CMD  0xFF
     wait 10 ms
CMD  0xFF × 4          flush 74HC595 pipeline
     wait 15 ms
```

### 12.2 Wake-Up

```
CMD  0x11              Sleep Out
     wait 150 ms
```

### 12.3 Configuration Registers

| Step | Command | Parameters (hex)                     | Register Name                        |
|------|---------|--------------------------------------|--------------------------------------|
| 1    | `0xB0`  | `00`                                 | Manufacturer Command Access Protect  |
| 2    | `0xB3`  | `02 00 00 00`                        | Frame Memory Access & Interface      |
| 3    | `0xB9`  | `01 00 0F 0F`                        | Internal Oscillator Setting          |
| 4    | `0xC0`  | `13 3B 00 02 00 01 00 43`            | Panel Driving Setting                |
| 5    | `0xC1`  | `08 0F 08 08`                        | Display Timing (Normal Mode)         |
| 6    | `0xC4`  | `11 07 03 04`                        | Source/Gate Timing                   |
| 7    | `0xC6`  | `00`                                 | Interface Control                    |
| 8    | `0xC8`  | `03 03 13 5C 03 07 14 08`            | Gamma Setting (positive curve)       |
|      |         | `00 21 08 14 07 53 0C 13`            | Gamma Setting (negative curve)       |
|      |         | `03 03 21 00`                        | Gamma Setting (continued)            |
| 9    | `0x35`  | `00`                                 | Tearing Effect Line ON               |
| 10   | `0x36`  | `60`                                 | Memory Access Control (initial)      |
| 11   | `0x3A`  | `55`                                 | Pixel Format: 16-bit RGB565          |
| 12   | `0x44`  | `00 01`                              | Set Tear Scanline                    |
| 13   | `0xD0`  | `07 07 1D 03`                        | Power Setting                        |
| 14   | `0xD1`  | `03 30 10`                           | VCOM Control                         |
| 15   | `0xD2`  | `03 14 04`                           | Power Setting (Normal Mode)          |

### 12.4 Display ON

```
CMD  0x29              Display ON
     wait 30 ms
```

### 12.5 Initial Address Window (Full Screen)

```
CMD  0x2A  DATA: 00 00 01 3F    Column: 0–319
CMD  0x2B  DATA: 00 00 01 E0    Page:   0–480
CMD  0xB4  DATA: 00              Frame Inversion: normal
CMD  0x2C                        Memory Write — ready for pixels
     wait 10 ms
```

### 12.6 Rotation Selection

After the full init, the driver re-programs register 0x36 with the
rotation value selected via Device Tree (see §13).

---

## 13. Rotation (Memory Access Control — Register 0x36) {#13-rotation}

Register 0x36 controls how the R61581 maps pixel addresses to physical
display lines and columns.

### 13.1 Bit Layout

```
Bit:  7    6    5    4    3    2    1    0
      MY   MX   MV   ML   BGR  MH   —    —
```

| Bit | Name | Description                                   |
|-----|------|-----------------------------------------------|
| 7   | MY   | Row address order (0=top→bottom, 1=bottom→top) |
| 6   | MX   | Column address order (0=left→right, 1=right→left)|
| 5   | MV   | Row/column exchange (0=normal, 1=swapped)     |
| 4   | ML   | Vertical refresh order (not used)             |
| 3   | BGR  | Colour order (0=RGB, 1=BGR) — **always 1**    |
| 2   | MH   | Horizontal refresh order (not used)           |
| 1–0 | —    | Reserved, set to `10` (binary) per vendor init|

### 13.2 Rotation Values

The driver's native panel is 320 columns × 480 rows (portrait).
fbtft-core creates a portrait framebuffer (320×480) for 0°/180° and
a landscape framebuffer (480×320) for 90°/270°.  The MV bit must
match the framebuffer orientation.

| Rotation | Degrees | Reg 0x36 | MY | MX | MV | FB Size  | Connector Position |
|----------|---------|----------|----|----|----|----------|--------------------|
| 0        | 0°      | `0x0A`   | 0  | 0  | 0  | 320×480  | Bottom             |
| 1        | 90°     | `0x6A`   | 0  | 1  | 1  | 480×320  | Left               |
| 2        | 180°    | `0xCA`   | 1  | 1  | 0  | 320×480  | Top                |
| 3        | 270°    | `0xAA`   | 1  | 0  | 1  | 480×320  | Right **(default)**|

All values include BGR=1 (bit 3) and bit 1=1, giving a base of `0x0A`.

### 13.3 Configuration

Set rotation via Device Tree overlay parameter:

```
dtoverlay=kedei,rotate=270
```

Valid values: `0`, `90`, `180`, `270`.

---

## 14. Pixel Data Transfer (RGB565)

### 14.1 Pixel Format

| Bit Position | 15–11 | 10–5 | 4–0  |
|--------------|-------|------|------|
| Colour       | Red   | Green| Blue |
| Bits         | 5     | 6    | 5    |

Note: Despite the name "RGB565", the display uses BGR ordering
internally (register 0x36 bit 3 = 1).  The driver accounts for this,
so the Linux framebuffer presents standard RGB565 to user-space.

### 14.2 Byte Order

```
Linux framebuffer (little-endian):
  Address N:   low byte  (GGGBBBBB)
  Address N+1: high byte (RRRRRGGG)

SPI packet to R61581 (big-endian):
  [0x15] [high byte: RRRRRGGG] [low byte: GGGBBBBB]
```

### 14.3 Full-Screen Transfer

For a complete 480×320 frame at RGB565:

```
Total pixels:      480 × 320 = 153,600
Bytes per pixel:   2 (RGB565)
Frame size:        307,200 bytes
SPI bytes/pixel:   3 (with 0x15 prefix)
SPI bytes/frame:   460,800
SPI transactions:  153,600 (one per pixel)
```

Each pixel requires a separate 3-byte SPI transaction with RCLK latch,
making full-frame updates relatively slow compared to displays with
direct SPI interfaces.

---

## 15. Address Windowing

Before writing pixels, the R61581's GRAM write window must be set using
the Column Address Set (0x2A) and Page Address Set (0x2B) registers,
followed by a Memory Write (0x2C) command.

### 15.1 Protocol

```
CMD  0x2A                    Column Address Set
DATA [xs_hi] [xs_lo]         Start column (0-based)
DATA [xe_hi] [xe_lo]         End column (inclusive)

CMD  0x2B                    Page Address Set
DATA [ys_hi] [ys_lo]         Start row (0-based)
DATA [ye_hi] [ye_lo]         End row (inclusive)

CMD  0x2C                    Memory Write — subsequent data goes to GRAM
```

### 15.2 Coordinate Ranges

| Rotation | Column Range | Row Range |
|----------|-------------|-----------|
| 0°, 180° (portrait)  | 0–319 | 0–479 |
| 90°, 270° (landscape) | 0–479 | 0–319 |

fbtft-core calls `set_addr_win()` before each `write_vmem()` transfer.

---

## 16. Touchscreen — ADS7846

The KeDei 6.2 board includes a **TI ADS7846** resistive touchscreen
controller.

### 16.1 Specifications

| Parameter              | Value              |
|------------------------|--------------------|
| Controller             | TI ADS7846         |
| Touch Type             | 4-wire resistive   |
| SPI Bus                | SPI0               |
| Chip Select            | CE0 (GPIO 8)       |
| Interrupt (PENIRQ)     | GPIO 25            |
| SPI Max Frequency      | 2 MHz              |
| Resolution             | 12-bit ADC         |
| X-Plate Resistance     | ~60 Ω              |
| Pressure Max           | 255 (configurable) |

### 16.2 Hardware Conflict

**The touchscreen cannot be used simultaneously with the display** in
board revision 6.2.  GPIO 8 is shared between:

- **Display**: 74HC595 RCLK latch signal (toggled on every SPI packet)
- **Touchscreen**: SPI CE0 chip-select

Possible workarounds (none implemented):
1. External GPIO multiplexer between CE0 and RCLK
2. Re-routing CE0 to a different GPIO via hardware modification
3. Software time-division multiplexing (complex, latency-sensitive)

### 16.3 DTS Node (Disabled)

```dts
/* Commented out due to GPIO 8 conflict */
kedei62-ts@0 {
    compatible = "ti,ads7846";
    reg = <0>;
    spi-max-frequency = <2000000>;
    interrupts = <25 2>;
    interrupt-parent = <&gpio>;
    pendown-gpio = <&gpio 25 0>;
    ti,x-plate-ohms = /bits/ 16 <60>;
    ti,pressure-max = /bits/ 16 <255>;
};
```

---

## 17. Power Supply

The board is powered entirely from the Raspberry Pi's 40-pin header:

| Rail  | Source         | Consumers                            |
|-------|---------------|--------------------------------------|
| 5 V   | RPi Pin 2/4   | Backlight LEDs, voltage regulators   |
| 3.3 V | RPi Pin 1/17  | 74HC595 logic, ADS7846, R61581 I/O   |

No external power supply is needed.  Current consumption (estimated):

| Component       | Typical Current |
|-----------------|----------------|
| LCD backlight   | ~80 mA (5 V)   |
| 74HC595 × 3     | ~3 mA (3.3 V)  |
| R61581           | ~10 mA (3.3 V) |
| ADS7846          | ~1 mA (3.3 V)  |
| **Total**       | **~95 mA**      |

---

## 18. Mechanical Dimensions

| Parameter            | Value (estimated)        |
|----------------------|--------------------------|
| Board Dimensions     | ~56 mm × 85 mm           |
| Display Active Area  | ~49 mm × 73 mm           |
| Connector            | 2×20 pin female header   |
| Mounting             | Press-fit onto RPi GPIO  |
| PCB Layers           | 2 (estimated)            |
| Weight               | ~45 g (estimated)        |

The board sits directly on top of the Raspberry Pi, covering the
entire GPIO header.  No additional standoffs are required but
recommended for secure mounting.

---

## 19. Device Tree Binding

### 19.1 Compatible String

```
compatible = "kedei62";
```

### 19.2 Required Properties

| Property            | Type    | Description                            |
|---------------------|---------|----------------------------------------|
| `compatible`        | string  | Must be `"kedei62"`                    |
| `reg`               | u32     | SPI chip-select index (1 for CE1)      |
| `spi-max-frequency` | u32     | SPI clock in Hz                        |
| `buswidth`          | u32     | Bus width in bits (always 8)           |
| `regwidth`          | u32     | Register width in bits (always 8)      |
| `cs-gpios`          | phandle | 74HC595 latch GPIO (GPIO 8, active-high)|

### 19.3 Optional Properties

| Property   | Type   | Default | Description                      |
|------------|--------|---------|----------------------------------|
| `rotate`   | u32    | 0       | Rotation in degrees (0/90/180/270)|
| `fps`      | u32    | 1       | Framebuffer refresh rate          |
| `bgr`      | flag   | —       | Set if panel uses BGR colour order|
| `debug`    | u32    | 0       | Debug verbosity level (0–7)       |

### 19.4 Overlay Parameters (`config.txt`)

| Parameter | DTS Property         | Example                     |
|-----------|----------------------|-----------------------------|
| `speed`   | `spi-max-frequency`  | `dtoverlay=kedei,speed=16000000` |
| `rotate`  | `rotate`             | `dtoverlay=kedei,rotate=90`      |
| `fps`     | `fps`                | `dtoverlay=kedei,fps=30`         |
| `debug`   | `debug`              | `dtoverlay=kedei,debug=7`        |

### 19.5 Complete DTS Example

See [kedei.dts](kedei.dts) in the source tree.

---

## 20. Kernel Driver Interface

### 20.1 Modules

| Module         | Source Files                                      | Role                       |
|----------------|---------------------------------------------------|----------------------------|
| `fbtft.ko`     | fbtft-core.c, fbtft-bus.c, fbtft-io.c, fbtft-sysfs.c | Core FBTFT framework   |
| `fb_kedei62.ko`| fb_kedei62.c                                      | KeDei 6.2 display driver   |

### 20.2 Driver Operations

| Callback         | Function              | Description                         |
|------------------|-----------------------|-------------------------------------|
| `.write`         | `kedei_write()`       | 3-byte SPI + RCLK latch             |
| `.write_vmem`    | `write_vmem()`        | Byte-swap RGB565 → big-endian       |
| `.set_addr_win`  | `set_addr_win()`      | Program column/page address          |
| `.reset`         | `reset()`             | Double-pulse reset via 74HC595       |
| `.init_display`  | `init_display()`      | Full R61581 initialisation + rotation|
| `.request_gpios` | `request_gpios()`     | No-op (DT path overrides)           |
| `.verify_gpios`  | `verify_gpios()`      | No-op (DT path overrides)           |

### 20.3 Framebuffer Device

| Property        | Value                  |
|-----------------|------------------------|
| Device Node     | `/dev/fb1` (typically) |
| Driver Name     | `fb_kedei62`           |
| Bits Per Pixel  | 16 (RGB565)            |
| Memory Model    | Deferred I/O           |
| Update Trigger  | Page-fault on `mmap()` |

### 20.4 Sysfs Interface

```
/sys/class/graphics/fb1/
├── name                    # "fb_kedei62"
├── rotate                  # software rotation (fbcon)
├── device/
│   └── debug               # debug level (writable)
└── ...
```

---

## 21. Performance Characteristics

### 21.1 Throughput

| Metric                    | Value                         |
|---------------------------|-------------------------------|
| SPI Clock                 | 39 MHz                        |
| Bytes per pixel (wire)    | 3 (with 0x15 prefix)          |
| Effective pixel rate      | 39 MHz / 24 bits = 1.625 Mpx/s|
| Full frame pixels         | 153,600 (480×320)             |
| Theoretical full-frame time| 94.5 ms (~10.6 fps)          |
| Practical max fps         | ~8–10 fps (full-screen redraw)|
| Partial update fps        | Up to 20+ fps (dirty regions) |

### 21.2 Latency

| Stage                   | Typical Latency |
|-------------------------|-----------------|
| Deferred I/O timer      | 1/fps seconds (50 ms at 20 fps) |
| RCLK latch setup        | < 1 μs          |
| SPI transaction (3B)    | ~0.6 μs at 39 MHz |
| Full-screen SPI transfer| ~95 ms           |
| **Total (worst case)**  | ~145 ms          |

### 21.3 CPU Impact

At 20 fps with active screen changes, expect 5–15% CPU utilisation on
a Raspberry Pi 3 Model B (ARMv8 Cortex-A53 @ 1.2 GHz) due to the
per-pixel SPI overhead.

---

## 22. Known Errata & Quirks

### 22.1 CE1 Polarity Warning (Cosmetic)

The SPI core prints:

```
spi spi0.1: GPIO handle specifies active low - ignored
```

This warning is **misleading** — the GPIO flag IS respected.  Do NOT add
`spi-cs-high` to the device node to silence it; doing so inverts the
chip-select polarity and blanks the display.

### 22.2 Per-Pixel SPI Transactions

The 3-byte protocol forces one SPI transaction per pixel, severely
limiting throughput.  A direct-SPI display (e.g., ILI9341) can stream
continuous pixel data without per-pixel framing, achieving much higher
effective bandwidth.

### 22.3 No Hardware Backlight Control

The backlight LEDs are connected directly to 5 V with no PWM or
enable pin exposed to the Raspberry Pi.  The backlight is always on
when the board is powered.

### 22.4 Reset via Shift Registers

There is no dedicated hardware reset GPIO.  Reset is performed by
sending a special 4-byte packet through the same 74HC595 bus used for
commands and data (see §11).

### 22.5 FPS=1 Compiled Default

The driver's compiled default is `fps=1` (one frame per second) as a
safe minimum.  This **must** be overridden via Device Tree (`fps=20`)
or the display will appear unresponsive.

### 22.6 MISO Not Routed to Display

The R61581 supports reading registers, but the 74HC595 bus is
unidirectional (serial-in → parallel-out only).  There is no read-back
path from the LCD controller to the Raspberry Pi.

### 22.7 No FBTFT DMA Support

The FBTFT subsystem's `write_vmem()` sends one pixel per `spi_sync()`
call rather than using DMA scatter-gather lists.  This is inherent to
the 3-byte framing protocol and cannot be easily optimised.

---

## 23. Register Reference (R61581 Subset Used)

Complete list of R61581 registers programmed during initialisation:

| Register | Name                                | Bytes | Values (hex)                                  |
|----------|-------------------------------------|-------|-----------------------------------------------|
| `0x00`   | NOP                                 | 0     | —                                             |
| `0x11`   | Sleep Out                           | 0     | —                                             |
| `0x29`   | Display ON                          | 0     | —                                             |
| `0x2A`   | Column Address Set                  | 4     | `00 00 01 3F` (0–319)                         |
| `0x2B`   | Page Address Set                    | 4     | `00 00 01 E0` (0–479)                         |
| `0x2C`   | Memory Write                        | 0     | —                                             |
| `0x35`   | Tearing Effect Line ON              | 1     | `00`                                          |
| `0x36`   | Memory Access Control               | 1     | rotation-dependent (see §13.2)                |
| `0x3A`   | Pixel Format                        | 1     | `55` (16-bit for both RGB and MCU interface)  |
| `0x44`   | Set Tear Scanline                   | 2     | `00 01`                                       |
| `0xB0`   | Manufacturer Command Access Protect | 1     | `00` (unlock)                                 |
| `0xB3`   | Frame Memory Access & Interface     | 4     | `02 00 00 00`                                 |
| `0xB4`   | Display Mode / Frame Inversion      | 1     | `00`                                          |
| `0xB9`   | Internal Oscillator Setting         | 4     | `01 00 0F 0F`                                 |
| `0xC0`   | Panel Driving Setting               | 8     | `13 3B 00 02 00 01 00 43`                     |
| `0xC1`   | Display Timing (Normal Mode)        | 4     | `08 0F 08 08`                                 |
| `0xC4`   | Source/Gate Timing                   | 4     | `11 07 03 04`                                 |
| `0xC6`   | Interface Control                   | 1     | `00`                                          |
| `0xC8`   | Gamma Setting                       | 20    | `03 03 13 5C 03 07 14 08 00 21 08 14 07 53 0C 13 03 03 21 00` |
| `0xD0`   | Power Setting                       | 4     | `07 07 1D 03`                                 |
| `0xD1`   | VCOM Control                        | 3     | `03 30 10`                                    |
| `0xD2`   | Power Setting (Normal Mode)         | 3     | `03 14 04`                                    |
| `0xFF`   | NOP (synchronisation)               | 0     | —                                             |

---

## 24. Revision History

| Date       | Author          | Description                                           |
|------------|-----------------|-------------------------------------------------------|
| 2019       | Tong Zhang      | Original driver for older kernels                     |
| 2025-02    | Reverse-eng.    | Full reverse-engineering of hardware architecture     |
| 2025-02    | Reverse-eng.    | 74HC595 latch protocol discovery (GPIO 8 = RCLK)     |
| 2025-02    | Reverse-eng.    | CE1 active-low polarity confirmed                     |
| 2025-02    | Reverse-eng.    | R61581 rotation table corrected (MV bit alignment)    |
| 2025-02    | Reverse-eng.    | Complete register init sequence documented            |
| 2025-02    | Modernisation   | Driver ported to Linux kernel 6.12+ (RPi OS Bookworm) |

---

*This document was produced through iterative hardware testing and
kernel driver development.  No official KeDei documentation was
available.  Use at your own risk.*
