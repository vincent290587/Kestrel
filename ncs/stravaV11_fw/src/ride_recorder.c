/*
 * See ride_recorder.h for the design overview. This file implements:
 *  - the FRAM ride-state struct (5 slots' worth) that is the sole source
 *    of truth for resume, superseding the old .noinit-RAM approach --
 *    survives real power loss, not just a warm reset;
 *  - the NOR-side record writer: a raw, filesystem-free append log inside
 *    ride_storage_partition (stravav11_nrf52840.dts), one 2MB slot per
 *    ride. Only ever holds `record` messages, written once-definition-
 *    then-fixed-size (this profile's FIT_RECORD_MESG_DEF_SIZE +
 *    FIT_RECORD_MESG_SIZE once, then FIT_HDR_SIZE + FIT_RECORD_MESG_SIZE
 *    per sample after that) -- deliberately not a technically-valid FIT
 *    file on its own; NOR flash can only clear bits on program (never set
 *    them back to 1 without a sector erase), so the classic "patch the
 *    header's data_size in place as you go" trick from stravaV10's own
 *    fit_encode.cpp -- fine on SD/FAT, whose own FTL hides that -- simply
 *    doesn't work here. Finalizing into a real, valid .FIT file only
 *    happens once, straight to the SD card, in ride_export_slot_to_sd().
 *  - the SD-side finalize/export: a single pass (file_id/creator/start
 *    event, the whole NOR record stream copied verbatim via the XIP
 *    memory-mapped read path already validated in Phase 11's
 *    qspi_xip_demo(), then lap/stop-event/session/activity/crc) --
 *    possible in one pass, no placeholder-then-patch step, because the
 *    final data_size is always known in advance (fixed one-time message
 *    sizes + FRAM's own write_cursor for that slot).
 */

#include "ride_recorder.h"

#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/nrf_qspi_nor.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <ff.h>
#include <zephyr/logging/log.h>

#include "fit_product.h" /* pulls in fit_config.h + fit_example.h, the actual
			  * message struct/enum definitions -- fit.h alone is
			  * only the generic protocol layer. */
#include "fit.h"
#include "fit_crc.h"
#include "gps_demo.h"
#include "hrm_demo.h"
#include "bsc_demo.h"

LOG_MODULE_REGISTER(ride_recorder, LOG_LEVEL_INF);

/* Same physical/protocol address every other QSPI XIP user in this port
 * (main.c's qspi_xip_demo()) uses -- Nordic's NRF_MEMORY_EXTFLASH_BASE,
 * not discoverable from devicetree. See that file's own comment for the
 * paper trail; duplicated here rather than shared since it's a one-line
 * SoC constant, not board-specific config. */
#define QSPI_XIP_BASE_ADDR 0x12000000UL

#define RIDE_SECTOR_SIZE 4096

/* Well clear of UserSettings' own 24-byte struct at FRAM_SETTINGS_ADDRESS
 * (0x0000) -- see UserSettings.h. FRAM is 2048 bytes total (mb85rc@50),
 * so this leaves enormous headroom on both sides. */
#define RIDE_FRAM_ADDRESS 0x0040
#define RIDE_FRAM_MAGIC 0x52494445UL /* "RIDE" as bytes, arbitrary but distinctive */

typedef enum {
	eRideSlotFree = 0,
	eRideSlotRecording = 1,
	eRideSlotPendingExport = 2,
} eRideSlotState;

typedef struct __attribute__((packed)) {
	uint8_t state; /* eRideSlotState */
	uint32_t start_timestamp;
	uint32_t write_cursor; /* bytes of record-stream written in this slot */
	uint32_t last_timestamp;
	float distance_m;
	float climb_m;
	uint32_t nb_records;
} sRideSlotState;

typedef struct __attribute__((packed)) {
	uint32_t magic;
	sRideSlotState slots[RIDE_NUM_SLOTS];
	uint8_t next_slot_hint; /* round-robin start point for wear-spreading */
	uint8_t crc;
} sRideFramState;

static sRideFramState s_state;
static int8_t s_active_slot = -1; /* -1 = none active this boot */

/* Previous GPS fix, for the 1Hz tick's own distance/climb delta calc
 * (ride_recorder_add_sample() takes deltas, not running totals -- see
 * ride_recorder.h). Reset whenever a ride starts (fresh or resumed) so
 * the first post-start/post-resume sample never computes a spurious
 * jump from stale state. */
static bool s_have_prev_fix;
static float s_prev_lat, s_prev_lon, s_prev_alt;

static uint8_t ride_calculate_crc(const uint8_t *addr, uint16_t len)
{
	uint8_t crc = 0;

	for (uint16_t i = 0; i < len; i++) {
		uint8_t inbyte = addr[i];

		for (uint8_t j = 0; j < 8; j++) {
			uint8_t mix = (crc ^ inbyte) & 0x01;

			crc >>= 1;
			if (mix) {
				crc ^= 0x8C;
			}
			inbyte >>= 1;
		}
	}

	return crc;
}

static bool ride_fram_load(void)
{
	const struct device *fram = DEVICE_DT_GET(DT_NODELABEL(fram));

	if (!device_is_ready(fram)) {
		return false;
	}

	if (eeprom_read(fram, RIDE_FRAM_ADDRESS, &s_state, sizeof(s_state)) != 0) {
		return false;
	}

	uint8_t crc = ride_calculate_crc((const uint8_t *)&s_state,
					  sizeof(s_state) - sizeof(s_state.crc));

	if (s_state.magic != RIDE_FRAM_MAGIC || crc != s_state.crc) {
		return false;
	}

	return true;
}

static bool ride_fram_save(void)
{
	const struct device *fram = DEVICE_DT_GET(DT_NODELABEL(fram));

	if (!device_is_ready(fram)) {
		return false;
	}

	s_state.magic = RIDE_FRAM_MAGIC;
	s_state.crc = ride_calculate_crc((const uint8_t *)&s_state,
					  sizeof(s_state) - sizeof(s_state.crc));

	return eeprom_write(fram, RIDE_FRAM_ADDRESS, &s_state, sizeof(s_state)) == 0;
}

static void ride_fram_reset(void)
{
	memset(&s_state, 0, sizeof(s_state));
	s_state.magic = RIDE_FRAM_MAGIC;
}

static const struct device *ride_qspi_dev(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(mx25r64));
}

static uint32_t ride_slot_offset(uint8_t slot_index)
{
	/* Partition Manager is active for this build (USE_PARTITION_MANAGER=1),
	 * which overrides FIXED_PARTITION_OFFSET() to resolve via its own
	 * PM_<NAME>_OFFSET macros (nrf/include/flash_map_pm.h) keyed on the
	 * pm_static.yml partition *name* ("ride_storage") -- not the
	 * devicetree node's own label ("ride_storage_partition"), which is
	 * what the plain upstream Zephyr macro would key on. Confirmed by
	 * reading the generated pm_config.h rather than assumed. */
	return FIXED_PARTITION_OFFSET(ride_storage) + (uint32_t)slot_index * RIDE_SLOT_SIZE;
}

/*
 * Appends one FIT `record` message (definition included, only the very
 * first time this slot is written this ride) to the slot's raw NOR
 * record stream. Sector-erase-ahead: whenever the write would straddle a
 * sector boundary, the cursor skips to the start of the next sector
 * first (wasting at most one record's worth of space per 4KB sector --
 * negligible at this record size) so "erase whenever the cursor lands
 * exactly on a sector boundary" is the only erase rule needed, and it's
 * fully re-derivable from write_cursor alone after a resume -- no
 * separate erase-watermark needs persisting.
 */
static int ride_write_record(uint8_t slot_index, sRideSlotState *st, const FIT_RECORD_MESG *rec)
{
	const struct device *qspi = ride_qspi_dev();
	uint8_t buf[FIT_HDR_SIZE + FIT_RECORD_MESG_DEF_SIZE + FIT_HDR_SIZE + FIT_RECORD_MESG_SIZE];
	size_t len = 0;
	const uint8_t local_mesg_number = 0;

	if (st->nb_records == 0) {
		uint8_t def_hdr = local_mesg_number | FIT_HDR_TYPE_DEF_BIT;

		buf[len++] = def_hdr;
		memcpy(&buf[len], fit_mesg_defs[FIT_MESG_RECORD], FIT_RECORD_MESG_DEF_SIZE);
		len += FIT_RECORD_MESG_DEF_SIZE;
	}

	buf[len++] = local_mesg_number;
	memcpy(&buf[len], rec, FIT_RECORD_MESG_SIZE);
	len += FIT_RECORD_MESG_SIZE;

	uint32_t cursor = st->write_cursor;

	if ((cursor % RIDE_SECTOR_SIZE) + len > RIDE_SECTOR_SIZE) {
		cursor = ((cursor / RIDE_SECTOR_SIZE) + 1) * RIDE_SECTOR_SIZE;
	}

	if (cursor + len > RIDE_SLOT_SIZE) {
		LOG_ERR("ride_recorder: slot %u full (cursor=%u), dropping sample", slot_index, cursor);
		return -ENOSPC;
	}

	if (cursor % RIDE_SECTOR_SIZE == 0) {
		int err = flash_erase(qspi, ride_slot_offset(slot_index) + cursor, RIDE_SECTOR_SIZE);

		if (err != 0) {
			LOG_ERR("ride_recorder: flash_erase() -> %d", err);
			return err;
		}
	}

	/* Real bug found on real hardware (2026-09-11): nrf_qspi_nor_write()
	 * requires the write address to be 4-byte aligned, and the size to
	 * be either <=4 or a multiple of 4 (confirmed by reading
	 * nrf_qspi_nor.c directly, not guessed) -- `cursor` here is a plain
	 * running byte offset into the record stream, not 4-aligned in
	 * general, so this call was failing with flash_write() -> -22
	 * (-EINVAL) on the very first real record ever written (masked
	 * until now by the separate Locator::tasks() bug above, which meant
	 * no record had ever actually reached this far before). Fixed by
	 * widening the physical write to the enclosing 4-byte-aligned
	 * window: the lead bytes (before `cursor`, if any) and the tail
	 * bytes (after cursor+len, up to the next 4-byte boundary) are both
	 * still-erased NOR (0xFF) -- rewriting 0xFF over 0xFF is a no-op on
	 * NOR flash (a write can only clear bits), and the tail bytes are
	 * never read back on export (ride_export_slot_to_sd() only ever
	 * reads up to the logical write_cursor, which still advances by the
	 * real, unpadded `len`) -- so this doesn't change the FIT byte
	 * stream's framing at all, only which flash bytes physically get
	 * touched by this one call. */
	uint32_t aligned_addr = cursor & ~3u;
	uint32_t lead_pad = cursor - aligned_addr;
	uint32_t aligned_len = (lead_pad + len + 3u) & ~3u;
	uint8_t aligned_buf[sizeof(buf) + 6];

	memset(aligned_buf, 0xFF, lead_pad);
	memcpy(&aligned_buf[lead_pad], buf, len);
	memset(&aligned_buf[lead_pad + len], 0xFF, aligned_len - lead_pad - len);

	int err = flash_write(qspi, ride_slot_offset(slot_index) + aligned_addr, aligned_buf,
			       aligned_len);

	if (err != 0) {
		LOG_ERR("ride_recorder: flash_write() -> %d", err);
		return err;
	}

	st->write_cursor = cursor + (uint32_t)len;
	st->nb_records++;

	return 0;
}

/* Running CRC over everything fs_write()s during export -- mirrors
 * stravaV10's encode_lib.c own WriteData()'s side effect, just against
 * an SD fs_file_t instead of the FA_WRITE FatFS handle that code used
 * directly. The file header itself is NOT folded in (matches the
 * original: WriteFileHeader() there calls WriteDataBare(), bypassing
 * WriteData()'s CRC side effect). */
static uint16_t s_export_crc;

static int ride_export_write(struct fs_file_t *file, const void *data, size_t len)
{
	ssize_t written = fs_write(file, data, len);

	if (written < 0 || (size_t)written != len) {
		return -EIO;
	}

	s_export_crc = FitCRC_Update16(s_export_crc, data, (FIT_UINT32)len);
	return 0;
}

/* Writes one def+data message pair and folds both into s_export_crc,
 * collapsing what was 7 near-identical 4-line call sequences in
 * ride_export_slot_to_sd() into a single shared helper -- real flash
 * savings on an already razor-thin image budget (see this file's mcuboot
 * partition-size comment in pm_static_stravav11_nrf52840.yml), not just
 * cosmetic: each call site previously duplicated the same
 * def-header/def-body/local-header/msg-body write sequence in machine
 * code, 7 times over. */
static bool ride_write_mesg(struct fs_file_t *file, FIT_UINT16 mesg_num, const void *msg,
			     FIT_UINT16 msg_size)
{
	const uint8_t local = 0;
	const uint8_t def_hdr = local | FIT_HDR_TYPE_DEF_BIT;
	const void *def = fit_mesg_defs[mesg_num];
	FIT_UINT16 def_size = Fit_GetMesgDefSize(def);
	bool ok = true;

	ok = ok && ride_export_write(file, &def_hdr, FIT_HDR_SIZE) == 0;
	ok = ok && ride_export_write(file, def, def_size) == 0;
	ok = ok && ride_export_write(file, &local, FIT_HDR_SIZE) == 0;
	ok = ok && ride_export_write(file, msg, msg_size) == 0;

	return ok;
}

#define RIDE_FIXED_OVERHEAD_SIZE                                                                 \
	((FIT_HDR_SIZE + FIT_FILE_ID_MESG_DEF_SIZE) + (FIT_HDR_SIZE + FIT_FILE_ID_MESG_SIZE) +    \
	 (FIT_HDR_SIZE + FIT_FILE_CREATOR_MESG_DEF_SIZE) +                                        \
	 (FIT_HDR_SIZE + FIT_FILE_CREATOR_MESG_SIZE) + (FIT_HDR_SIZE + FIT_EVENT_MESG_DEF_SIZE) + \
	 (FIT_HDR_SIZE + FIT_EVENT_MESG_SIZE) + /* start event */                                 \
	 (FIT_HDR_SIZE + FIT_LAP_MESG_DEF_SIZE) + (FIT_HDR_SIZE + FIT_LAP_MESG_SIZE) +             \
	 (FIT_HDR_SIZE + FIT_EVENT_MESG_DEF_SIZE) + (FIT_HDR_SIZE + FIT_EVENT_MESG_SIZE) + /* stop */ \
	 (FIT_HDR_SIZE + FIT_SESSION_MESG_DEF_SIZE) + (FIT_HDR_SIZE + FIT_SESSION_MESG_SIZE) +    \
	 (FIT_HDR_SIZE + FIT_ACTIVITY_MESG_DEF_SIZE) + (FIT_HDR_SIZE + FIT_ACTIVITY_MESG_SIZE))

static bool ride_export_slot_to_sd(uint8_t slot_index, sRideSlotState *st)
{
	char fname[32];

	snprintf(fname, sizeof(fname), "/SD:/%08lX.FIT", (unsigned long)st->start_timestamp);

	/* SD is mounted transiently, per operation -- not held persistently
	 * across boot (see sd_fat_demo()'s own fs_unmount() at the end of its
	 * self-test in main.c, and map_screen_demo.c/sd_stress_demo.c's own
	 * history of the same "two persistent mounts conflict" lesson).
	 * Found the hard way here too: the very first real hardware test of
	 * RIDE STOP failed with "fs: mount point not found!!" because this
	 * function assumed /SD: was already mounted from boot. */
	static FATFS fat_fs;
	static struct fs_mount_t mp = {
		.type = FS_FATFS,
		.fs_data = &fat_fs,
		.mnt_point = "/SD:",
	};

	int err = fs_mount(&mp);

	if (err != 0) {
		LOG_ERR("ride_recorder: fs_mount(/SD:) -> %d", err);
		return false;
	}

	struct fs_file_t file;

	fs_file_t_init(&file);
	err = fs_open(&file, fname, FS_O_CREATE | FS_O_WRITE);

	if (err != 0) {
		LOG_ERR("ride_recorder: fs_open(%s) -> %d", fname, err);
		fs_unmount(&mp);
		return false;
	}

	uint32_t data_size = RIDE_FIXED_OVERHEAD_SIZE + st->write_cursor;

	FIT_FILE_HDR file_header;

	file_header.header_size = FIT_FILE_HDR_SIZE;
	file_header.protocol_version = FIT_PROTOCOL_VERSION_20;
	file_header.profile_version = FIT_PROFILE_VERSION;
	file_header.data_size = data_size;
	memcpy((FIT_UINT8 *)&file_header.data_type, ".FIT", 4);
	file_header.crc = FitCRC_Calc16(&file_header, FIT_STRUCT_OFFSET(crc, FIT_FILE_HDR));

	/* Header itself isn't part of the running data CRC -- see this
	 * function's own top comment. */
	if (fs_write(&file, &file_header, FIT_FILE_HDR_SIZE) != FIT_FILE_HDR_SIZE) {
		LOG_ERR("ride_recorder: header write failed");
		fs_close(&file);
		fs_unmount(&mp);
		return false;
	}

	s_export_crc = 0;

	bool ok = true;

	/* file_id */
	FIT_FILE_ID_MESG file_id;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_FILE_ID], &file_id);
	file_id.time_created = st->start_timestamp;
	file_id.type = FIT_FILE_ACTIVITY;
	file_id.manufacturer = FIT_MANUFACTURER_LEZYNE;
	strncpy(file_id.product_name, "stravaV11", sizeof(file_id.product_name));
	ok = ok && ride_write_mesg(&file, FIT_MESG_FILE_ID, &file_id, FIT_FILE_ID_MESG_SIZE);

	/* file_creator */
	FIT_FILE_CREATOR_MESG creator_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_FILE_CREATOR], &creator_msg);
	creator_msg.hardware_version = 2;
	creator_msg.software_version = 1;
	ok = ok && ride_write_mesg(&file, FIT_MESG_FILE_CREATOR, &creator_msg, FIT_FILE_CREATOR_MESG_SIZE);

	/* event: start */
	FIT_EVENT_MESG event_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_EVENT], &event_msg);
	event_msg.timestamp = st->start_timestamp;
	event_msg.event_type = FIT_EVENT_TYPE_START;
	ok = ok && ride_write_mesg(&file, FIT_MESG_EVENT, &event_msg, FIT_EVENT_MESG_SIZE);

	/* The whole NOR record stream, copied verbatim via the same XIP
	 * memory-mapped read path Phase 11's qspi_xip_demo() validated --
	 * this is the actual point of storing ride_storage_partition as one
	 * flat, contiguous region rather than inside a filesystem: no
	 * chunked flash_read() driver calls, no per-file block indirection,
	 * just a normal RAM pointer fs_write() (and the CRC loop above) can
	 * read directly. */
	if (ok && st->write_cursor > 0) {
		const struct device *qspi = ride_qspi_dev();

		nrf_qspi_nor_xip_enable(qspi, true);

		const uint8_t *xip = (const uint8_t *)(QSPI_XIP_BASE_ADDR + ride_slot_offset(slot_index));
		const size_t chunk_size = 4096;

		for (uint32_t off = 0; ok && off < st->write_cursor; off += chunk_size) {
			size_t n = st->write_cursor - off;

			if (n > chunk_size) {
				n = chunk_size;
			}
			ok = ride_export_write(&file, xip + off, n) == 0;
		}

		nrf_qspi_nor_xip_enable(qspi, false);
	}

	/* lap */
	FIT_LAP_MESG lap_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_LAP], &lap_msg);
	lap_msg.timestamp = st->last_timestamp;
	lap_msg.start_time = st->start_timestamp;
	lap_msg.total_elapsed_time = (st->last_timestamp - st->start_timestamp) * 1000;
	lap_msg.event = FIT_EVENT_LAP;
	lap_msg.sport = FIT_SPORT_CYCLING;
	ok = ok && ride_write_mesg(&file, FIT_MESG_LAP, &lap_msg, FIT_LAP_MESG_SIZE);

	/* event: stop */
	Fit_InitMesg(fit_mesg_defs[FIT_MESG_EVENT], &event_msg);
	event_msg.timestamp = st->last_timestamp;
	event_msg.event_type = FIT_EVENT_TYPE_STOP_ALL;
	ok = ok && ride_write_mesg(&file, FIT_MESG_EVENT, &event_msg, FIT_EVENT_MESG_SIZE);

	/* session */
	FIT_SESSION_MESG session_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_SESSION], &session_msg);
	session_msg.timestamp = st->last_timestamp;
	session_msg.start_time = st->start_timestamp;
	session_msg.total_elapsed_time = (st->last_timestamp - st->start_timestamp) * 1000;
	session_msg.sport = FIT_SPORT_CYCLING;
	session_msg.sub_sport = FIT_SUB_SPORT_MOUNTAIN;
	ok = ok && ride_write_mesg(&file, FIT_MESG_SESSION, &session_msg, FIT_SESSION_MESG_SIZE);

	/* activity */
	FIT_ACTIVITY_MESG act_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_ACTIVITY], &act_msg);
	act_msg.timestamp = st->last_timestamp;
	act_msg.type = FIT_ACTIVITY_MANUAL;
	act_msg.event_type = FIT_EVENT_TYPE_STOP;
	act_msg.event = FIT_EVENT_ACTIVITY;
	act_msg.num_sessions = 1;
	ok = ok && ride_write_mesg(&file, FIT_MESG_ACTIVITY, &act_msg, FIT_ACTIVITY_MESG_SIZE);

	/* CRC itself is written raw, not folded into itself -- matches
	 * encode_lib.c's own WriteDataBare(&data_crc, ...) precedent. */
	if (ok) {
		uint16_t crc_le = s_export_crc;

		ok = fs_write(&file, &crc_le, sizeof(crc_le)) == sizeof(crc_le);
	}

	fs_close(&file);
	fs_unmount(&mp);

	if (!ok) {
		LOG_ERR("ride_recorder: export of slot %u to %s failed mid-write", slot_index, fname);
		return false;
	}

	LOG_INF("ride_recorder: exported slot %u (%u records, %u bytes) -> %s", slot_index,
		st->nb_records, data_size, fname);
	return true;
}

static bool ride_slot_export_and_free(uint8_t slot_index)
{
	sRideSlotState *st = &s_state.slots[slot_index];

	if (!ride_export_slot_to_sd(slot_index, st)) {
		return false;
	}

	memset(st, 0, sizeof(*st));
	st->state = eRideSlotFree;
	ride_fram_save();
	return true;
}

void ride_recorder_init(void)
{
	if (!ride_fram_load()) {
		LOG_INF("ride_recorder: no valid FRAM state, starting fresh");
		ride_fram_reset();
		ride_fram_save();
		return;
	}

	for (uint8_t i = 0; i < RIDE_NUM_SLOTS; i++) {
		if (s_state.slots[i].state == eRideSlotRecording) {
			s_active_slot = (int8_t)i;
			s_have_prev_fix = false;
			/* Whole metres only, same reasoning gfx_demo.cpp's own
			 * altitude line already established: avoids float
			 * formatting under picolibc (unconfirmed supported in
			 * this build) and its extra code-size cost. */
			LOG_INF("ride_recorder: resuming slot %u (start_ts=%u, %u records, "
				"cursor=%u, dist=%dm climb=%dm)",
				i, s_state.slots[i].start_timestamp, s_state.slots[i].nb_records,
				s_state.slots[i].write_cursor, (int)s_state.slots[i].distance_m,
				(int)s_state.slots[i].climb_m);
		} else if (s_state.slots[i].state == eRideSlotPendingExport) {
			LOG_INF("ride_recorder: retrying export for slot %u", i);
			ride_slot_export_and_free(i);
		}
	}
}

bool ride_recorder_start(uint32_t start_unix_timestamp)
{
	if (s_active_slot >= 0) {
		LOG_WRN("ride_recorder: ride already active in slot %d", s_active_slot);
		return false;
	}

	for (uint8_t n = 0; n < RIDE_NUM_SLOTS; n++) {
		uint8_t i = (s_state.next_slot_hint + n) % RIDE_NUM_SLOTS;

		if (s_state.slots[i].state == eRideSlotFree) {
			memset(&s_state.slots[i], 0, sizeof(s_state.slots[i]));
			s_state.slots[i].state = eRideSlotRecording;
			s_state.slots[i].start_timestamp = start_unix_timestamp;
			s_state.slots[i].last_timestamp = start_unix_timestamp;
			s_state.next_slot_hint = (uint8_t)((i + 1) % RIDE_NUM_SLOTS);
			s_active_slot = (int8_t)i;
			s_have_prev_fix = false;

			if (!ride_fram_save()) {
				LOG_ERR("ride_recorder: FRAM save failed on start");
			}

			LOG_INF("ride_recorder: started ride in slot %u (start_ts=%u)", i,
				start_unix_timestamp);
			return true;
		}
	}

	LOG_ERR("ride_recorder: no free slot (all %u in use) -- refusing to start", RIDE_NUM_SLOTS);
	return false;
}

bool ride_recorder_is_active(void)
{
	return s_active_slot >= 0;
}

void ride_recorder_add_sample(int32_t lat_semicircles, int32_t lon_semicircles, int32_t alt_cm,
			       uint8_t hrm_bpm, uint8_t cadence, uint16_t power_w,
			       uint32_t unix_timestamp, float distance_delta_m,
			       float climb_delta_m)
{
	ARG_UNUSED(power_w); /* not yet an active field in this FIT profile, see ride_recorder.h */

	if (s_active_slot < 0) {
		return;
	}

	sRideSlotState *st = &s_state.slots[s_active_slot];

	FIT_RECORD_MESG rec;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_RECORD], &rec);
	rec.timestamp = unix_timestamp;
	rec.position_lat = lat_semicircles;
	rec.position_long = lon_semicircles;
	rec.altitude = (FIT_UINT16)(2500 + alt_cm / 20); /* 5m/count + 500m offset, see fit_encode.cpp's own comment */
	rec.grade = 0;
	rec.heart_rate = hrm_bpm;
	rec.cadence = cadence;
	rec.temperature = 0;

	int err = ride_write_record((uint8_t)s_active_slot, st, &rec);

	if (err != 0) {
		/* Slot full or a real flash error -- stop cleanly rather than
		 * keep dropping samples silently forever. */
		LOG_ERR("ride_recorder: write failed (%d), stopping ride", err);
		ride_recorder_stop();
		return;
	}

	st->last_timestamp = unix_timestamp;
	st->distance_m += distance_delta_m;
	st->climb_m += climb_delta_m;

	ride_fram_save();
}

bool ride_recorder_stop(void)
{
	if (s_active_slot < 0) {
		return false;
	}

	uint8_t slot = (uint8_t)s_active_slot;

	s_state.slots[slot].state = eRideSlotPendingExport;
	ride_fram_save();
	s_active_slot = -1;

	return ride_slot_export_and_free(slot);
}

bool ride_recorder_get_live_totals(float *distance_m, float *climb_m)
{
	int8_t slot = s_active_slot;

	if (slot < 0) {
		/* Nothing active -- but still report the most recently
		 * touched slot's totals if one exists, so the display isn't
		 * blank immediately after a clean stop either. */
		for (uint8_t i = 0; i < RIDE_NUM_SLOTS; i++) {
			if (s_state.slots[i].nb_records > 0) {
				slot = (int8_t)i;
				break;
			}
		}
		if (slot < 0) {
			return false;
		}
	}

	*distance_m = s_state.slots[slot].distance_m;
	*climb_m = s_state.slots[slot].climb_m;
	return true;
}

#define RIDE_TICK_INTERVAL_MS 1000
#define RIDE_EARTH_RADIUS_M 6371000.0f
#define RIDE_PI 3.14159265358979323846f

/* Same great-circle formula tools/gpx_to_c.py already uses host-side for
 * the GPS-sim route data (haversine, not a flat-earth approximation --
 * matters over anything but very short legs). */
static float ride_haversine_m(float lat1, float lon1, float lat2, float lon2)
{
	const float deg2rad = RIDE_PI / 180.0f;
	float phi1 = lat1 * deg2rad;
	float phi2 = lat2 * deg2rad;
	float dphi = (lat2 - lat1) * deg2rad;
	float dlambda = (lon2 - lon1) * deg2rad;

	float a = sinf(dphi / 2.0f) * sinf(dphi / 2.0f) +
		  cosf(phi1) * cosf(phi2) * sinf(dlambda / 2.0f) * sinf(dlambda / 2.0f);
	float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));

	return RIDE_EARTH_RADIUS_M * c;
}

static void ride_tick_work_handler(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(s_ride_tick_work, ride_tick_work_handler);

static void ride_tick_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (ride_recorder_is_active()) {
		float lat, lon, alt = 0.f;
		bool have_pos = gps_demo_get_position(&lat, &lon);
		bool have_alt = gps_demo_get_altitude(&alt);
		uint32_t ts;

		if (have_pos && gps_demo_get_unix_timestamp(&ts)) {
			float dist_delta = 0.f, climb_delta = 0.f;

			if (s_have_prev_fix) {
				dist_delta = ride_haversine_m(s_prev_lat, s_prev_lon, lat, lon);
				if (have_alt && alt > s_prev_alt) {
					climb_delta = alt - s_prev_alt;
				}
			}

			/* Same degrees-to-semicircles conversion stravaV10's own
			 * fit_encode.cpp used (2^31 / 180). */
			int32_t lat_sc = (int32_t)(lat * 11930464.7111f);
			int32_t lon_sc = (int32_t)(lon * 11930464.7111f);
			int32_t alt_cm = (int32_t)(alt * 100.0f);
			uint8_t bpm = hrm_demo_is_paired() ? hrm_demo_get_bpm() : 0;
			uint32_t cad = bsc_demo_is_paired() ? bsc_demo_get_cadence() : 0;

			ride_recorder_add_sample(lat_sc, lon_sc, alt_cm, bpm, (uint8_t)cad, 0, ts,
						  dist_delta, climb_delta);

			s_prev_lat = lat;
			s_prev_lon = lon;
			if (have_alt) {
				s_prev_alt = alt;
			}
			s_have_prev_fix = true;
		}
	}

	k_work_schedule(&s_ride_tick_work, K_MSEC(RIDE_TICK_INTERVAL_MS));
}

void ride_recorder_start_tick(void)
{
	k_work_schedule(&s_ride_tick_work, K_MSEC(RIDE_TICK_INTERVAL_MS));
}
