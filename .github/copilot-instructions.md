# FBTFT KeDei 6.2 Driver - AI Coding Instructions

## Project Context
Linux kernel module driver for the KeDei 6.2 SPI TFT display (480×320), built upon a locally-bundled FBTFT subsystem. Targets Raspberry Pi 3 Model B, kernel 6.12+.

## Architecture & Data Flow
- **`fbtft.ko`**: Core module from `fbtft-core.c`, `fbtft-sysfs.c`, `fbtft-bus.c`, `fbtft-io.c`. Provides framebuffer allocation, deferred I/O, SPI transport, and sysfs interface.
- **`fb_kedei62.ko`**: Display-specific driver in `fb_kedei62.c`. Implements the KeDei's custom 3-byte SPI protocol: `{0x11, 0x00, cmd}` for commands, `{0x15, 0x00, data}` for data. Registered via the `FBTFT_REGISTER_DRIVER` macro in `fbtft.h`.
- **`kedei.dts`**: Device Tree overlay binding the driver to SPI0 CE1 at 39 MHz plus an ADS7846 touchscreen on CE0.

## Build & Install (on Raspberry Pi)
```bash
sudo apt install raspberrypi-kernel-headers build-essential device-tree-compiler
make                    # builds fbtft.ko and fb_kedei62.ko
sudo ./install_ko.sh    # copies .ko to /lib/modules/$(uname -r)/kernel/misc
sudo ./install_dtb.sh   # compiles kedei.dts → kedei.dtb into /boot/overlays/
# Add "dtoverlay=kedei" and "dtparam=spi=on" to /boot/config.txt, then reboot
```

## Coding Conventions
- **Style**: Linux Kernel Coding Style — 8-char tabs, C89/GNU89 standard.
- **GPIO**: Use `gpiod_*` API only (e.g., `gpiod_set_value(par->gpio.cs, 1)`). Legacy `gpio_set_value()` / `<linux/gpio.h>` is removed.
- **Logging**: Use `dev_info(par->info->device, ...)`, never `printk`.
- **fb_ops**: Must be `static const struct fb_ops` (kernel 6.1+). Cannot be dynamically allocated.
- **SPI**: `spi->controller` (not the removed `spi->master`). `spi_driver.remove` returns `void`.
- **Platform**: `platform_driver.remove` returns `void`. No `.owner` in `.driver` (set by macros).
- **Framebuffer**: `unregister_framebuffer()` returns `void`. Use `info->device` (not `info->dev`, which requires `CONFIG_FB_DEVICE`). `FBINFO_FLAG_DEFAULT` is removed — use `FBINFO_VIRTFB`.
- **Deferred I/O**: `deferred_io` callback receives `struct list_head *pagereflist` of `struct fb_deferred_io_pageref` entries (not `struct page` via `lru`). Access offset via `pageref->offset`.

## Critical Files
| File | Role |
|------|------|
| `fb_kedei62.c` | KeDei-specific init sequence, `kedei_write()`, `write_vmem()`, rotation |
| `fbtft.h` | `FBTFT_REGISTER_DRIVER` macro, `struct fbtft_par/display/ops` |
| `fbtft-core.c` | Framebuffer alloc/register, deferred I/O loop, DT probe, GPIO request |
| `fbtft-bus.c` | `write_register` and `write_vmem` implementations for various bus widths |
| `kedei.dts` | DT overlay (SPI0 CE1 for display, CE0 for touchscreen) |
| `Kconfig` | `CONFIG_FB_TFT` and `CONFIG_FB_KEDEI62`; note: `FB_SYS_FOPS` no longer exists |

## Common Patterns
- **SPI protocol**: Every SPI transaction is wrapped: `gpiod_set_value(cs, 1)` → `fbtft_write_spi()` → `gpiod_set_value(cs, 0)`. See `kedei_write()` in `fb_kedei62.c`.
- **Display update**: Kernel's deferred I/O triggers `fbtft_deferred_io()` → `fbtft_update_display()` → `set_addr_win()` + `write_vmem()`. The `write_vmem()` byte-swaps RGB565 pixels into the 3-byte protocol.
- **`install_ko.sh`**: Uses `$SCRIPT_DIR` to locate `.ko` files. Previously had a hardcoded path — do not re-introduce it.
