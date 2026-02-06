// SPDX-License-Identifier: GPL-2.0+
/*
 * 2019 Tong Zhang <ztong@vt.edu>
 * FBTFT Driver for KeDei 6.2 Display
 *
 * Modernized for kernel 6.12+ — replaced legacy GPIO API with gpiod,
 * fixed C89 compliance, and uses proper logging.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"kedei62"
#define WIDTH		480
#define HEIGHT		320
#define FPS		1

static const uint8_t lcd_rotations[4] = {
	0xEA,	/*   0 deg */
	0x4A,	/*  90 deg */
	0x2A,	/* 180 deg */
	0x0A	/* 270 deg */
};

static uint16_t lcd_h;
static uint16_t lcd_w;

static int kedei_write(struct fbtft_par *par, void *buf, size_t len)
{
	/*
	 * The KeDei 6.2 board uses 3× 74HC595 shift registers.
	 * GPIO 7 (CE1) is the SPI chip-select (active-low, managed
	 * by the SPI framework).  GPIO 8 (CE0) is the 74HC595 RCLK
	 * (latch) signal — we toggle it HIGH before and LOW after
	 * each SPI packet to latch shift-register data onto the
	 * parallel LCD bus.
	 */
	if (par->gpio.cs)
		gpiod_set_value(par->gpio.cs, 1);

	fbtft_write_spi(par, buf, len);

	if (par->gpio.cs)
		gpiod_set_value(par->gpio.cs, 0);

	return 0;
}

/*
 * Send a command byte to the R61581 via the 74HC595 bus.
 * Prefix 0x11 tells the CPLD "this is a command".
 */
static void lcd_cmd(struct fbtft_par *par, uint8_t cmd)
{
	uint8_t b1[3] = { 0x11, 0x00, cmd };

	kedei_write(par, b1, sizeof(b1));
}

/*
 * Send a data byte to the R61581 via the 74HC595 bus.
 * Prefix 0x15 tells the CPLD "this is data".
 */
static void lcd_data(struct fbtft_par *par, uint8_t dat)
{
	uint8_t b1[3] = { 0x15, 0x00, dat };

	kedei_write(par, b1, sizeof(b1));
}

static void lcd_setrotation(struct fbtft_par *par, uint8_t m)
{
	lcd_cmd(par, 0x36);
	lcd_data(par, lcd_rotations[m]);
	if (m & 1) {
		lcd_h = WIDTH;
		lcd_w = HEIGHT;
	} else {
		lcd_h = HEIGHT;
		lcd_w = WIDTH;
	}
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys,
			 int xe, int ye)
{
	lcd_cmd(par, 0x2A);
	lcd_data(par, xs >> 8);
	lcd_data(par, xs & 0xFF);
	lcd_data(par, xe >> 8);
	lcd_data(par, xe & 0xFF);
	lcd_cmd(par, 0x2B);
	lcd_data(par, ys >> 8);
	lcd_data(par, ys & 0xFF);
	lcd_data(par, ye >> 8);
	lcd_data(par, ye & 0xFF);
	lcd_cmd(par, 0x2C);
}

/*
 * Transfer pixel data from the framebuffer to the display.
 *
 * Each RGB565 pixel is two bytes.  The framebuffer stores them in
 * little-endian order, but the R61581 expects big-endian, so we
 * byte-swap each pair: vmem8[i+1] goes first, vmem8[i] second.
 * The 0x15 prefix tells the CPLD "this is pixel data".
 */
static int write_vmem(struct fbtft_par *par, size_t offset, size_t len)
{
	u8 *vmem8 = (u8 *)(par->info->screen_buffer + offset);
	uint8_t b1[3];
	int i;

	b1[0] = 0x15;
	for (i = 0; i < len; i += 2) {
		b1[1] = vmem8[i + 1];
		b1[2] = vmem8[i];
		kedei_write(par, b1, sizeof(b1));
	}
	return 0;
}

/*
 * Hardware reset via the 74HC595 bus.  The 4-byte sequence with prefix
 * 0x00 is the CPLD's reset command (active-high pulse on bit 0):
 *   {0x00, 0x01, ...} = assert reset
 *   {0x00, 0x00, ...} = release reset
 *   {0x00, 0x01, ...} = assert again (double-pulse for reliability)
 */
static void reset(struct fbtft_par *par)
{
	uint8_t buff[4];

	/* Assert reset */
	buff[0] = 0x00; buff[1] = 0x01; buff[2] = 0x00; buff[3] = 0x00;
	kedei_write(par, buff, sizeof(buff));
	mdelay(50);

	/* Release reset */
	buff[0] = 0x00; buff[1] = 0x00; buff[2] = 0x00; buff[3] = 0x00;
	kedei_write(par, buff, sizeof(buff));
	mdelay(100);

	/* Assert reset again (double-pulse) */
	buff[0] = 0x00; buff[1] = 0x01; buff[2] = 0x00; buff[3] = 0x00;
	kedei_write(par, buff, sizeof(buff));
	mdelay(50);
}

/*
 * R61581 (Renesas) LCD controller initialization sequence.
 *
 * Register reference (R61581 datasheet):
 *   0x11 — Sleep Out
 *   0xB0 — Manufacturer Command Access Protect
 *   0xB3 — Frame Memory Access & Interface Setting
 *   0xB9 — Internal Oscillator Setting (undocumented, vendor-specific)
 *   0xC0 — Panel Driving Setting (bias, scan lines, etc.)
 *   0xC1 — Display Timing for Normal Mode
 *   0xC4 — Source/Gate Timing (undocumented, vendor-specific)
 *   0xC6 — Interface Control (undocumented, vendor-specific)
 *   0xC8 — Gamma Setting
 *   0x35 — Tearing Effect Line ON
 *   0x36 — Memory Access Control (rotation / BGR / mirror)
 *   0x3A — Pixel Format (16-bit/18-bit)
 *   0x44 — Set Tear Scanline
 *   0xD0 — Power Setting
 *   0xD1 — VCOM Control
 *   0xD2 — Power Setting for Normal Mode
 *   0x29 — Display ON
 *   0x2A — Column Address Set
 *   0x2B — Page Address Set
 *   0xB4 — Display Mode / Frame Inversion Control
 *   0x2C — Memory Write (begin pixel data)
 */
static int init_display(struct fbtft_par *par)
{
	reset(par);

	/* NOP + synchronization pulses to flush the 74HC595 pipeline */
	lcd_cmd(par, 0x00);
	mdelay(10);
	lcd_cmd(par, 0xFF);
	lcd_cmd(par, 0xFF);
	mdelay(10);
	lcd_cmd(par, 0xFF);
	lcd_cmd(par, 0xFF);
	lcd_cmd(par, 0xFF);
	lcd_cmd(par, 0xFF);
	mdelay(15);

	/* 0x11: Sleep Out — wake up the controller */
	lcd_cmd(par, 0x11);
	mdelay(150);

	/* 0xB0: Manufacturer Command Access Protect — unlock */
	lcd_cmd(par, 0xB0);
	lcd_data(par, 0x00);

	/* 0xB3: Frame Memory Access & Interface Setting */
	lcd_cmd(par, 0xB3);
	lcd_data(par, 0x02);
	lcd_data(par, 0x00);
	lcd_data(par, 0x00);
	lcd_data(par, 0x00);

	/* 0xB9: Internal oscillator (vendor-specific) */
	lcd_cmd(par, 0xB9);
	lcd_data(par, 0x01);
	lcd_data(par, 0x00);
	lcd_data(par, 0x0F);
	lcd_data(par, 0x0F);

	/* 0xC0: Panel Driving Setting */
	lcd_cmd(par, 0xC0);
	lcd_data(par, 0x13);
	lcd_data(par, 0x3B);
	lcd_data(par, 0x00);
	lcd_data(par, 0x02);
	lcd_data(par, 0x00);
	lcd_data(par, 0x01);
	lcd_data(par, 0x00);
	lcd_data(par, 0x43);

	/* 0xC1: Display Timing for Normal Mode */
	lcd_cmd(par, 0xC1);
	lcd_data(par, 0x08);
	lcd_data(par, 0x0F);
	lcd_data(par, 0x08);
	lcd_data(par, 0x08);

	/* 0xC4: Source/Gate Timing (vendor-specific) */
	lcd_cmd(par, 0xC4);
	lcd_data(par, 0x11);
	lcd_data(par, 0x07);
	lcd_data(par, 0x03);
	lcd_data(par, 0x04);

	/* 0xC6: Interface Control (vendor-specific) */
	lcd_cmd(par, 0xC6);
	lcd_data(par, 0x00);

	/* 0xC8: Gamma Setting (20 parameters for pos/neg curves) */
	lcd_cmd(par, 0xC8);
	lcd_data(par, 0x03);
	lcd_data(par, 0x03);
	lcd_data(par, 0x13);
	lcd_data(par, 0x5C);
	lcd_data(par, 0x03);
	lcd_data(par, 0x07);
	lcd_data(par, 0x14);
	lcd_data(par, 0x08);
	lcd_data(par, 0x00);
	lcd_data(par, 0x21);
	lcd_data(par, 0x08);
	lcd_data(par, 0x14);
	lcd_data(par, 0x07);
	lcd_data(par, 0x53);
	lcd_data(par, 0x0C);
	lcd_data(par, 0x13);
	lcd_data(par, 0x03);
	lcd_data(par, 0x03);
	lcd_data(par, 0x21);
	lcd_data(par, 0x00);

	/* 0x35: Tearing Effect Line ON */
	lcd_cmd(par, 0x35);
	lcd_data(par, 0x00);

	/* 0x36: Memory Access Control — landscape, BGR */
	lcd_cmd(par, 0x36);
	lcd_data(par, 0x60);

	/* 0x3A: Pixel Format — 16 bits/pixel (RGB565) */
	lcd_cmd(par, 0x3A);
	lcd_data(par, 0x55);

	/* 0x44: Set Tear Scanline */
	lcd_cmd(par, 0x44);
	lcd_data(par, 0x00);
	lcd_data(par, 0x01);

	/* 0xD0: Power Setting */
	lcd_cmd(par, 0xD0);
	lcd_data(par, 0x07);
	lcd_data(par, 0x07);
	lcd_data(par, 0x1D);
	lcd_data(par, 0x03);

	/* 0xD1: VCOM Control */
	lcd_cmd(par, 0xD1);
	lcd_data(par, 0x03);
	lcd_data(par, 0x30);
	lcd_data(par, 0x10);

	/* 0xD2: Power Setting for Normal Mode */
	lcd_cmd(par, 0xD2);
	lcd_data(par, 0x03);
	lcd_data(par, 0x14);
	lcd_data(par, 0x04);

	/* 0x29: Display ON */
	lcd_cmd(par, 0x29);
	mdelay(30);

	/* 0x2A: Column Address Set — 0..319 (0x013F) */
	lcd_cmd(par, 0x2A);
	lcd_data(par, 0x00);
	lcd_data(par, 0x00);
	lcd_data(par, 0x01);
	lcd_data(par, 0x3F);

	/* 0x2B: Page Address Set — 0..479 (0x01E0) */
	lcd_cmd(par, 0x2B);
	lcd_data(par, 0x00);
	lcd_data(par, 0x00);
	lcd_data(par, 0x01);
	lcd_data(par, 0xE0);

	/* 0xB4: Display Mode / Frame Inversion — normal */
	lcd_cmd(par, 0xB4);
	lcd_data(par, 0x00);

	/* 0x2C: Memory Write — ready for pixel data */
	lcd_cmd(par, 0x2C);

	mdelay(10);

	/* Set rotation to 270° (landscape, connector on the right) */
	lcd_setrotation(par, 3);

	dev_info(par->info->device, "kedei62 initialized\n");
	return 0;
}

/*
 * request_gpios / verify_gpios — intentional no-ops.
 *
 * When a DT node is present, fbtft_probe_dt() overrides these with
 * fbtft_request_gpios_dt() and the default fbtft_verify_gpios(),
 * which parse "cs-gpios", "led-gpios", etc. from the device tree.
 * These stubs exist only as placeholders for the non-DT path.
 */
static int request_gpios(struct fbtft_par *par)
{
	return 0;
}

static int verify_gpios(struct fbtft_par *par)
{
	return 0;
}

/*
 * Note: .width and .height are swapped (HEIGHT=320, WIDTH=480) because
 * the R61581's native orientation is portrait.  Rotation to landscape
 * is done in init_display() via lcd_setrotation().
 * .fps = 1 is a safe default; override via DT "fps" property.
 */
static struct fbtft_display display = {
	.regwidth = 8,
	.width = HEIGHT,
	.height = WIDTH,
	.fps = FPS,
	.fbtftops = {
		.write = kedei_write,
		.write_vmem = write_vmem,
		.set_addr_win = set_addr_win,
		.reset = reset,
		.init_display = init_display,
		.request_gpios = request_gpios,
		.verify_gpios = verify_gpios,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "kedei62", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:kedei62");
MODULE_ALIAS("platform:kedei62");

MODULE_DESCRIPTION("FB driver for KeDei6.2 display");
MODULE_AUTHOR("Tong Zhang<ztong@vt.edu>");
MODULE_LICENSE("GPL");

