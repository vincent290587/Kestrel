#include "ZephyrGFX.h"

#include <string.h>

ZephyrGFX::ZephyrGFX() : Adafruit_GFX(ZEPHYR_GFX_WIDTH, ZEPHYR_GFX_HEIGHT)
{
	/* 1 = white per the ls0xx driver's convention -- start on a blank
	 * (white) screen, same as stravaV10 clearing to white/black on init. */
	memset(m_buffer, 0xFF, sizeof(m_buffer));
}

void ZephyrGFX::drawPixel(int16_t x, int16_t y, uint16_t color)
{
	if (x < 0 || x >= _width || y < 0 || y >= _height) {
		return;
	}

	/* Adafruit_GFX applies rotation per-driver, inside drawPixel() --
	 * WIDTH/HEIGHT are the raw physical panel dims (never change);
	 * _width/_height are the current, rotation-adjusted logical ones
	 * (already checked above). Transform logical (x,y) into physical
	 * panel coordinates before writing to the buffer -- standard
	 * Adafruit_GFX per-driver pattern. stravaV10 hardcodes rotation=3 for
	 * its case mounting; confirmed against the real board that this
	 * board's application also wants portrait (rotation 1 or 3, not the
	 * panel's native landscape 0/2). */
	switch (rotation) {
	case 1:
		adagfxswap(x, y);
		x = WIDTH - x - 1;
		break;
	case 2:
		x = WIDTH - x - 1;
		y = HEIGHT - y - 1;
		break;
	case 3:
		adagfxswap(x, y);
		y = HEIGHT - y - 1;
		break;
	default:
		break;
	}

	const uint32_t byte_index = (uint32_t)y * (ZEPHYR_GFX_WIDTH / 8) + (x / 8);
	const uint8_t bit_mask = (uint8_t)(1u << (x % 8)); /* LSB-first */

	if (color) {
		m_buffer[byte_index] |= bit_mask;
	} else {
		m_buffer[byte_index] &= (uint8_t)~bit_mask;
	}
}

void ZephyrGFX::fillScreen(uint16_t color)
{
	memset(m_buffer, color ? 0xFF : 0x00, sizeof(m_buffer));
}

uint32_t ZephyrGFX::countSetPixels() const
{
	uint32_t count = 0;

	for (size_t i = 0; i < sizeof(m_buffer); i++) {
		uint8_t byte = m_buffer[i];

		while (byte) {
			count += (byte & 1);
			byte >>= 1;
		}
	}

	return count;
}
