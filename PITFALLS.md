# KeDei 6.2 FBTFT Driver — Pitfalls & Troubleshooting

This document records every problem encountered while porting and debugging the
KeDei 6.2 SPI TFT driver on **Raspberry Pi 3 Model B** running
**Raspberry Pi OS Bookworm (kernel 6.12+)**.  Each section explains what went
wrong, why, and how to fix it.

---

## Table of Contents

1. [Hardware Architecture](#1-hardware-architecture)
2. [SPI Chip-Select Polarity (CE1 Must Be Active-LOW)](#2-spi-chip-select-polarity)
3. [GPIO 8 Is Not a Normal Chip-Select — It's a 74HC595 Latch](#3-gpio-8-is-a-74hc595-latch)
4. [Freeing GPIO 8 from the SPI Core](#4-freeing-gpio-8-from-the-spi-core)
5. [GPIO Consumer-ID Bug in fbtft_request_one_gpio()](#5-gpio-consumer-id-bug)
6. [Deferred I/O Warnings (Kernel 6.12+)](#6-deferred-io-warnings)
7. [fb_ops Must Be `static const` (Kernel 6.1+)](#7-fb_ops-must-be-static-const)
8. [Module Vermagic Mismatch — Build Natively](#8-module-vermagic-mismatch)
9. [Bookworm Boot Partition Path](#9-bookworm-boot-partition-path)
10. [Device Tree Overlay Extension (.dtbo)](#10-device-tree-overlay-extension)
11. [DTS `unit_address_vs_reg` Warnings](#11-dts-unit_address_vs_reg-warnings)
12. [Kernel 6.12 API Changes](#12-kernel-612-api-changes)
13. [fbtft.h Is Not Exported by the Kernel](#13-fbtfth-is-not-exported)
14. [Touchscreen Conflicts with Display Latch](#14-touchscreen-conflicts)
15. [FPS Default Override](#15-fps-default-override)
16. [Kconfig: FB_SYS_FOPS No Longer Exists](#16-fb_sys_fops-removed)

---

## 1. Hardware Architecture

The KeDei 6.2 board does **not** connect the Raspberry Pi SPI bus directly to
the LCD controller (R61581).  Instead it routes SPI through **three 74HC595
shift registers** that act as a parallel bus converter (CPLD-like).

```
RPi SPI0  ──►  3× 74HC595 shift registers  ──►  R61581 LCD controller
                     ▲
               GPIO 8 = RCLK (latch)
               GPIO 7 = CE1  (SPI chip-select, active-low)
```

Every SPI transaction is a **3-byte packet**:
- Command: `{0x11, 0x00, cmd_byte}`
- Data:    `{0x15, high_byte, low_byte}`
- Reset:   `{0x00, 0x01/0x00, 0x00, 0x00}` (4 bytes)

Each packet must be **latched**: GPIO 8 goes HIGH before `spi_sync()`, then LOW
after, to clock the shift register outputs onto the parallel bus.

---

## 2. SPI Chip-Select Polarity

**Symptom**: Display stays blank.  SPI transactions appear on a logic analyser
but the 74HC595 ignores them.

**Root cause**: GPIO 7 (CE1) must be **active-LOW**.  The 74HC595 shift
registers clock in data while CE1 is LOW.  Adding `spi-cs-high` to the DTS
inverts the polarity and the CPLD never latches data.

**Fix**: In the DTS controller-level `cs-gpios`, declare CE1 as active-low
(flag `1` = `GPIO_ACTIVE_LOW`):

```dts
cs-gpios = <0>,              /* CE0: not managed  */
           <&gpio 7 1>;      /* CE1: GPIO_ACTIVE_LOW */
```

Do **NOT** add `spi-cs-high` to the device node.

**Why this is confusing**: The SPI core in recent kernels warns
*"GPIO handle specifies active low - ignored"* when the device node lacks
`spi-cs-high`.  This warning is cosmetic and misleading — the GPIO flag IS
respected despite the warning.  Adding `spi-cs-high` to silence the warning
would actually break the display.

---

## 3. GPIO 8 Is a 74HC595 Latch

**Symptom**: `par->gpio.cs` is NULL inside `kedei_write()`, so the latch toggle
is skipped.  The SPI data reaches the shift registers but is never latched onto
the LCD bus.

**Root cause**: GPIO 8 (normally CE0) is wired to the 74HC595 RCLK (register
clock / latch) input.  It is NOT a standard SPI chip-select — it needs to be
toggled HIGH→LOW around each 3-byte packet.

The driver obtains this GPIO via the `cs-gpios` DT property on the **device
node** (not the controller):

```dts
kedei62@1 {
    cs-gpios = <&gpio 8 0>;   /* GPIO_ACTIVE_HIGH */
};
```

FBTFT's `fbtft_request_gpios_dt()` reads this property and stores the
descriptor in `par->gpio.cs`, which `kedei_write()` then toggles.

---

## 4. Freeing GPIO 8 from the SPI Core

**Symptom**: `fbtft_request_one_gpio()` fails to acquire GPIO 8 because the SPI
core already owns it as CE0.

**Root cause**: The default `cs-gpios` for the SPI0 controller assigns GPIO 8
to CE0.  Even if no device uses `reg = <0>`, the SPI core still requests the
GPIO.

**Fix**: Override `cs-gpios` at the **SPI controller level** with `<0>` for the
CE0 slot, which tells the SPI core not to manage that GPIO:

```dts
fragment@0 {
    target = <&spi0>;
    __overlay__ {
        cs-gpios = <0>,              /* CE0: not managed */
                   <&gpio 7 1>;      /* CE1: GPIO_ACTIVE_LOW */
    };
};
```

The `<0>` (phandle zero) means "no GPIO here" — the SPI core ignores CE0
entirely, leaving GPIO 8 available for the driver.

---

## 5. GPIO Consumer-ID Bug

**Symptom**: `fbtft_request_one_gpio()` returns `-ENOENT` even though the DT
property (`cs-gpios`) exists and is correct.

**Root cause**: The original `fbtft_request_one_gpio()` passed the raw DT
property name (e.g., `"cs-gpios"`) as the consumer ID to
`devm_gpiod_get_index()`.  The gpiod API expects the **stem** without the
`-gpios` / `-gpio` suffix — i.e. just `"cs"`.

**Fix** (in `fbtft-core.c`):

```c
/*
 * devm_gpiod_get_index() expects the consumer-ID without the
 * "-gpios"/"-gpio" suffix that appears in the DT property name.
 * Strip it before calling.
 */
strscpy(consumer, name, sizeof(consumer));
p = strstr(consumer, "-gpios");
if (p)
    *p = '\0';
else if ((p = strstr(consumer, "-gpio")))
    *p = '\0';

*gpiod = devm_gpiod_get_index(dev, consumer, index, GPIOD_OUT_LOW);
```

---

## 6. Deferred I/O Warnings

**Symptom**: Kernel WARNING on module load:
`fb_deferred_io_init: smem_len is not set`, and/or
`WARN_ON(!fbdefio->sort_pagereflist)`.

**Root cause** (kernel 6.12+):
1. `info->fix.smem_len` must be set **before** calling `fb_deferred_io_init()`.
2. `fbdefio->sort_pagereflist` must be set to `true`.
3. `fb_ops` must include `.fb_mmap = fb_deferred_io_mmap`.

**Fix** (in `fbtft-core.c`, `fbtft_framebuffer_alloc()`):

```c
info->fix.smem_len = vmem_size;          /* BEFORE fb_deferred_io_init */

fbdefio->delay = HZ / fps;
fbdefio->sort_pagereflist = true;        /* required since 6.8+ */
fbdefio->deferred_io = fbtft_deferred_io;
fb_deferred_io_init(info);               /* AFTER smem_len is set */
```

And in `fb_ops`:
```c
.fb_mmap = fb_deferred_io_mmap,
```

Without `.fb_mmap`, user-space `mmap()` on `/dev/fbN` would fault.

Also, the deferred I/O callback signature changed — the page list is now
`struct list_head *pagereflist` containing `struct fb_deferred_io_pageref`
entries (accessed via `pageref->offset`), not `struct page` via `lru`.

---

## 7. fb_ops Must Be `static const`

**Symptom**: Compiler error about assigning to a const-qualified field, or
modifying `info->fbops`.

**Root cause**: Since kernel 6.1, `info->fbops` is `const struct fb_ops *`.
The `fb_ops` structure can no longer be dynamically allocated or modified after
init.

**Fix**: Declare `fb_ops` as `static const` inside `fbtft_framebuffer_alloc()`.

---

## 8. Module Vermagic Mismatch

**Symptom**: `insmod` refuses to load the module:
`version magic '...' should be '...'`.

**Root cause**: Cross-compiling from another machine or a Docker container
produces modules whose vermagic string does not match the running kernel's
exact build configuration.

**Fix**: Build the modules **natively on the Raspberry Pi** against the
installed kernel headers:

```bash
sudo apt install raspberrypi-kernel-headers
make KDIR=/lib/modules/$(uname -r)/build
```

Alternatively, force-load with `insmod -f`, but this is not recommended for
production.

---

## 9. Bookworm Boot Partition Path

**Symptom**: `install_dtb.sh` copies the overlay to `/boot/overlays/` but the
system doesn't see it.

**Root cause**: Raspberry Pi OS **Bookworm** (and later) mounts the boot
partition at `/boot/firmware/`, not `/boot/`.

**Fix**: The install script auto-detects the correct path:

```bash
if [ -d "/boot/firmware/overlays" ]; then
    BOOT_OVERLAYS="/boot/firmware/overlays"
else
    BOOT_OVERLAYS="/boot/overlays"
fi
```

Similarly, `config.txt` is at `/boot/firmware/config.txt` on Bookworm.

---

## 10. Device Tree Overlay Extension

**Symptom**: Overlay compiled as `.dtb` is not picked up by the firmware.

**Root cause**: Raspberry Pi firmware expects overlays with the `.dtbo`
extension.

**Fix**: Compile with the correct output name:

```bash
dtc -@ -I dts -O dtb -o kedei.dtbo kedei.dts
```

And reference it in `config.txt` without extension:

```
dtoverlay=kedei
```

---

## 11. DTS `unit_address_vs_reg` Warnings

**Symptom**: `dtc` warns: `node has a unit name but no reg property`.

**Root cause**: Any node with a `@N` unit address must have a matching `reg`
property.  Common culprits: `spidev@0`, `spidev@1`, `kedei62@1`.

**Fix**: Add `reg = <N>;` to every `@N` node, matching the SPI chip-select
number.

---

## 12. Kernel 6.12 API Changes

A summary of API changes that required code modifications:

| Old API | New API | File(s) |
|---------|---------|---------|
| `spi->master` | `spi->controller` | fbtft-core.c |
| `spi->chip_select` | `spi_get_chipselect(spi, 0)` | fbtft-core.c |
| `gpio_set_value()` / `<linux/gpio.h>` | `gpiod_set_value()` / `<linux/gpio/consumer.h>` | all |
| `int remove(struct spi_device *)` | `void remove(struct spi_device *)` | fbtft.h (macro) |
| `int remove(struct platform_device *)` | `void remove(struct platform_device *)` | fbtft.h (macro) |
| `FBINFO_FLAG_DEFAULT` | `FBINFO_VIRTFB` | fbtft-core.c |
| `unregister_framebuffer()` returns int | returns `void` | fbtft-core.c |
| `info->dev` | `info->device` (unless `CONFIG_FB_DEVICE`) | all |
| `strlcpy()` | `strscpy()` | fbtft-core.c |
| `struct page` via `lru` in deferred_io | `struct fb_deferred_io_pageref` | fbtft-core.c |
| `.owner` in `platform_driver.driver` | removed (set by macros) | fbtft.h |

---

## 13. fbtft.h Is Not Exported by the Kernel

**Symptom**: Compile error — `fbtft.h: No such file or directory`.

**Root cause**: The in-kernel FBTFT subsystem does NOT export its header to
`/lib/modules/.../build/include/`.  The header `fbtft.h` is internal to the
`drivers/staging/fbtft/` directory.

**Fix**: Bundle the entire FBTFT subsystem (`fbtft-core.c`, `fbtft-bus.c`,
`fbtft-io.c`, `fbtft-sysfs.c`, `fbtft.h`, `internal.h`) with the driver and
build them together as `fbtft.ko`.  Use `#include "fbtft.h"` (quotes, not angle
brackets).

---

## 14. Touchscreen Conflicts

**Symptom**: Enabling the ADS7846 touchscreen node in the DTS causes the
display to stop working or the GPIO request to fail.

**Root cause**: The touchscreen uses SPI CE0 (GPIO 8), but GPIO 8 is also the
74HC595 latch signal needed by the display driver.  Both cannot own the same
GPIO simultaneously.

**Fix**: Keep the touchscreen DTS node **commented out** until a hardware
or software GPIO-sharing solution is implemented.  The ADS7846 would require
either a different CE pin or a GPIO multiplexer.

---

## 15. FPS Default Override

**Symptom**: Display updates extremely slowly (once per second).

**Root cause**: The driver's `display` struct sets `.fps = FPS` where
`#define FPS 1` (one frame per second).  This is a safe minimum, but far too
slow for interactive use.

**Fix**: Override via the DTS `fps` property:

```dts
fps = <20>;
```

The FBTFT core reads this from the DT and uses it instead of the driver's
compiled default.  A value of 20 provides a reasonable balance between CPU load
and visual responsiveness.

You can also pass it as a boot parameter:

```
dtoverlay=kedei,fps=20
```

---

## 16. Kconfig: FB_SYS_FOPS No Longer Exists

**Symptom**: Build error referencing `FB_SYS_FOPS`.

**Root cause**: The `FB_SYS_FOPS` Kconfig symbol was removed in recent kernels.
The functions it provided (`fb_sys_read`, `fb_sys_write`) are now always
available when `CONFIG_FB` is enabled.

**Fix**: Remove `select FB_SYS_FOPS` from the `Kconfig` file.  The required
symbols (`FB_SYS_FILLRECT`, `FB_SYS_COPYAREA`, `FB_SYS_IMAGEBLIT`) still exist
and must be selected.

---

## Quick Checklist

Before deploying, verify:

- [ ] Built **natively** on the target RPi (vermagic match)
- [ ] `cs-gpios` at controller level has `<0>` for CE0 and `<&gpio 7 1>` for CE1
- [ ] **No** `spi-cs-high` in the device node
- [ ] Device node has `cs-gpios = <&gpio 8 0>` for the latch signal
- [ ] `fps = <20>` (or desired value) in the DTS
- [ ] Overlay installed as `.dtbo` in the correct boot partition
- [ ] `dtoverlay=kedei` and `dtparam=spi=on` in `config.txt`
- [ ] Touchscreen node is **disabled** (GPIO 8 conflict)
