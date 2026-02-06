# FBTFT Driver for KeDei 6.2 TFT Display

Tested on Raspberry Pi 3 Model B with kernel 6.12.

## Prerequisites

Install the kernel headers for your running kernel:

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
sudo mkdir -p /lib/modules/$(uname -r)/kernel/misc
sudo ./install_ko.sh
```

### Device Tree Overlay

```bash
sudo ./install_dtb.sh
```

Then add the following line to `/boot/config.txt` (or `/boot/firmware/config.txt`
on newer Raspberry Pi OS versions):

```
dtoverlay=kedei
```

### Enable SPI

Ensure SPI is enabled in `/boot/config.txt`:

```
dtparam=spi=on
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

You can pass parameters via `/boot/config.txt`:

```
dtoverlay=kedei,speed=39000000,rotate=0,fps=1,debug=0
```

| Parameter | Default    | Description                          |
|-----------|------------|--------------------------------------|
| speed     | 39000000   | SPI clock frequency in Hz            |
| rotate    | 0          | Rotation: 0, 90, 180, 270           |
| fps       | 1          | Frames per second for deferred I/O   |
| debug     | 0          | Debug verbosity level (0-7)          |

## Touchscreen

The overlay also configures the ADS7846 touchscreen controller on SPI CE0.
Install `ts_lib` for calibration:

```bash
sudo apt install libts-bin
sudo ts_calibrate
```



