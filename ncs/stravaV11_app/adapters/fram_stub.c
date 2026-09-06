/*
 * Placeholder for source/sensors/fram.c (not ported yet -- no FRAM driver
 * exists under Zephyr for this chip). UserSettings::sync()/writeConfig()
 * call these; until the FRAM driver phase, settings just don't persist
 * (isConfigValid() will always fail its CRC check, so enforceConfigVersion()
 * falls back to resetConfig()'s in-memory defaults).
 */

#include "fram.h"

void fram_init_sensor(void)
{
}

bool fram_read_block(uint16_t block_addr, uint8_t *readout, uint16_t length)
{
	(void)block_addr;
	(void)readout;
	(void)length;
	return false;
}

bool fram_write_block(uint16_t block_addr, uint8_t *writeout, uint16_t length)
{
	(void)block_addr;
	(void)writeout;
	(void)length;
	return false;
}
