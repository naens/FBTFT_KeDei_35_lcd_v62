# KeDei 6.2 Display — Usage Guide

Practical instructions for using the KeDei 6.2 TFT display on Raspberry Pi
after the driver has been installed and the display shows pixels.

---

## Table of Contents

1. [Identifying the Framebuffer Device](#1-identifying-the-framebuffer-device)
2. [Quick Display Test](#2-quick-display-test)
3. [Changing Orientation (Rotation)](#3-changing-orientation)
4. [Using It as a TTY Console](#4-using-it-as-a-tty-console)
5. [Booting Directly into the TFT Console](#5-booting-into-the-tft-console)
6. [Running a Desktop Environment on the TFT](#6-desktop-environment-on-the-tft)
7. [Displaying Images and Text](#7-displaying-images-and-text)
8. [Adjusting Frame Rate](#8-adjusting-frame-rate)
9. [Adjusting SPI Speed](#9-adjusting-spi-speed)
10. [Debugging and Diagnostics](#10-debugging-and-diagnostics)
11. [Unloading the Driver](#11-unloading-the-driver)
12. [Boot Configuration Reference](#12-boot-configuration-reference)

---

## 1. Identifying the Framebuffer Device

After boot, the display registers as a Linux framebuffer — typically
`/dev/fb1` (since `/dev/fb0` is usually HDMI).  If HDMI is not connected,
it may be `/dev/fb0`.

```bash
# List all framebuffers
ls -l /dev/fb*

# See which driver owns each framebuffer
cat /proc/fb
# Example output:
#   0 BCM2708 FB        ← HDMI
#   1 fb_kedei62         ← KeDei TFT
```

The examples below assume the TFT is `/dev/fb1`.  Adjust if yours differs.

---

## 2. Quick Display Test

```bash
# Fill the screen with random coloured pixels
cat /dev/urandom > /dev/fb1

# Clear the screen (fill with black)
dd if=/dev/zero of=/dev/fb1

# Fill with a solid colour (white = 0xFF bytes in RGB565)
tr '\0' '\377' < /dev/zero | dd of=/dev/fb1 bs=307200 count=1
```

---

## 3. Changing Orientation

The display supports four rotations.  The `rotate` parameter is set in
the Device Tree overlay and takes effect at driver load time.

### Method A — Edit `config.txt` (persistent, requires reboot)

In `/boot/firmware/config.txt` (or `/boot/config.txt` on older OS):

```
dtoverlay=kedei,rotate=0
```

| Value | Orientation | Resolution on screen |
|-------|-------------|---------------------|
| `0`   | 0° — Portrait, connector at bottom | 320 × 480 |
| `90`  | 90° — Landscape, connector on left | 480 × 320 |
| `180` | 180° — Portrait, connector at top | 320 × 480 |
| `270` | 270° — Landscape, connector on right **(default)** | 480 × 320 |

The driver programs the R61581's Memory Access Control register (0x36) to
match the DT value, so both framebuffer geometry and LCD scan direction
stay in sync for all four orientations.

### Method B — Rotate the console in software (no reboot)

If you just want to rotate what's shown on a console that's already
mapped to the TFT:

```bash
# Rotate the fbcon output (0=normal, 1=CW, 2=180°, 3=CCW)
echo 1 | sudo tee /sys/class/graphics/fb1/rotate
```

This does not change the hardware scan direction — it rotates the
rendered text in software and may be slightly slower.

---

## 4. Using It as a TTY Console

Linux can map any virtual console (tty1–tty63) to any framebuffer.

### Map a console temporarily

```bash
# Map virtual console 1 (tty1) to the TFT framebuffer (fb1)
sudo con2fbmap 1 1

# Verify
con2fbmap 1
# Output: console 1 is mapped to framebuffer 1
```

You should now see a login prompt (or your shell) on the TFT.  Switch
to it with **Ctrl+Alt+F1** (from a desktop) or it will be active
automatically if you're running headless.

### Map it back to HDMI

```bash
sudo con2fbmap 1 0
```

### Set console font size

The default font may be too small on a 480×320 screen.  Install and
configure a larger font:

```bash
sudo apt install console-setup

# Reconfigure to pick a larger font (Terminus, 16×32 works well)
sudo dpkg-reconfigure console-setup
```

Or set it directly:

```bash
sudo setfont /usr/share/consolefonts/Lat15-Terminus32x16.psf.gz
```

---

## 5. Booting Directly into the TFT Console

To have the system use the TFT as its primary console from boot, add
kernel command-line parameters.

### Step 1 — Edit `cmdline.txt`

Open `/boot/firmware/cmdline.txt` (or `/boot/cmdline.txt`):

```bash
sudo nano /boot/firmware/cmdline.txt
```

Add these parameters to the **existing single line** (do NOT create a
new line):

```
fbcon=map:10
```

This tells the `fbcon` module: map console 0 to fb1, console 1 to fb0.
In other words, the **first console goes to the TFT**.

If you want **all** consoles on the TFT and nothing on HDMI:

```
fbcon=map:1
```

### Step 2 — Reboot

```bash
sudo reboot
```

After reboot, boot messages and the login prompt will appear on the
TFT display.

### Undo

Remove `fbcon=map:...` from `cmdline.txt` and reboot.

---

## 6. Desktop Environment on the TFT

You can run a lightweight X11 or Wayland session on the TFT.  At 480×320
and 20 fps this is best suited for simple kiosk UIs rather than a full
desktop.

### X11 (fbdev)

```bash
# Install minimal X server
sudo apt install xserver-xorg xinit

# Start X on the TFT framebuffer
FRAMEBUFFER=/dev/fb1 startx -- -dpi 96
```

Or create `/etc/X11/xorg.conf.d/99-fbdev.conf`:

```
Section "Device"
    Identifier "KeDei TFT"
    Driver     "fbdev"
    Option     "fbdev" "/dev/fb1"
EndSection
```

### Lightweight desktops that work well at 480×320

- **Matchbox** — designed for small screens / kiosks
- **dwm / i3** — tiling window managers, no wasted space
- **tinywm** — absolute minimum WM

### SDL / Pygame / LVGL

For embedded GUIs without X, point SDL at the framebuffer:

```bash
export SDL_FBDEV=/dev/fb1
export SDL_VIDEODRIVER=fbcon
./your_sdl_app
```

---

## 7. Displaying Images and Text

### Display a BMP/PNG/JPEG image

Use `fbi` (framebuffer imageviewer):

```bash
sudo apt install fbi

# Display an image (auto-scaled to fit)
sudo fbi -T 1 -d /dev/fb1 -a photo.jpg

# Display without cursor and auto-fit
sudo fbi -T 1 -d /dev/fb1 -noverbose -a photo.png
```

The `-T 1` flag selects tty1 (avoids "can't open /dev/tty0" errors).

### Display an image using ffmpeg

Convert any image to raw RGB565 and write directly:

```bash
ffmpeg -i photo.jpg -vf scale=480:320 -pix_fmt rgb565le -f rawvideo - \
    > /dev/fb1
```

### Write text directly

```bash
# Using figlet + cat
sudo apt install figlet
figlet "Hello!" | fold -w 60 > /dev/vcs1   # if console is mapped
```

Or render text into an image and push it to the framebuffer.

---

## 8. Adjusting Frame Rate

The `fps` parameter controls how often the kernel's deferred I/O
flushes dirty framebuffer pages to the display.

```
dtoverlay=kedei,fps=20
```

| fps | Behaviour |
|-----|-----------|
| 1   | Very slow updates, minimal CPU. Good for static info displays. |
| 10  | Smooth enough for scrolling text. |
| 20  | **Default.** Good balance for interactive console use. |
| 30+ | Diminishing returns — SPI bandwidth becomes the bottleneck. |

Higher fps values increase CPU and SPI bus usage.  At 39 MHz SPI with
the 3-byte-per-pixel protocol, the theoretical maximum throughput is
roughly 13 MHz effective pixel rate, which limits full-screen redraws
to about 28 fps for 480×320×16bpp.

---

## 9. Adjusting SPI Speed

```
dtoverlay=kedei,speed=39000000
```

The default 39 MHz is the maximum tested speed.  If you see display
corruption (garbled pixels, shifted colours), try reducing it:

```
dtoverlay=kedei,speed=16000000
```

---

## 10. Debugging and Diagnostics

### Enable debug output

Set the debug level via `config.txt`:

```
dtoverlay=kedei,debug=7
```

Or at runtime via sysfs (if the framebuffer is fb1):

```bash
echo 7 | sudo tee /sys/class/graphics/fb1/device/debug
```

Debug output goes to the kernel log:

```bash
dmesg | grep -i kedei
dmesg | grep -i fbtft
```

### Check the framebuffer info

```bash
fbset -fb /dev/fb1 -i
```

This shows resolution, colour depth, and timing info.

### Verify module is loaded

```bash
lsmod | grep -E 'fbtft|kedei'
# Expected:
#   fb_kedei62   ...  0
#   fbtft        ...  1 fb_kedei62
```

### Verify overlay is applied

```bash
sudo vcdbg log msg 2>&1 | grep -i kedei
# or
dtc -I fs /proc/device-tree 2>/dev/null | grep kedei
```

---

## 11. Unloading the Driver

If you need to remove the driver without rebooting:

```bash
# Unmap any console first
sudo con2fbmap 1 0

# Remove the modules (order matters)
sudo rmmod fb_kedei62
sudo rmmod fbtft
```

To permanently disable, remove `dtoverlay=kedei` from `config.txt` and
reboot.

---

## 12. Boot Configuration Reference

All configuration lives in `/boot/firmware/config.txt` (Bookworm) or
`/boot/config.txt` (older Raspberry Pi OS).

### Minimal working config

```ini
# Enable SPI bus
dtparam=spi=on

# Load the KeDei display overlay
dtoverlay=kedei
```

### Full config with all parameters

```ini
dtparam=spi=on
dtoverlay=kedei,speed=39000000,rotate=270,fps=20,debug=0
```

### Console on TFT at boot

In `cmdline.txt`, append to the existing line:

```
fbcon=map:10
```

### Summary of parameters

| Parameter | Where | Default | Description |
|-----------|-------|---------|-------------|
| `speed` | config.txt | 39000000 | SPI clock (Hz) |
| `rotate` | config.txt | 270 | Hardware rotation (0/90/180/270) |
| `fps` | config.txt | 20 | Deferred I/O refresh rate |
| `debug` | config.txt or sysfs | 0 | Debug verbosity (0–7) |
| `fbcon=map:10` | cmdline.txt | — | Map tty0→fb1, tty1→fb0 |
| `fbcon=map:1` | cmdline.txt | — | All consoles on fb1 |
