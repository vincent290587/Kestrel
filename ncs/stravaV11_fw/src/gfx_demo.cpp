/*
 * Phase 7: draws real content via Adafruit_GFX (through ZephyrGFX's
 * buffer, see lib/source/vue/ZephyrGFX.h) and pushes it to the actual
 * LS027 display path brought up in Phase 2 (sharp,ls0xx / display_write())
 * -- replacing that phase's raw alternating-stripe test pattern with
 * something drawn through the real graphics pipeline stravaV10's UI would
 * use. On the DK, no physical display is wired up, so this only proves the
 * draw + push pipeline runs without error. On the real PCB (Phase 11),
 * this is genuinely visible on the LS027 glass -- confirmed against real
 * hardware, including catching and fixing a real bug: the display's CS
 * polarity in the board's own devicetree (GPIO_ACTIVE_LOW) didn't match
 * what zephyr/drivers/display/ls0xx.c hardcodes in its own spi_config
 * (SPI_CS_ACTIVE_HIGH), corrupting every SPI transaction -- fixed in
 * stravav11_nrf52840.dts, not here.
 *
 * setRotation(3): the panel is natively landscape (400x240), but this
 * board's application is portrait, matching stravaV10's own hardcoded
 * rotation=3 for its case mounting -- confirmed against the real board.
 * width()/height() below are the rotation-adjusted logical dims (240x400
 * after rotation 3), not the raw ZEPHYR_GFX_WIDTH/HEIGHT panel constants.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "ZephyrGFX.h"
#include "Org_01.h"

#include "gfx_demo.h"

void gfx_demo(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(disp)) {
		printk("gfx_demo: display device not ready\n");
		return;
	}

	static ZephyrGFX gfx;

	gfx.setRotation(3);

	/* Constructor already defaults the buffer to white (the LS027's
	 * natural blank state) -- fillScreen(1) here is explicit, not a
	 * color change, matching the earlier accidental fillScreen(0)
	 * (black) this demo used before real-glass testing caught it. */
	gfx.fillScreen(1);
	gfx.drawRect(0, 0, gfx.width(), gfx.height(), 0);
	gfx.setFont(&Org_01);
	gfx.setTextColor(0);

	static char line1[] = "STRAVAV11";
	static char line2[] = "GPS COMPUTER";
	static char line3[] = "PHASE 11 DEMO";
	int16_t x1, y1;
	uint16_t w, h;

	gfx.setTextSize(3);
	gfx.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
	gfx.setCursor(MAX(2, (gfx.width() - (int)w) / 2 - x1), 100);
	gfx.print(line1);

	gfx.setTextSize(2);
	gfx.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
	gfx.setCursor(MAX(2, (gfx.width() - (int)w) / 2 - x1), 180);
	gfx.print(line2);

	gfx.setTextSize(1);
	gfx.getTextBounds(line3, 0, 0, &x1, &y1, &w, &h);
	gfx.setCursor(MAX(2, (gfx.width() - (int)w) / 2 - x1), 240);
	gfx.print(line3);

	printk("gfx_demo: %u pixels set\n", gfx.countSetPixels());

	struct display_buffer_descriptor desc = {
		.buf_size = gfx.getBufferSize(),
		.width = ZEPHYR_GFX_WIDTH,
		.height = ZEPHYR_GFX_HEIGHT,
		.pitch = ZEPHYR_GFX_WIDTH,
	};

	int err = display_write(disp, 0, 0, &desc, gfx.getBuffer());
	printk("gfx_demo: display_write() -> %d\n", err);
#else
	printk("gfx_demo: no zephyr,display chosen for this board\n");
#endif
}
