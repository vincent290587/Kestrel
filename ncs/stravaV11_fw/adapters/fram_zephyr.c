/*
 * Real fram.c replacement -- UserSettings::sync()/writeConfig() call these.
 * Unlike stravaV11_app's adapters/fram_stub.c (which always returns false,
 * since stravaV11_app has no FRAM driver at all), this talks to the real
 * fujitsu,mb85rcxx device at devicetree nodelabel "fram" -- the same chip
 * main.c's own fram_demo() has driven and validated (real write/read/MATCH
 * round trip on the custom PCB) since Phase 3/11. block_addr is passed
 * straight through as a Zephyr eeprom API offset: the mb85rcxx driver's own
 * mb85rcxx_translate_address() folds high address bits into the low bits of
 * the 7-bit I2C address exactly the way stravaV10's original fram.c did by
 * hand (confirmed line-by-line against source in Phase 3), so no address
 * translation is needed here.
 */

#include "fram.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/eeprom.h>

void fram_init_sensor(void)
{
}

bool fram_read_block(uint16_t block_addr, uint8_t *readout, uint16_t length)
{
	const struct device *fram = DEVICE_DT_GET(DT_NODELABEL(fram));

	if (!device_is_ready(fram)) {
		return false;
	}

	return eeprom_read(fram, block_addr, readout, length) == 0;
}

bool fram_write_block(uint16_t block_addr, uint8_t *writeout, uint16_t length)
{
	const struct device *fram = DEVICE_DT_GET(DT_NODELABEL(fram));

	if (!device_is_ready(fram)) {
		return false;
	}

	return eeprom_write(fram, block_addr, writeout, length) == 0;
}
