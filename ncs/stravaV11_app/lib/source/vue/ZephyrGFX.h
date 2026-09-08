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
 * Rotation: drawPixel() applies Adafruit_GFX's standard per-driver rotation
 * transform (WIDTH/HEIGHT are the raw physical 400x240 panel dims; _width/
 * _height are the current rotation-adjusted logical ones set by
 * setRotation()). stravaV10 hardcodes rotation=3 for its physical case
 * mounting; confirmed against the real board that this board's application
 * wants portrait too, so callers should setRotation(1) or (3) as
 * appropriate rather than relying on the class's native landscape 400x240.
 *
 * GFX port Phase B (2026-09-08): added buffer-native drawFastVLine()/
 * drawFastHLine() overrides. Adafruit_GFX's own fillRect() is just a loop
 * of drawFastVLine() calls per column (confirmed by reading
 * Adafruit_GFX.cpp directly, same as stravaV10's own Vue::fillRect() ->
 * drawFastVLine() -> drawPixelGroup() strategy) -- overriding
 * drawFastVLine()/drawFastHLine() alone, without also needing a separate
 * fillRect() override, makes every caller of fillRect()/drawFastVLine()/
 * drawFastHLine() buffer-native automatically. Without this, those calls
 * fall through to Adafruit_GFX's default per-pixel-loop implementation --
 * fine for the sparse polyline drawing map_render.cpp already validated,
 * but a real risk for the fillRect-heavy screens (histogram bars,
 * notification banners, full-screen clears) the actual UI port needs.
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
	void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
	void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
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

	/* Rotation transform factored out of drawPixel() so
	 * drawFastVLine()/drawFastHLine() apply the exact same logic instead
	 * of risking the two silently drifting apart. Same case-by-case
	 * transform as before, just returning the result instead of writing
	 * straight to the buffer. */
	void logicalToBuffer(int16_t x, int16_t y, int16_t *bx, int16_t *by) const;

	/* Shared by drawFastVLine()/drawFastHLine(): after rotation, a
	 * logical axis-aligned span lands as either a horizontal or a
	 * vertical run in buffer space (rotation by a multiple of 90 degrees
	 * preserves axis alignment) -- this dispatches to whichever one it
	 * turns out to be. lx0<=lx1, ly0<=ly1 not required by the caller;
	 * this handles both orderings. */
	void drawFastLineSpan(int16_t lx0, int16_t ly0, int16_t lx1, int16_t ly1, uint16_t color);

	/* Low-level buffer-space span fills -- bx0<=bx1 / by0<=by1 required
	 * (callers normalize). Byte-aligned bulk set/clear for the
	 * horizontal case (the common one at this port's validated
	 * rotation=3, where a logical vertical line becomes a horizontal
	 * buffer run); a tight per-row loop for the vertical case, still
	 * avoiding drawPixel()'s per-call bounds-check+rotation-switch
	 * overhead. */
	void fillBufferHSpan(int16_t by, int16_t bx0, int16_t bx1, uint16_t color);
	void fillBufferVSpan(int16_t bx, int16_t by0, int16_t by1, uint16_t color);
};

#endif /* SOURCE_VUE_ZEPHYRGFX_H_ */
