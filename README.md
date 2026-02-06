# FBTFT Driver for KeDei 6.2 TFT Display

Framebuffer driver for the KeDei 6.2 SPI TFT display (480×320) with R61581
LCD controller and 74HC595 shift-register CPLD.  Runs on Raspberry Pi 3
Model B with kernel 6.12+.

> **First time?** Read [PITFALLS.md](PITFALLS.md) — it documents every
> problem and fix encountered during development.

## Prerequisites

Build natively on the Raspberry Pi to avoid vermagic mismatches:

```bash
sudo apt update
sudo apt install raspberrypi-kernel-headers build-essential device-tree-compiler
```

## Build

```bash
make
```

This builds two kernel modules:
- `fbtft.ko` — the core FBTFT framebuffer subsystem
- `fb_kedei62.ko` — the KeDei 6.2 display driver

## Install

### Kernel Modules

```bash
sudo ./install_ko.sh
```

### Device Tree Overlay

```bash
sudo ./install_dtb.sh
```

Then add the following lines to `/boot/firmware/config.txt` (Bookworm) or
`/boot/config.txt` (older OS versions):

```
dtparam=spi=on
dtoverlay=kedei
```

## Reboot

```bash
sudo reboot
```

After reboot, the display should be available as `/dev/fb1` (or `/dev/fb0` if no
HDMI is connected).

## Usage

Test the display:

```bash
# Fill screen with random pixels
cat /dev/urandom > /dev/fb1

# Display console on the TFT
con2fbmap 1 1
```

## DT Overlay Parameters

You can pass parameters via `config.txt`:

```
dtoverlay=kedei,speed=39000000,rotate=0,fps=20,debug=0
```

| Parameter | Default    | Description                          |
|-----------|------------|--------------------------------------|
| speed     | 39000000   | SPI clock frequency in Hz            |
| rotate    | 0          | Rotation: 0, 90, 180, 270           |
| fps       | 20         | Frames per second for deferred I/O   |
| debug     | 0          | Debug verbosity level (0-7)          |

## Touchscreen

The KeDei board has an ADS7846 touchscreen controller on SPI CE0, but GPIO 8
(CE0) is also used as the 74HC595 latch signal for the display.  The
touchscreen node is **disabled** in the DTS until a GPIO-sharing solution is
implemented.  See [PITFALLS.md § Touchscreen Conflicts](PITFALLS.md#14-touchscreen-conflicts)
for details.



