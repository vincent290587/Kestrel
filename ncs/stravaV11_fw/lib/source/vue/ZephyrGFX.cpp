#include "ZephyrGFX.h"

#include <string.h>

ZephyrGFX::ZephyrGFX() : Adafruit_GFX(ZEPHYR_GFX_WIDTH, ZEPHYR_GFX_HEIGHT)
{
	/* 1 = white per the ls0xx driver's convention -- start on a blank
	 * (white) screen, same as stravaV10 clearing to white/black on init. */
	memset(m_buffer, 0xFF, sizeof(m_buffer));
}

void ZephyrGFX::logicalToBuffer(int16_t x, int16_t y, int16_t *bx, int16_t *by) const
{
	/* Adafruit_GFX applies rotation per-driver, inside drawPixel() --
	 * WIDTH/HEIGHT are the raw physical panel dims (never change);
	 * _width/_height are the current, rotation-adjusted logical ones.
	 * Transform logical (x,y) into physical panel coordinates -- standard
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

	*bx = x;
	*by = y;
}

void ZephyrGFX::drawPixel(int16_t x, int16_t y, uint16_t color)
{
	if (x < 0 || x >= _width || y < 0 || y >= _height) {
		return;
	}

	int16_t bx, by;

	logicalToBuffer(x, y, &bx, &by);

	const uint32_t byte_index = (uint32_t)by * (ZEPHYR_GFX_WIDTH / 8) + (bx / 8);
	const uint8_t bit_mask = (uint8_t)(1u << (bx % 8)); /* LSB-first */

	if (color) {
		m_buffer[byte_index] |= bit_mask;
	} else {
		m_buffer[byte_index] &= (uint8_t)~bit_mask;
	}
}

void ZephyrGFX::fillBufferHSpan(int16_t by, int16_t bx0, int16_t bx1, uint16_t color)
{
	uint8_t *row = &m_buffer[(uint32_t)by * (ZEPHYR_GFX_WIDTH / 8)];
	int16_t byte0 = bx0 / 8;
	int16_t byte1 = bx1 / 8;
	bool set = color != 0;

	if (byte0 == byte1) {
		uint8_t mask = (uint8_t)((0xFFu << (bx0 % 8)) & (0xFFu >> (7 - (bx1 % 8))));

		if (set) {
			row[byte0] |= mask;
		} else {
			row[byte0] &= (uint8_t)~mask;
		}
		return;
	}

	uint8_t first_mask = (uint8_t)(0xFFu << (bx0 % 8));

	if (set) {
		row[byte0] |= first_mask;
	} else {
		row[byte0] &= (uint8_t)~first_mask;
	}

	if (byte1 > byte0 + 1) {
		memset(&row[byte0 + 1], set ? 0xFF : 0x00, (size_t)(byte1 - byte0 - 1));
	}

	uint8_t last_bits = (uint8_t)((bx1 % 8) + 1);
	uint8_t last_mask = (uint8_t)((1u << last_bits) - 1u);

	if (set) {
		row[byte1] |= last_mask;
	} else {
		row[byte1] &= (uint8_t)~last_mask;
	}
}

void ZephyrGFX::fillBufferVSpan(int16_t bx, int16_t by0, int16_t by1, uint16_t color)
{
	int16_t byte_col = bx / 8;
	uint8_t bit_mask = (uint8_t)(1u << (bx % 8));
	bool set = color != 0;

	for (int16_t y = by0; y <= by1; y++) {
		uint8_t *b = &m_buffer[(uint32_t)y * (ZEPHYR_GFX_WIDTH / 8) + byte_col];

		if (set) {
			*b |= bit_mask;
		} else {
			*b &= (uint8_t)~bit_mask;
		}
	}
}

void ZephyrGFX::drawFastLineSpan(int16_t lx0, int16_t ly0, int16_t lx1, int16_t ly1, uint16_t color)
{
	int16_t bx0, by0, bx1, by1;

	logicalToBuffer(lx0, ly0, &bx0, &by0);
	logicalToBuffer(lx1, ly1, &bx1, &by1);

	if (by0 == by1) {
		fillBufferHSpan(by0, bx0 < bx1 ? bx0 : bx1, bx0 < bx1 ? bx1 : bx0, color);
	} else {
		/* bx0 must equal bx1 here -- a 90-degree-multiple rotation of an
		 * axis-aligned logical span always lands axis-aligned in buffer
		 * space too. */
		fillBufferVSpan(bx0, by0 < by1 ? by0 : by1, by0 < by1 ? by1 : by0, color);
	}
}

void ZephyrGFX::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color)
{
	/* Same clipping a per-pixel drawPixel() loop would have done silently,
	 * just done once up front instead of once per pixel. */
	if (x < 0 || x >= _width || h <= 0) {
		return;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (y + h > _height) {
		h = _height - y;
	}
	if (h <= 0) {
		return;
	}

	drawFastLineSpan(x, y, x, y + h - 1, color);
}

void ZephyrGFX::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color)
{
	if (y < 0 || y >= _height || w <= 0) {
		return;
	}
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (x + w > _width) {
		w = _width - x;
	}
	if (w <= 0) {
		return;
	}

	drawFastLineSpan(x, y, x + w - 1, y, color);
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
