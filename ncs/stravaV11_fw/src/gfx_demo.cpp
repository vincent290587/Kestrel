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
 *
 * gfx/disp are file-scope, not function-local, so gfx_demo() (the initial
 * static message) and gfx_demo_show_sensors() (Phase 11's live ANT+
 * HRM/BSC readout) share the same GFX object and display handle rather
 * than each re-deriving/re-configuring them.
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/time_units.h>

#include "ZephyrGFX.h"
#include "Org_01.h"
#include "map_render.h"

#include "gfx_demo.h"

#if DT_HAS_CHOSEN(zephyr_display)
static ZephyrGFX gfx;
static const struct device *disp;

static bool gfx_ready(void)
{
	if (disp == NULL) {
		disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
		gfx.setRotation(3);
	}
	return device_is_ready(disp);
}

static void gfx_push(void)
{
	struct display_buffer_descriptor desc = {
		.buf_size = gfx.getBufferSize(),
		.width = ZEPHYR_GFX_WIDTH,
		.height = ZEPHYR_GFX_HEIGHT,
		.pitch = ZEPHYR_GFX_WIDTH,
	};

	int err = display_write(disp, 0, 0, &desc, gfx.getBuffer());

	printk("gfx_demo: %u pixels set, display_write() -> %d\n", gfx.countSetPixels(), err);
}

static void gfx_print_centered(char *str, int16_t y, uint8_t size)
{
	int16_t x1, y1;
	uint16_t w, h;

	gfx.setTextSize(size);
	gfx.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
	gfx.setCursor(MAX(2, (gfx.width() - (int)w) / 2 - x1), y);
	gfx.print(str);
}
#endif

void gfx_demo(void)
{
#if DT_HAS_CHOSEN(zephyr_display)
	if (!gfx_ready()) {
		printk("gfx_demo: display device not ready\n");
		return;
	}

	/* Constructor already defaults the buffer to white (the LS027's
	 * natural blank state) -- fillScreen(1) here is explicit, not a
	 * color change, matching the earlier accidental fillScreen(0)
	 * (black) this demo used before real-glass testing caught it. */
	gfx.fillScreen(1);
	gfx.drawRect(0, 0, gfx.width(), gfx.height(), 0);
	gfx.setFont(&Org_01);
	gfx.setTextColor(0);

	gfx_print_centered((char *)"STRAVAV11", 100, 3);
	gfx_print_centered((char *)"GPS COMPUTER", 180, 2);
	gfx_print_centered((char *)"PHASE 11 DEMO", 240, 1);

	gfx_push();
#else
	printk("gfx_demo: no zephyr,display chosen for this board\n");
#endif
}

void gfx_demo_show_sensors(uint8_t bpm, uint16_t rr_ms, bool hrm_paired, uint32_t speed_kph,
			    uint32_t cadence_rpm, bool bsc_paired, float alt_m, bool has_alt)
{
#if DT_HAS_CHOSEN(zephyr_display)
	if (!gfx_ready()) {
		return;
	}

	gfx.fillScreen(1);
	gfx.drawRect(0, 0, gfx.width(), gfx.height(), 0);
	gfx.setFont(&Org_01);
	gfx.setTextColor(0);

	gfx_print_centered((char *)"LIVE DATA", 30, 2);

	static char hr_line[24];
	static char rr_line[24];
	static char spd_line[24];
	static char cad_line[24];

	if (hrm_paired) {
		snprintf(hr_line, sizeof(hr_line), "HR %u bpm", bpm);
		snprintf(rr_line, sizeof(rr_line), "RR %u ms", rr_ms);
	} else {
		snprintf(hr_line, sizeof(hr_line), "HR -- (search)");
		snprintf(rr_line, sizeof(rr_line), "RR --");
	}

	if (bsc_paired) {
		snprintf(spd_line, sizeof(spd_line), "%u km/h", speed_kph);
		snprintf(cad_line, sizeof(cad_line), "CAD %u rpm", cadence_rpm);
	} else {
		snprintf(spd_line, sizeof(spd_line), "SPD -- (search)");
		snprintf(cad_line, sizeof(cad_line), "CAD -- (search)");
	}

	static char alt_line[24];

	/* Whole metres only, same as every other value on this screen (HR,
	 * SPD, CAD are all integers too) -- also sidesteps relying on
	 * snprintf's float formatting under picolibc, which this project has
	 * deliberately avoided elsewhere (see gps_demo.cpp) since it isn't
	 * confirmed supported in this build. */
	if (has_alt) {
		snprintf(alt_line, sizeof(alt_line), "ALT %d m", (int)alt_m);
	} else {
		snprintf(alt_line, sizeof(alt_line), "ALT -- (no fix)");
	}

	gfx_print_centered(hr_line, 100, 3);
	gfx_print_centered(rr_line, 150, 2);
	gfx_print_centered(spd_line, 230, 3);
	gfx_print_centered(cad_line, 280, 2);
	gfx_print_centered(alt_line, 330, 2);

	gfx_push();
#else
	ARG_UNUSED(bpm);
	ARG_UNUSED(rr_ms);
	ARG_UNUSED(hrm_paired);
	ARG_UNUSED(speed_kph);
	ARG_UNUSED(cadence_rpm);
	ARG_UNUSED(bsc_paired);
	ARG_UNUSED(alt_m);
	ARG_UNUSED(has_alt);
#endif
}

uint32_t gfx_demo_show_map(const uint8_t *tile_buf, size_t tile_len, float center_lat,
			    float center_lon)
{
#if DT_HAS_CHOSEN(zephyr_display)
	if (!gfx_ready()) {
		return 0;
	}

	gfx.fillScreen(1);

	uint32_t render_start = k_cycle_get_32();
	uint32_t points_drawn = map_render_tile(gfx, tile_buf, tile_len, center_lat, center_lon);
	uint32_t render_us = k_cyc_to_us_floor32(k_cycle_get_32() - render_start);

	uint32_t push_start = k_cycle_get_32();

	gfx_push();
	uint32_t push_us = k_cyc_to_us_floor32(k_cycle_get_32() - push_start);

	printk("gfx_demo: map render, %u points drawn, render=%u us push=%u us total=%u us\n",
	       points_drawn, render_us, push_us, render_us + push_us);
	return points_drawn;
#else
	ARG_UNUSED(tile_buf);
	ARG_UNUSED(tile_len);
	ARG_UNUSED(center_lat);
	ARG_UNUSED(center_lon);
	return 0;
#endif
}
