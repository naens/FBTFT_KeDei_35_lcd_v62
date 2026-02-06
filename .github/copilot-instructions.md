# FBTFT KeDei 6.2 Driver - AI Coding Instructions

## Project Context
This is a Linux kernel module driver for the KeDei 6.2 SPI TFT display, built upon the FBTFT (FrameBuffer TFT) subsystem. It targets the Raspberry Pi architecture.

## Architecture & Data Flow
- **FBTFT Subsystem**: The project bundles the FBTFT core (`fbtft-core.c`, `fbtft-bus.c`, `fbtft-io.c`) locally.
- **Specific Driver**: `fb_kedei62.c` implements the display initialization, register configuration, and memory write operations specific to the KeDei panel.
- **Device Tree**: `kedei.dts` describes the hardware connections (SPI, GPIOs) and loads the driver via `dtoverlay`.

## Developer Workflow

### Build Environment
- **Dependencies**: Requires `raspberrypi-kernel-headers` (or equivalent kernel headers for the target system).
- **Command**: Run `make` to build the kernel modules (`.ko` files).
- **Config**: The `Makefile` generates a local `.config` from `Kconfig` to set `CONFIG_FB_TFT` and `CONFIG_FB_KEDEI62`.

### Installation
- **Modules**: `install_ko.sh` handles module installation.
  - **Warning**: Provides a hardcoded path `/home/tong/fbtft/*.ko`. AI agents should verify/fix this path to use `$PWD` or the local build directory when suggesting installation fixes.
- **Device Tree**: `install_dtb.sh` compiles `kedei.dts` to `kedei.dtb` (using `dtc`) and copies it to `/boot/overlays/`.

## Coding Conventions
- **Style**: Strict Linux Kernel Coding Style.
  - Indentation: 8-character tabs (no spaces).
  - C Version: C89/GNU89 standard.
- **Logging**: Use `dev_info()`, `dev_err()`, or `fbtft_par->info->device` for logging, not `printk`.
- **Hardware Access**:
  - Use `gpio_set_value()` for GPIO control.
  - Use `fbtft_write_spi()` for SPI transactions.
  - The driver defines `write()`, `lcd_cmd()`, `lcd_data()` wrappers in `fb_kedei62.c` to abstract the specific protocol (often involving helper bytes).

## Critical Files
- `fb_kedei62.c`: Main driver implementation (look here for init sequences and gamma curves).
- `fbtft.h`: Core structures (`struct fbtft_par`, `struct fbtft_display`) and function prototypes.
- `fbtft-core.c`: Logic for framebuffer registration and update loops.
- `kedei.dts`: Device Tree overlay source.

## Common Patterns
- **Display Update**: The `write_vmem` function is called to flush the framebuffer to the display. It converts the linear framebuffer memory into the specific SPI sequence required by the hardware.
- **Register Access**: The display uses specific command/data protocols (e.g., `0x11` byte prefix for commands in `lcd_cmd`).
