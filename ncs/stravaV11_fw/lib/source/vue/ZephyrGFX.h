/*
 * Phase 7: bridges stravaV10's Adafruit_GFX-based UI drawing to the LS027
 * display as it's actually wired up in this port -- Zephyr's upstream
 * sharp,ls0xx driver (Phase 2), via the generic display.h API
 * (display_write()), not stravaV10's own hand-rolled drivers/lcd/ls027.c.
 *
 * This class is deliberately hardware-free (no <zephyr/...> include) so it
 * builds and is testable on native_sim: it only maintains the in-memory
 * framebuffer and implements drawPixel(). Pushing that buffer to a real
 * display via display_write() is the caller's job (see stravaV11_fw, which
 * has the actual display device) -- getBuffer()/getBufferSize() are there
 * for exactly that.
 *
 * Buffer format matches what the ls0xx driver (and its PIXEL_FORMAT_MONO01
 * capability) expects: 1 bit/pixel, LSB-first within each byte, 1 = white,
 * 0 = black -- see zephyr/drivers/display/ls0xx.c's own comment ("Display
 * expects LSB first"). This is NOT the same bit order as stravaV10's own
 * ls027.c (which packed MSB-first via its set[]/clr[] tables) -- ls0xx
 * needing LSB order is exactly the kind of driver-specific detail Phase 2
 * flagged as unverifiable without real glass attached; still true here,
 * for the same reason.
 *
 * Unlike stravaV10's Vue::drawPixel(), this does NOT apply any rotation
 * transform (stravaV10 hardcodes rotation=3 for its physical case mounting
 * -- Adafruit_GFX's own setRotation() is available if/when that's needed,
 * but getting it right needs the real screen to check against, so it's
 * left at the class's natural orientation for now).
 */

#ifndef SOURCE_VUE_ZEPHYRGFX_H_
#define SOURCE_VUE_ZEPHYRGFX_H_

#include "Adafruit_GFX.h"

#define ZEPHYR_GFX_WIDTH  400
#define ZEPHYR_GFX_HEIGHT 240
#define ZEPHYR_GFX_BUFFER_SIZE ((ZEPHYR_GFX_WIDTH / 8) * ZEPHYR_GFX_HEIGHT)

class ZephyrGFX : public Adafruit_GFX {
public:
	ZephyrGFX();

	void drawPixel(int16_t x, int16_t y, uint16_t color) override;
	void fillScreen(uint16_t color) override;

	const uint8_t *getBuffer() const {
		return m_buffer;
	}

	size_t getBufferSize() const {
		return sizeof(m_buffer);
	}

	/* Count of set (white) bits -- cheap way to sanity-check a draw
	 * without needing a real screen to look at (see stravaV11_app's
	 * smoke test). */
	uint32_t countSetPixels() const;

private:
	uint8_t m_buffer[ZEPHYR_GFX_BUFFER_SIZE];
};

#endif /* SOURCE_VUE_ZEPHYRGFX_H_ */
