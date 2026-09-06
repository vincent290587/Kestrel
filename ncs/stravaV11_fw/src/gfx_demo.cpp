/*
 * Phase 7: draws real content via Adafruit_GFX (through ZephyrGFX's
 * buffer, see lib/source/vue/ZephyrGFX.h) and pushes it to the actual
 * LS027 display path brought up in Phase 2 (sharp,ls0xx / display_write())
 * -- replacing that phase's raw alternating-stripe test pattern with
 * something drawn through the real graphics pipeline stravaV10's UI would
 * use. Same caveat as Phase 2: no physical display is wired to this DK, so
 * this proves the draw + push pipeline runs without error, not that the
 * result looks right on real glass.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>

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

	gfx.fillScreen(0);
	gfx.setTextColor(1);
	gfx.setFont(&Org_01);
	gfx.setCursor(10, 30);
	gfx.print("stravaV11");
	gfx.drawRect(0, 0, ZEPHYR_GFX_WIDTH, ZEPHYR_GFX_HEIGHT, 1);
	gfx.fillRect(20, 60, 60, 60, 1);
	gfx.drawLine(0, 0, ZEPHYR_GFX_WIDTH - 1, ZEPHYR_GFX_HEIGHT - 1, 1);

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
