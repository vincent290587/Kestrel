/*
 * Phase 2/3 DK bring-up smoke test.
 *
 * Validated two different ways:
 *  - LED, button, I2C bus scan: this DK has the first two on-board and
 *    nothing needs to ACK on the bus for the third, so these are real,
 *    observed hardware results.
 *  - LS027 display, bme280, fxos8700, FRAM (mb85rcxx): nothing is wired up
 *    to this DK (see CLAUDE.md and the board overlay), so these only prove
 *    each driver initializes and completes its transactions without
 *    error/hang -- not that any particular reading or pixel is correct.
 *    That check comes once real hardware is wired up (custom PCB phase).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/flash.h>
#if defined(CONFIG_NORDIC_QSPI_NOR)
#include <zephyr/drivers/flash/nrf_qspi_nor.h>
#endif
#include <zephyr/drivers/uart.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#include <zephyr/fs/fs.h>
#include <ff.h>
#endif

#include "ble_demo.h"
#include "ant_demo.h"
#include "hrm_demo.h"
#include "bsc_demo.h"
#include "fec_demo.h"
#include "ant_dm_demo.h"
#include "gfx_demo.h"
#include "gps_demo.h"
#include "map_demo.h"
#include "Locator.h"
#include "sensor_screen_demo.h"
#include "task_demo.h"
#include "power_demo.h"
#include "poll_demo.h"
#include "usb_demo.h"
#include "cmd_console.h"
#include "disk_raw_test.h"
#include "notifications_demo.h"

/*
 * The custom PCB latches its own regulator ON via the STC3100 fuel gauge's
 * IO0 pin: source/sensors/STC3100.cpp's reset()/init() sequence (REG_CONTROL
 * = STC_RESET, then REG_MODE = MODE_RUN) leaves IO0 actively driven, holding
 * the latch; shutdown() (REG_MODE = 0, then REG_CONTROL = STC_IO_OD) releases
 * it to open-drain so the board can power itself off -- see
 * power_scheduler.cpp's real stc.shutdown() call and CLAUDE.md's Phase 8
 * notes on that mechanism. No Zephyr driver for the STC3100 exists yet
 * (Phase 3 gap), so until one does, this replicates only stravaV10's own
 * init() register writes -- just enough to hold power, not the full
 * fuel-gauge driver. Found the hard way: the board powered itself off
 * between Phase 11 test runs before this existed, since nothing was ever
 * talking to the STC3100 at all, and its post-reset default apparently
 * doesn't hold the latch on its own. Runs first, before anything else in
 * main() -- every millisecond without this held is a millisecond the board
 * risks losing power. Harmless on the DK: the same i2c_demo()-scanned bus,
 * just cleanly NACKed since nothing is at this address there.
 */
#define STC3100_I2C_ADDR    0x70
#define STC3100_REG_MODE    0
#define STC3100_REG_CONTROL 1
#define STC3100_MODE_RUN    0x10
#define STC3100_CTRL_RESET  0x02

static void stc3100_power_latch_hold(void)
{
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

	if (!device_is_ready(i2c)) {
		return;
	}

	uint8_t reset_cmd[2] = { STC3100_REG_CONTROL, STC3100_CTRL_RESET };
	int err = i2c_write(i2c, reset_cmd, sizeof(reset_cmd), STC3100_I2C_ADDR);

	if (err) {
		/* No STC3100 on this bus (e.g. the DK) -- nothing to hold. */
		return;
	}

	k_msleep(1);

	uint8_t mode_cmd[2] = { STC3100_REG_MODE, STC3100_MODE_RUN };

	i2c_write(i2c, mode_cmd, sizeof(mode_cmd), STC3100_I2C_ADDR);

	printk("stc3100: power latch held (CONTROL reset, MODE run)\n");
}

/* Diagnostic finding (maps-feasibility real-tile/GPS-sim validation): the
 * board genuinely lost power a few minutes into the heaviest sustained
 * combined workload this project has ever run (real SD-card tile reads
 * every second, concurrent with QSPI/ANT+/BLE/display, all while on
 * stable USB power -- ruling out battery drain). Confirmed via
 * POWER.RESETREAS reading 0 (no warm-reset-reason bits set) immediately
 * after the reboot -- the signature of a genuine power-on reset, not a
 * software crash. stc3100_power_latch_hold() above was, until now, only
 * ever called once, as the very first line of main() -- if the STC3100
 * itself gets disturbed by anything afterward (never actually confirmed
 * what; this is a mitigation, not a root-caused fix), nothing was ever
 * re-asserting the latch, since the original one-shot call already
 * fully accounted for the case explored back then (nothing ever talking
 * to the STC3100 across a whole boot). Re-asserting periodically makes
 * the latch self-healing against a disturbance partway through a run,
 * whatever its cause. */
static void stc3100_power_latch_refresh_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(stc3100_power_latch_refresh_work,
				stc3100_power_latch_refresh_handler);

static void stc3100_power_latch_refresh_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	stc3100_power_latch_hold();
	k_work_schedule(&stc3100_power_latch_refresh_work, K_SECONDS(5));
}

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static void led_button_demo(void)
{
	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		printk("LED or button GPIO device not ready\n");
		return;
	}

	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&button, GPIO_INPUT);

	bool was_pressed = false;

	for (int i = 0; i < 10; i++) {
		gpio_pin_toggle_dt(&led);

		bool pressed = gpio_pin_get_dt(&button) > 0;
		if (pressed != was_pressed) {
			printk("button0: %s\n", pressed ? "pressed" : "released");
			was_pressed = pressed;
		}

		k_msleep(300);
	}
}

static void i2c_demo(void)
{
	/* No sensor is attached (bare DK) -- this only proves the bus
	 * initializes and issues clean transactions (NACKs are expected and
	 * fine; a hang would not be). */
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

	if (!device_is_ready(i2c)) {
		printk("I2C device not ready\n");
		return;
	}

	int nacks = 0, acks = 0;

	for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
		uint8_t dummy;
		int err = i2c_read(i2c, &dummy, 1, addr);

		if (err == 0) {
			acks++;
		} else {
			nacks++;
		}
	}
	printk("I2C scan (arduino_i2c): %d NACKs, %d ACKs (no sensor attached, so ACKs would be unexpected)\n",
	       nacks, acks);
}

static void sensor_demo(const char *name, const struct device *dev)
{
	if (!device_is_ready(dev)) {
		printk("%s: not ready (expected -- nothing attached)\n", name);
		return;
	}

	int err = sensor_sample_fetch(dev);
	printk("%s: ready, sensor_sample_fetch() -> %d\n", name, err);
}

static void fram_demo(void)
{
	const struct device *fram = DEVICE_DT_GET(DT_NODELABEL(fram));

	if (!device_is_ready(fram)) {
		printk("fram: not ready (expected -- nothing attached)\n");
		return;
	}

	printk("fram: ready, size=%zu bytes\n", eeprom_get_size(fram));

	static const uint8_t pattern[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
	uint8_t readback[sizeof(pattern)] = { 0 };

	int err = eeprom_write(fram, 0, pattern, sizeof(pattern));
	printk("eeprom_write() -> %d\n", err);

	err = eeprom_read(fram, 0, readback, sizeof(readback));
	printk("eeprom_read() -> %d, %s\n", err,
	       memcmp(pattern, readback, sizeof(pattern)) == 0 ? "MATCH" : "MISMATCH");
}

static void disk_raw_ioctl_demo(const char *name)
{
	int err = disk_access_ioctl(name, DISK_IOCTL_CTRL_INIT, NULL);

	if (err != 0) {
		printk("disk %s: init -> %d\n", name, err);
		return;
	}

	uint32_t block_count = 0, block_size = 0;
	disk_access_ioctl(name, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
	disk_access_ioctl(name, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);
	printk("disk %s: init ok, %u sectors x %u bytes\n", name, block_count, block_size);

	/* Real write/read/verify round trip, same rigor as qspi_flash_demo()
	 * below -- enumeration alone (sector count/size) doesn't prove data
	 * actually moves correctly. Sector 0 is fine here: confirmed with
	 * the user this card is blank/scratch, not carrying real data. */
	if (block_size == 512) {
		static uint8_t pattern[512];
		static uint8_t readback[512];

		for (size_t i = 0; i < sizeof(pattern); i++) {
			pattern[i] = (uint8_t)i;
		}

		err = disk_access_write(name, pattern, 0, 1);
		printk("disk %s: write() -> %d\n", name, err);

		err = disk_access_read(name, readback, 0, 1);
		printk("disk %s: read() -> %d, %s\n", name, err,
		       memcmp(pattern, readback, sizeof(pattern)) == 0 ? "MATCH" : "MISMATCH");
	} else {
		printk("disk %s: unexpected sector size %u, skipping write/read test\n", name,
		       block_size);
	}

	disk_access_ioctl(name, DISK_IOCTL_CTRL_DEINIT, NULL);
}

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
static void sd_fat_demo(void)
{
	/* Maps-feasibility step 1 (docs/maps_feasibility.md): confirm
	 * CONFIG_FAT_FILESYSTEM_ELM actually mounts on the real SD card via
	 * disk_access, not just raw sectors -- disk_raw_ioctl_demo() above
	 * only proves the block device works, not a filesystem on top of
	 * it. CONFIG_FS_FATFS_MOUNT_MKFS defaults to y, so a card with no
	 * existing FAT filesystem gets formatted automatically on the first
	 * mount attempt -- correct here since disk_raw_ioctl_demo() already
	 * overwrites sector 0 (where a FAT boot sector would live) with
	 * test data every boot, and the user confirmed this card is
	 * blank/scratch during the FRAM/SD/NOR round-trip validation. */
	static FATFS fat_fs;
	static struct fs_mount_t mp = {
		.type = FS_FATFS,
		.fs_data = &fat_fs,
		.mnt_point = "/SD:",
	};

	int err = fs_mount(&mp);

	printk("sd_fat: fs_mount(\"/SD:\") -> %d\n", err);
	if (err != 0) {
		return;
	}

	/* Zephyr's own fs_sample does this same unmount/remount cycle right
	 * after a successful mount, before any file I/O -- needed so a
	 * volume that fs_mount() just auto-mkfs'd (CONFIG_FS_FATFS_MOUNT_MKFS)
	 * is cleanly re-read from disk rather than continuing to operate on
	 * whatever in-memory state the mkfs call itself left behind. */
	fs_unmount(&mp);
	err = fs_mount(&mp);
	printk("sd_fat: remount -> %d\n", err);
	if (err != 0) {
		return;
	}

	/* Real round-trip, same rigor as every other storage test in this
	 * file: write a file, close it, reopen, read back, compare. */
	static const char test_data[] = "stravaV11 SD FAT test\n";
	char readback[sizeof(test_data)] = { 0 };
	struct fs_file_t file;

	fs_file_t_init(&file);
	err = fs_open(&file, "/SD:/stravav11_test.txt", FS_O_CREATE | FS_O_WRITE);
	printk("sd_fat: fs_open(write) -> %d\n", err);
	if (err == 0) {
		ssize_t written = fs_write(&file, test_data, sizeof(test_data));

		printk("sd_fat: fs_write() -> %d\n", (int)written);
		fs_close(&file);
	}

	fs_file_t_init(&file);
	err = fs_open(&file, "/SD:/stravav11_test.txt", FS_O_READ);
	printk("sd_fat: fs_open(read) -> %d\n", err);
	if (err == 0) {
		ssize_t bytes_read = fs_read(&file, readback, sizeof(readback));

		fs_close(&file);
		printk("sd_fat: fs_read() -> %d, %s\n", (int)bytes_read,
		       memcmp(test_data, readback, sizeof(test_data)) == 0 ? "MATCH"
									     : "MISMATCH");
	}

	struct fs_dir_t dir;
	struct fs_dirent entry;
	int count = 0;

	fs_dir_t_init(&dir);
	err = fs_opendir(&dir, "/SD:");
	printk("sd_fat: fs_opendir(\"/SD:\") -> %d\n", err);
	if (err == 0) {
		while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
			printk("sd_fat:   %s %s (%zu bytes)\n",
			       entry.type == FS_DIR_ENTRY_DIR ? "[DIR] " : "[FILE]",
			       entry.name, entry.size);
			count++;
		}
		fs_closedir(&dir);
		printk("sd_fat: %d entries in root\n", count);
	}

	fs_unmount(&mp);
}
#endif /* CONFIG_FAT_FILESYSTEM_ELM */

static void qspi_flash_demo(void)
{
	/* Real, populated hardware: erase/write/read/verify directly against
	 * the DK's on-board mx25r64 QSPI NOR chip (see the overlay for why
	 * this is the direct flash API rather than disk_access/FAT). On the
	 * real PCB, this node is the board's actual external chip -- labeled
	 * mx25r64 for main.c source-compat (see the board .dts), but the
	 * jedec-id configured there (20 ba 18) is Micron's, matching
	 * libraries/SST/mt25.c's own read_id, not an SST/Microchip part
	 * (manufacturer byte would be 0xBF, not 0x20) despite the
	 * "libraries/SST" directory name -- confirmed below by reading the
	 * chip's actual ID back, not just trusting the devicetree config
	 * (which nrf_qspi_nor.c's own init already hard-validates: a
	 * mismatch returns -ENODEV and device_is_ready() would be false). */
	const struct device *flash = DEVICE_DT_GET(DT_NODELABEL(mx25r64));

	if (!device_is_ready(flash)) {
		printk("mx25r64: not ready\n");
		return;
	}

	uint8_t jedec_id[3] = { 0 };
	int jedec_err = flash_read_jedec_id(flash, jedec_id);

	printk("mx25r64: flash_read_jedec_id() -> %d, id=%02x %02x %02x\n", jedec_err,
	       jedec_id[0], jedec_id[1], jedec_id[2]);

	static const uint8_t pattern[16] = {
		0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22, 0x33,
		0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB,
	};
	uint8_t readback[sizeof(pattern)] = { 0 };

	int err = flash_erase(flash, 0, 4096);
	printk("mx25r64: flash_erase() -> %d\n", err);

	err = flash_write(flash, 0, pattern, sizeof(pattern));
	printk("mx25r64: flash_write() -> %d\n", err);

	err = flash_read(flash, 0, readback, sizeof(readback));
	printk("mx25r64: flash_read() -> %d, %s\n", err,
	       memcmp(pattern, readback, sizeof(pattern)) == 0 ? "MATCH" : "MISMATCH");
}

/* nRF52840's QSPI peripheral can map the external NOR chip's contents
 * straight onto the CPU's memory bus for reads (XIP -- execute/access in
 * place) instead of going through flash_read()'s SPI command sequence
 * each time. There's no devicetree node for this window (Zephyr's QSPI
 * NOR driver only models the chip as a flash_read()/flash_write() device,
 * not a memory-mapped one) -- the address below is Nordic's own
 * NRF_MEMORY_EXTFLASH_BASE (nrfx's nrf52840_xxaa_memory.h), not something
 * discoverable from devicetree, hence hardcoded here with this comment as
 * the paper trail. Same address on both boards: it's an nRF52840 SoC
 * property, not a per-board pin/wiring detail. */
#define QSPI_XIP_BASE_ADDR 0x12000000UL
#define QSPI_XIP_TEST_SIZE (256 * 1024)
#define QSPI_XIP_CHUNK_SIZE 256

static uint8_t qspi_xip_test_pattern_byte(uint32_t offset)
{
	/* A multiplicative-hash pseudorandom byte per offset, not a trivial
	 * incrementing/repeating pattern -- deliberately gives every byte
	 * position its own bit pattern so a stuck bit or a
	 * misaddressed/aliased region anywhere across the 256KB span would
	 * show up as a mismatch instead of hiding behind a repeated value. */
	return (uint8_t)((offset * 2654435761u) >> 24);
}

static void qspi_xip_demo(const struct device *flash)
{
#if defined(CONFIG_NORDIC_QSPI_NOR)
	static uint8_t chunk[QSPI_XIP_CHUNK_SIZE] __aligned(4);
	int err;

	err = flash_erase(flash, 0, QSPI_XIP_TEST_SIZE);
	printk("mx25r64: xip test flash_erase(%u) -> %d\n", QSPI_XIP_TEST_SIZE, err);
	if (err != 0) {
		return;
	}

	for (uint32_t off = 0; off < QSPI_XIP_TEST_SIZE; off += sizeof(chunk)) {
		for (uint32_t i = 0; i < sizeof(chunk); i++) {
			chunk[i] = qspi_xip_test_pattern_byte(off + i);
		}
		err = flash_write(flash, off, chunk, sizeof(chunk));
		if (err != 0) {
			printk("mx25r64: xip test flash_write() failed at %u -> %d\n", off, err);
			return;
		}
	}
	printk("mx25r64: xip test wrote %u bytes via flash_write()\n", QSPI_XIP_TEST_SIZE);

	/* Baseline via the normal command-based API first -- if this doesn't
	 * match, the bug isn't in XIP/memory-mapping. */
	uint32_t normal_mismatches = 0;

	for (uint32_t off = 0; off < QSPI_XIP_TEST_SIZE; off += sizeof(chunk)) {
		err = flash_read(flash, off, chunk, sizeof(chunk));
		if (err != 0) {
			printk("mx25r64: xip test flash_read() failed at %u -> %d\n", off, err);
			return;
		}
		for (uint32_t i = 0; i < sizeof(chunk); i++) {
			if (chunk[i] != qspi_xip_test_pattern_byte(off + i)) {
				normal_mismatches++;
			}
		}
	}
	printk("mx25r64: xip test normal-API readback: %u/%u byte mismatches\n",
	       normal_mismatches, QSPI_XIP_TEST_SIZE);

	/* Now the actual point of this test: enable memory-mapped mode and
	 * read the exact same region straight off the CPU's memory bus,
	 * byte by byte, instead of through flash_read(). */
	nrf_qspi_nor_xip_enable(flash, true);

	const volatile uint8_t *xip = (const volatile uint8_t *)QSPI_XIP_BASE_ADDR;
	uint32_t xip_mismatches = 0;

	for (uint32_t off = 0; off < QSPI_XIP_TEST_SIZE; off++) {
		if (xip[off] != qspi_xip_test_pattern_byte(off)) {
			xip_mismatches++;
		}
	}

	nrf_qspi_nor_xip_enable(flash, false);

	printk("mx25r64: xip memory-mapped readback: %u/%u byte mismatches -> %s\n",
	       xip_mismatches, QSPI_XIP_TEST_SIZE,
	       (normal_mismatches == 0 && xip_mismatches == 0) ? "MATCH" : "MISMATCH");
#else
	ARG_UNUSED(flash);
	printk("mx25r64: xip test skipped (CONFIG_NORDIC_QSPI_NOR not enabled)\n");
#endif
}

/* Not called from storage_demo() anymore -- disk_raw_ioctl_demo()'s own
 * write/read round trip lands on sector 0, which for "SD" is the FAT boot
 * sector. Real, persistent map-tile data on the SD card means that
 * corrupting the boot sector on *every boot* (which is what this was
 * doing) forces CONFIG_FS_FATFS_MOUNT_MKFS to silently reformat the card
 * fresh right after, wiping it -- confirmed as the real cause of two
 * separate "the tile files vanished" incidents in the same session, not
 * the RTT/sd_stress issues that were also found and fixed alongside it.
 * Gated behind cmd_console.c's "DISK TEST" command instead -- exported
 * here (declared in disk_raw_test.h) rather than moved to its own file,
 * since disk_raw_ioctl_demo() itself is small and only used from main.c. */
void disk_raw_test_start(void)
{
	disk_raw_ioctl_demo("SD");
	disk_raw_ioctl_demo("NOR");
}

static void storage_demo(void)
{
#if defined(CONFIG_FAT_FILESYSTEM_ELM)
	sd_fat_demo();
#endif

	qspi_flash_demo();

	const struct device *qspi_flash = DEVICE_DT_GET(DT_NODELABEL(mx25r64));

	if (device_is_ready(qspi_flash)) {
		qspi_xip_demo(qspi_flash);
	}
}

#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), gps_reset_gpios)
/* GPS_R/GPS_S/FIX_PIN (custom_board_v3.h) -- only defined on the real PCB's
 * board files (stravav11_nrf52840.dts), not the DK, since the DK has no
 * counterpart pins for them. GPS_R/GPS_S are active-low (TDD/Simulator.cpp
 * only emits NMEA data once both read high), so "released" (module out of
 * reset/standby, its normal running state) means driving both to their
 * GPIO_DT_SPEC-relative INACTIVE level -- physically high. Previously left
 * floating, which is the likely reason the GPS UART received unexplained
 * bytes on every boot (Phase 11): nothing ever held the module in a known
 * state either way. FIX_PIN is active-high, GPS-to-MCU. */
static const struct gpio_dt_spec gps_reset =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_reset_gpios);
static const struct gpio_dt_spec gps_standby =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_standby_gpios);
static const struct gpio_dt_spec gps_fix =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gps_fix_gpios);

static void gps_pins_release(void)
{
	if (!gpio_is_ready_dt(&gps_reset) || !gpio_is_ready_dt(&gps_standby) ||
	    !gpio_is_ready_dt(&gps_fix)) {
		printk("gps: reset/standby/fix GPIO device not ready\n");
		return;
	}

	gpio_pin_configure_dt(&gps_reset, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&gps_standby, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&gps_fix, GPIO_INPUT);

	printk("gps: reset/standby released, fix pin reads %d\n", gpio_pin_get_dt(&gps_fix));
}
#else
static void gps_pins_release(void)
{
}
#endif

static void uart_demo(void)
{
	/* Phase 6: GPS module UART (arduino_serial/uart1, see the overlay).
	 * On the DK, no GPS module is attached -- this only proves TX
	 * completes and RX doesn't hang, not that anything is received. On
	 * the real PCB (Phase 11), gps_pins_release() below takes the module
	 * out of reset/standby first, so RX bytes here are real. Every
	 * received byte is also fed through locator_encode_char() (Phase 6's
	 * TinyGPS++-based Locator, ported unmodified from stravaV11_app) --
	 * see gps_demo_report() below for what it decoded. */
	gps_pins_release();
	gps_demo_init();

	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(arduino_serial));

	if (!device_is_ready(uart)) {
		printk("arduino_serial: not ready\n");
		return;
	}

	static const char msg[] = "$PMTK220,200*2C\r\n"; /* stravaV10's actual fix-interval cmd format */

	for (size_t i = 0; i < sizeof(msg) - 1; i++) {
		uart_poll_out(uart, msg[i]);
	}
	printk("arduino_serial: TX complete (%u bytes)\n", (unsigned)(sizeof(msg) - 1));

	/* Capture and print the actual bytes, not just a count: on the real
	 * PCB (Phase 11) something answers here even though nothing decodes
	 * it yet. Polls for a wall-clock window, not a fixed iteration count
	 * -- uart_poll_in() is non-blocking, and a fixed spin-count can
	 * finish before a real 9600-baud stream (~73ms for a ~70-byte NMEA
	 * sentence) has had time to arrive. */
	static uint8_t rx_buf[256];
	int rx_count = 0;
	int64_t deadline = k_uptime_get() + 2000;

	while (k_uptime_get() < deadline && rx_count < (int)sizeof(rx_buf)) {
		unsigned char rx;

		if (uart_poll_in(uart, &rx) == 0) {
			rx_buf[rx_count++] = rx;
			locator_encode_char((char)rx);
		} else {
			/* Only sleep when idle, not while bytes are actively
			 * arriving: a tight 2s CPU-bound spin here starved the
			 * deferred-log thread of any chance to run, which
			 * dropped several other boot messages queued during
			 * this window (RTT's own buffering is unrelated --
			 * this is Zephyr's separate deferred-log message pool
			 * filling up because nothing could drain it). */
			k_msleep(1);
		}
	}

	/* Build the whole dump into one buffer and printk() it once -- a
	 * separate printk() per byte flooded the deferred log queue (each one
	 * queued as its own message) badly enough that a first attempt at
	 * this logged "258 messages dropped", losing everything else that
	 * boot was trying to log at the same time. */
	static char dump[4 * sizeof(rx_buf) + 1];
	size_t dump_len = 0;

	for (int i = 0; i < rx_count && dump_len + 5 < sizeof(dump); i++) {
		if (rx_buf[i] >= 0x20 && rx_buf[i] < 0x7f) {
			dump[dump_len++] = (char)rx_buf[i];
		} else {
			dump_len += snprintf(&dump[dump_len], 5, "\\x%02x", rx_buf[i]);
		}
	}
	dump[dump_len] = '\0';

	printk("arduino_serial: RX got %d bytes over 2s: \"%s\"\n", rx_count, dump);

	gps_demo_report();

	/* Give the deferred-log thread a moment to actually drain those two
	 * lines before ant_demo_start() immediately queues a burst of its
	 * own -- without this, one of them was the one getting dropped. */
	k_msleep(5);
}

#if DT_HAS_CHOSEN(zephyr_display)
static void display_demo(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (!device_is_ready(disp)) {
		printk("Display device not ready\n");
		return;
	}

	struct display_capabilities caps;
	display_get_capabilities(disp, &caps);
	printk("Display: %ux%u, pixel format 0x%x\n",
	       caps.x_resolution, caps.y_resolution, caps.current_pixel_format);

	static uint8_t buf[400 / 8 * 240];
	for (int row = 0; row < 240; row++) {
		memset(&buf[row * (400 / 8)], (row & 1) ? 0x00 : 0xFF, 400 / 8);
	}

	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(buf),
		.width = 400,
		.height = 240,
		.pitch = 400,
	};

	int err = display_write(disp, 0, 0, &desc, buf);
	printk("display_write() -> %d\n", err);
}
#else
static void display_demo(void)
{
	printk("No zephyr,display chosen for this board\n");
}
#endif

int main(void)
{
	stc3100_power_latch_hold();
	k_work_schedule(&stc3100_power_latch_refresh_work, K_SECONDS(5));

	printk("=== stravaV11 Phase 2/3 DK bring-up ===\n");

	led_button_demo();
	i2c_demo();
	sensor_demo("bme280", DEVICE_DT_GET(DT_NODELABEL(bme280)));
	sensor_demo("fxos8700", DEVICE_DT_GET(DT_NODELABEL(fxos8700)));
	fram_demo();
	storage_demo();
	display_demo();
	gfx_demo();
	uart_demo(); /* also calls gps_demo_init() -- Locator needs this regardless
		      * of whether real NMEA bytes or gps_sim's injection feeds it. */
	ant_demo_start();
	hrm_demo_start();
	bsc_demo_start();
	fec_demo_start();
	ant_dm_demo_start();
	ble_demo_start();
	task_demo_start();
	power_demo_start();
	poll_demo_start();
	notifications_demo_start();
	usb_demo_start();

	/* sd_stress_demo and map_screen_demo both do real, recurring SD-card
	 * I/O that competes with USB MSC host access (see cmd_console.c) --
	 * neither auto-starts anymore. cmd_console_start() just arms the
	 * shared RTT+CDC-ACM command listener (covers gps_sim_demo's "SIM
	 * START" too, not just the SD-heavy ones); nothing it gates actually
	 * runs until the matching command is sent. */
	cmd_console_start();

	printk("=== bring-up smoke test done ===\n");
	return 0;
}
