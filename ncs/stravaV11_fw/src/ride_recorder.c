/*
 * See ride_recorder.h for the design overview. This file implements:
 *  - the FRAM ride-state struct (5 slots' worth) that is the sole source
 *    of truth for resume, superseding the old .noinit-RAM approach --
 *    survives real power loss, not just a warm reset;
 *  - the NOR-side record writer: a raw, filesystem-free append log inside
 *    ride_storage_partition (stravav11_nrf52840.dts), one 2MB slot per
 *    ride. Holds `record` messages (local mesg id 0, written once-
 *    definition-then-fixed-size) interleaved with `lap` messages (local
 *    mesg id 1, same trick, added 2026-09-12 for the manual-lap feature
 *    -- see ride_recorder_lap()) -- deliberately not a technically-valid
 *    FIT file on its own; NOR flash can only clear bits on program
 *    (never set them back to 1 without a sector erase), so the classic
 *    "patch the header's data_size in place as you go" trick from
 *    stravaV10's own fit_encode.cpp -- fine on SD/FAT, whose own FTL
 *    hides that -- simply doesn't work here. Finalizing into a real,
 *    valid .FIT file only happens once, straight to the SD card, in
 *    ride_export_slot_to_sd().
 *  - the SD-side finalize/export: a single pass (file_id/creator/start
 *    event, the whole NOR record+lap stream copied verbatim via the XIP
 *    memory-mapped read path already validated in Phase 11's
 *    qspi_xip_demo(), then one final trailing lap/stop-event/session/
 *    activity/crc for whatever lap was still open at RIDE STOP) --
 *    possible in one pass, no placeholder-then-patch step, because the
 *    final data_size is always known in advance (fixed one-time message
 *    sizes + FRAM's own write_cursor for that slot, which already
 *    includes every interleaved lap record).
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
#include "power_provider.h"
#include "hrm_provider.h"
#include "cadence_provider.h"

LOG_MODULE_REGISTER(ride_recorder, LOG_LEVEL_INF);

/* Real bug, found 2026-09-11 via a real GPS Ally download crash: every
 * FIT_DATE_TIME field this file writes (file_id.time_created, every
 * event/lap/session/activity timestamp/start_time, and record.timestamp)
 * used to get a raw Unix timestamp (seconds since 1970) assigned
 * directly -- but fit_example.h's own FIT_DATE_TIME typedef comment says
 * exactly what the real FIT spec requires: "seconds since UTC 00:00 Dec
 * 31 1989" (the FIT epoch), not Unix time. Confirmed independently with
 * Python's `fitparse` against a real exported file pulled off the SD
 * card over USB MSC: every timestamp decoded as 2046, not 2026 -- a ride
 * dated 20 years in the future is exactly the kind of value GPS Ally's
 * own Ride.fromFitFile(...).save(...) most likely chokes on. Every OTHER
 * use of a timestamp in this file (the exported filename, the FRAM
 * st->start_timestamp/last_timestamp fields, the Lezyne wire protocol's
 * file_id) is deliberately still plain Unix time -- only values actually
 * written into a FIT_DATE_TIME field need this conversion. Duration
 * fields (total_elapsed_time = end - start) are unaffected either way,
 * since subtracting two timestamps in the same epoch cancels the
 * constant offset out -- not converted here, and correctly so. */
#define FIT_EPOCH_OFFSET_FROM_UNIX 631065600U /* 1989-12-31T00:00:00Z minus 1970-01-01T00:00:00Z, in seconds */

static inline FIT_DATE_TIME fit_timestamp_from_unix(uint32_t unix_timestamp)
{
	return (FIT_DATE_TIME)(unix_timestamp - FIT_EPOCH_OFFSET_FROM_UNIX);
}

/* Same "5 * m + 500" scale/offset as ride_recorder_add_sample()'s own
 * rec.altitude assignment (see fit_example.h's FIT_LAP_MESG/
 * FIT_SESSION_MESG comments) -- shared here since lap/session altitude
 * aggregates need the identical conversion applied to floats instead of
 * the raw alt_cm integer the per-record field uses. */
static inline FIT_UINT16 fit_altitude_from_m(float alt_m)
{
	return (FIT_UINT16)(2500.0f + alt_m * 5.0f);
}

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

/* Manual-lap feature: how many *closed* laps FRAM remembers for the
 * on-screen display and reboot survival. A ride can have far more laps
 * than this (each one is durably written into the QSPI record stream
 * regardless, see ride_write_lap_to_stream()) -- this is deliberately
 * small, not a cap on real lap count, just on how much lap *history*
 * the live display needs to show. */
#define RIDE_LAP_DISPLAY_COUNT 5

typedef struct __attribute__((packed)) {
	uint32_t lap_number; /* 1-based */
	uint32_t elapsed_s; /* this lap's own duration */
	uint16_t avg_power_w;
	uint16_t normalized_power_w;
} sRideRecentLap;

typedef struct __attribute__((packed)) {
	uint8_t state; /* eRideSlotState */
	uint32_t start_timestamp;
	uint32_t write_cursor; /* bytes of record-stream written in this slot */
	uint32_t last_timestamp;
	float distance_m;
	float climb_m; /* ascent only, see ride_tick_work_handler()'s own comment */
	uint32_t nb_records;
	/* Real GPS Ally NPE crash, found 2026-09-12: Ride.java's onMesg(LapMesg)/
	 * onMesg(SessionMesg) unconditionally call .intValue() on several
	 * FIT fields (avg_altitude/min_altitude on LAP; total_ascent/
	 * total_descent/total_calories/enhanced_min_altitude/
	 * enhanced_max_altitude on SESSION) with no null check, unlike every
	 * other field in those same methods -- this port's exported FIT file
	 * never set any of them, so the FIT SDK's generated getters returned
	 * null and the app crashed decoding the LAP message (the first of the
	 * two to appear in file order) the instant a download finished.
	 * These four fields plus fit_example.{h,c}'s newly-uncommented
	 * FIT_LAP_MESG/FIT_SESSION_MESG fields are the fix. */
	float descent_m;
	float min_alt_m;
	float max_alt_m;
	float sum_alt_m;
	/* Manual-lap feature (2026-09-12): the *currently open* lap's live
	 * accumulators, persisted for the same resume-safety reasoning as
	 * distance_m/climb_m/etc. above -- a crash mid-lap just means the
	 * open lap's stats keep accumulating correctly from where they left
	 * off, same as every other running total in this struct. Not
	 * reset at lap boundaries: current_lap_start_timestamp (also
	 * doubles as this lap's start for total_elapsed_time). Every
	 * *closed* lap is written straight into the QSPI stream as its own
	 * FIT_LAP_MESG (see ride_write_lap_to_stream()) instead of being
	 * buffered here -- FRAM only needs to remember the last
	 * RIDE_LAP_DISPLAY_COUNT of them (recent_laps[], below) for the
	 * on-screen display and reboot survival; the full lap history
	 * already lives safely in the QSPI stream, same as every GPS
	 * record. */
	uint32_t current_lap_start_timestamp;
	uint32_t lap_power_sum;
	uint32_t lap_power_count;
	double lap_np_sum_pow4; /* sum of (30s rolling avg power)^4, for Normalized Power */
	uint32_t lap_np_count;
	float lap_min_alt_m;
	float lap_max_alt_m;
	float lap_sum_alt_m;
	uint32_t lap_nb_records;
	uint32_t total_completed_laps; /* never reset within a ride; also gates the LAP stream def write-once */
	sRideRecentLap recent_laps[RIDE_LAP_DISPLAY_COUNT];
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

/* Normalized Power's 30-second rolling-average window: RAM-only, same
 * "acceptable to lose on a crash" reasoning as s_have_prev_fix/
 * s_prev_lat/lon/alt above -- a crash mid-ride just means a ~30s
 * re-warm-up of this window, not a wrong or missing lap boundary. Not
 * reset at lap boundaries (continuous across the whole ride, standard
 * Normalized Power methodology) -- only reset when a new ride starts. */
#define NP_WINDOW_SAMPLES 30 /* seconds, matches RIDE_TICK_INTERVAL_MS below */
static uint16_t s_np_window[NP_WINDOW_SAMPLES];
static uint8_t s_np_window_count;
static uint8_t s_np_window_idx;
static uint32_t s_np_window_sum;

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
 * Appends `len` already-framed FIT bytes (a def+data pair, or a bare
 * data record reusing an earlier def) to the slot's raw NOR stream.
 * Shared by ride_write_record() (local mesg id 0, RECORD) and
 * ride_write_lap_to_stream() (local mesg id 1, LAP) -- both message
 * types are appended to the exact same growing byte range, distinguished
 * only by their own local mesg id, exactly like two FIT local message
 * definitions coexisting in any real FIT file. Sector-erase-ahead:
 * whenever the write would straddle a sector boundary, the cursor skips
 * to the start of the next sector first (wasting at most one message's
 * worth of space per 4KB sector -- negligible at these message sizes)
 * so "erase whenever the cursor lands exactly on a sector boundary" is
 * the only erase rule needed, and it's fully re-derivable from
 * write_cursor alone after a resume -- no separate erase-watermark needs
 * persisting.
 */
static int ride_stream_append(uint8_t slot_index, sRideSlotState *st, const uint8_t *buf,
			       size_t len)
{
	const struct device *qspi = ride_qspi_dev();
	uint32_t cursor = st->write_cursor;

	if ((cursor % RIDE_SECTOR_SIZE) + len > RIDE_SECTOR_SIZE) {
		cursor = ((cursor / RIDE_SECTOR_SIZE) + 1) * RIDE_SECTOR_SIZE;
	}

	if (cursor + len > RIDE_SLOT_SIZE) {
		LOG_ERR("ride_recorder: slot %u full (cursor=%u), dropping message", slot_index,
			cursor);
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
	uint8_t aligned_buf[FIT_HDR_SIZE + FIT_LAP_MESG_DEF_SIZE + FIT_HDR_SIZE + FIT_LAP_MESG_SIZE + 6];

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

	return 0;
}

static int ride_write_record(uint8_t slot_index, sRideSlotState *st, const FIT_RECORD_MESG *rec)
{
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

	int err = ride_stream_append(slot_index, st, buf, len);

	if (err == 0) {
		st->nb_records++;
	}

	return err;
}

/* Manual-lap feature (2026-09-12): appends one *closed* lap straight into
 * the same growing QSPI stream ride_write_record() uses, under its own
 * local mesg id (1, distinct from RECORD's 0) so the two message types
 * coexist without either redefining the other's meaning -- a real FIT
 * decoder tracks "what does local id N currently mean" independently per
 * id, exactly like this. This is what lets a ride have far more laps
 * than FRAM's small recent_laps[] display window without any extra FRAM
 * cost: every closed lap is durable here the instant it closes, same
 * crash-safety guarantee the GPS records already have. */
static int ride_write_lap_to_stream(uint8_t slot_index, sRideSlotState *st,
				     const FIT_LAP_MESG *lap)
{
	uint8_t buf[FIT_HDR_SIZE + FIT_LAP_MESG_DEF_SIZE + FIT_HDR_SIZE + FIT_LAP_MESG_SIZE];
	size_t len = 0;
	const uint8_t local_mesg_number = 1;

	if (st->total_completed_laps == 0) {
		uint8_t def_hdr = local_mesg_number | FIT_HDR_TYPE_DEF_BIT;

		buf[len++] = def_hdr;
		memcpy(&buf[len], fit_mesg_defs[FIT_MESG_LAP], FIT_LAP_MESG_DEF_SIZE);
		len += FIT_LAP_MESG_DEF_SIZE;
	}

	buf[len++] = local_mesg_number;
	memcpy(&buf[len], lap, FIT_LAP_MESG_SIZE);
	len += FIT_LAP_MESG_SIZE;

	return ride_stream_append(slot_index, st, buf, len);
}

/* Reduces the *currently open* lap's live accumulators to the four FIT
 * fields a FIT_LAP_MESG needs -- shared between ride_recorder_lap()
 * (closing a lap early) and ride_export_slot_to_sd() (finalizing
 * whatever lap is still open at RIDE STOP). avg_altitude/min_altitude
 * must never be left at their zeroed-by-memset defaults when a lap has
 * zero samples (e.g. two "LAP" commands in the same tick) -- see
 * sRideSlotState's own comment on the real GPS Ally NPE crash this
 * would otherwise regress. */
static void ride_finalize_lap_metrics(const sRideSlotState *st, uint16_t *avg_power_w,
				       uint16_t *normalized_power_w, FIT_UINT16 *avg_altitude_raw,
				       FIT_UINT16 *min_altitude_raw)
{
	*avg_power_w =
		st->lap_power_count > 0 ? (uint16_t)(st->lap_power_sum / st->lap_power_count) : 0;
	*normalized_power_w =
		st->lap_np_count > 0
			? (uint16_t)pow(st->lap_np_sum_pow4 / (double)st->lap_np_count, 0.25)
			: 0;
	*avg_altitude_raw = fit_altitude_from_m(
		st->lap_nb_records > 0 ? st->lap_sum_alt_m / (float)st->lap_nb_records : 0.f);
	*min_altitude_raw = fit_altitude_from_m(st->lap_min_alt_m);
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

	/* Real bug, found 2026-09-12 via a real GPS Ally file-list showing
	 * 2046 (title only -- the downloaded file's own internal FIT fields,
	 * fixed above, decoded correctly): the decompiled app's own
	 * BleFitFile.parseFileName() treats the filename's hex value as FIT
	 * epoch seconds and adds FIT_EPOCH_OFFSET_FROM_UNIX again to get a
	 * display date ("new DateTime((fileId*1000) + 631065600000L)") --
	 * i.e. a real Lezyne device names its files with the FIT epoch, not
	 * Unix time. This port's filename (and hence the Lezyne wire
	 * protocol's file_id, which is just this same hex value echoed back
	 * by lezyne_handler.c) was using raw Unix time, double-applying the
	 * offset once the app added its own. FRAM's own start_timestamp
	 * stays plain Unix (still used for total_elapsed_time and as the
	 * input to fit_timestamp_from_unix() for the file's internal FIT
	 * fields) -- only the on-disk filename needs this conversion. */
	snprintf(fname, sizeof(fname), "/SD:/%08lX.FIT",
		 (unsigned long)fit_timestamp_from_unix(st->start_timestamp));

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
	file_id.time_created = fit_timestamp_from_unix(st->start_timestamp);
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
	event_msg.timestamp = fit_timestamp_from_unix(st->start_timestamp);
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

	/* Trailing lap: whatever lap was still open at RIDE STOP. Every
	 * *closed* lap before this one is already inside the
	 * [0, write_cursor) byte range the XIP copy loop above just wrote
	 * verbatim (see ride_write_lap_to_stream()) -- this is the only lap
	 * message this function itself needs to build. */
	FIT_LAP_MESG lap_msg;
	uint16_t trailing_avg_power_w, trailing_normalized_power_w;
	FIT_UINT16 trailing_avg_altitude_raw, trailing_min_altitude_raw;

	ride_finalize_lap_metrics(st, &trailing_avg_power_w, &trailing_normalized_power_w,
				   &trailing_avg_altitude_raw, &trailing_min_altitude_raw);

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_LAP], &lap_msg);
	lap_msg.timestamp = fit_timestamp_from_unix(st->last_timestamp);
	lap_msg.start_time = fit_timestamp_from_unix(st->current_lap_start_timestamp);
	lap_msg.total_elapsed_time = (st->last_timestamp - st->current_lap_start_timestamp) * 1000;
	lap_msg.event = FIT_EVENT_LAP;
	lap_msg.sport = FIT_SPORT_CYCLING;
	lap_msg.avg_power = trailing_avg_power_w;
	lap_msg.normalized_power = trailing_normalized_power_w;
	/* avg_altitude/min_altitude: real GPS Ally NPE fix, see
	 * sRideSlotState's own comment. */
	lap_msg.avg_altitude = trailing_avg_altitude_raw;
	lap_msg.min_altitude = trailing_min_altitude_raw;
	ok = ok && ride_write_mesg(&file, FIT_MESG_LAP, &lap_msg, FIT_LAP_MESG_SIZE);

	/* event: stop */
	Fit_InitMesg(fit_mesg_defs[FIT_MESG_EVENT], &event_msg);
	event_msg.timestamp = fit_timestamp_from_unix(st->last_timestamp);
	event_msg.event_type = FIT_EVENT_TYPE_STOP_ALL;
	ok = ok && ride_write_mesg(&file, FIT_MESG_EVENT, &event_msg, FIT_EVENT_MESG_SIZE);

	/* session */
	FIT_SESSION_MESG session_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_SESSION], &session_msg);
	session_msg.timestamp = fit_timestamp_from_unix(st->last_timestamp);
	session_msg.start_time = fit_timestamp_from_unix(st->start_timestamp);
	session_msg.total_elapsed_time = (st->last_timestamp - st->start_timestamp) * 1000;
	session_msg.sport = FIT_SPORT_CYCLING;
	session_msg.sub_sport = FIT_SUB_SPORT_MOUNTAIN;
	/* Real GPS Ally NPE fix, see sRideSlotState's own comment --
	 * total_calories isn't tracked by this port (no calorie model yet),
	 * written as 0 purely so the field is non-null, same reasoning as
	 * lap_msg's altitude fields above. */
	session_msg.total_ascent = (FIT_UINT16)st->climb_m;
	session_msg.total_descent = (FIT_UINT16)st->descent_m;
	session_msg.total_calories = 0;
	session_msg.enhanced_min_altitude = (FIT_UINT32)fit_altitude_from_m(st->min_alt_m);
	session_msg.enhanced_max_altitude = (FIT_UINT32)fit_altitude_from_m(st->max_alt_m);
	ok = ok && ride_write_mesg(&file, FIT_MESG_SESSION, &session_msg, FIT_SESSION_MESG_SIZE);

	/* activity */
	FIT_ACTIVITY_MESG act_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_ACTIVITY], &act_msg);
	act_msg.timestamp = fit_timestamp_from_unix(st->last_timestamp);
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
			s_state.slots[i].current_lap_start_timestamp = start_unix_timestamp;
			s_state.next_slot_hint = (uint8_t)((i + 1) % RIDE_NUM_SLOTS);
			s_active_slot = (int8_t)i;
			s_have_prev_fix = false;
			s_np_window_count = 0;
			s_np_window_idx = 0;
			s_np_window_sum = 0;

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
			       float climb_delta_m, float descent_delta_m)
{
	if (s_active_slot < 0) {
		return;
	}

	sRideSlotState *st = &s_state.slots[s_active_slot];

	FIT_RECORD_MESG rec;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_RECORD], &rec);
	rec.timestamp = fit_timestamp_from_unix(unix_timestamp);
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
	st->descent_m += descent_delta_m;

	float alt_m = (float)alt_cm / 100.0f;

	if (st->nb_records == 1) {
		/* First sample of the ride (post-increment in
		 * ride_write_record()) -- seed min/max instead of comparing
		 * against the zeroed-by-memset defaults from
		 * ride_recorder_start(), which would wrongly clamp max to 0. */
		st->min_alt_m = alt_m;
		st->max_alt_m = alt_m;
	} else {
		if (alt_m < st->min_alt_m) {
			st->min_alt_m = alt_m;
		}
		if (alt_m > st->max_alt_m) {
			st->max_alt_m = alt_m;
		}
	}
	st->sum_alt_m += alt_m;

	/* Manual-lap feature: same seed-then-track pattern as the whole-ride
	 * min/max above, scoped to the currently open lap instead (reset
	 * whenever a lap closes, see ride_recorder_lap()). */
	st->lap_nb_records++;
	if (st->lap_nb_records == 1) {
		st->lap_min_alt_m = alt_m;
		st->lap_max_alt_m = alt_m;
	} else {
		if (alt_m < st->lap_min_alt_m) {
			st->lap_min_alt_m = alt_m;
		}
		if (alt_m > st->lap_max_alt_m) {
			st->lap_max_alt_m = alt_m;
		}
	}
	st->lap_sum_alt_m += alt_m;

	st->lap_power_sum += power_w;
	st->lap_power_count++;

	/* Normalized Power: a 30-second rolling average of power, raised to
	 * the 4th power, averaged over the lap, then 4th-rooted (standard
	 * Coggan/TrainingPeaks definition). The rolling window itself is
	 * continuous across the whole ride (not reset per lap, see
	 * s_np_window's own comment); only the per-lap sum-of-4th-powers
	 * accumulator below resets at each lap boundary. */
	if (s_np_window_count < NP_WINDOW_SAMPLES) {
		s_np_window_sum += power_w;
		s_np_window[s_np_window_idx] = power_w;
		s_np_window_count++;
	} else {
		s_np_window_sum -= s_np_window[s_np_window_idx];
		s_np_window_sum += power_w;
		s_np_window[s_np_window_idx] = power_w;
	}
	s_np_window_idx = (uint8_t)((s_np_window_idx + 1) % NP_WINDOW_SAMPLES);

	if (s_np_window_count == NP_WINDOW_SAMPLES) {
		double rolling_avg = (double)s_np_window_sum / NP_WINDOW_SAMPLES;

		st->lap_np_sum_pow4 += rolling_avg * rolling_avg * rolling_avg * rolling_avg;
		st->lap_np_count++;
	}

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

bool ride_recorder_lap(void)
{
	if (s_active_slot < 0) {
		LOG_WRN("ride_recorder: LAP refused, no ride active");
		return false;
	}

	sRideSlotState *st = &s_state.slots[s_active_slot];

	uint16_t avg_power_w, normalized_power_w;
	FIT_UINT16 avg_altitude_raw, min_altitude_raw;

	ride_finalize_lap_metrics(st, &avg_power_w, &normalized_power_w, &avg_altitude_raw,
				   &min_altitude_raw);

	FIT_LAP_MESG lap_msg;

	Fit_InitMesg(fit_mesg_defs[FIT_MESG_LAP], &lap_msg);
	lap_msg.start_time = fit_timestamp_from_unix(st->current_lap_start_timestamp);
	lap_msg.timestamp = fit_timestamp_from_unix(st->last_timestamp);
	lap_msg.total_elapsed_time = (st->last_timestamp - st->current_lap_start_timestamp) * 1000;
	lap_msg.event = FIT_EVENT_LAP;
	lap_msg.sport = FIT_SPORT_CYCLING;
	lap_msg.avg_power = avg_power_w;
	lap_msg.normalized_power = normalized_power_w;
	lap_msg.avg_altitude = avg_altitude_raw;
	lap_msg.min_altitude = min_altitude_raw;

	int err = ride_write_lap_to_stream((uint8_t)s_active_slot, st, &lap_msg);

	if (err != 0) {
		LOG_ERR("ride_recorder: LAP write failed (%d), lap not closed", err);
		return false;
	}

	sRideRecentLap *recent = &st->recent_laps[st->total_completed_laps % RIDE_LAP_DISPLAY_COUNT];

	recent->lap_number = st->total_completed_laps + 1;
	recent->elapsed_s = st->last_timestamp - st->current_lap_start_timestamp;
	recent->avg_power_w = avg_power_w;
	recent->normalized_power_w = normalized_power_w;

	st->total_completed_laps++;

	/* Reset the open-lap accumulators for the next lap -- the NP rolling
	 * window itself (s_np_window*) is deliberately NOT reset here, see
	 * its own comment. */
	st->current_lap_start_timestamp = st->last_timestamp;
	st->lap_power_sum = 0;
	st->lap_power_count = 0;
	st->lap_np_sum_pow4 = 0.0;
	st->lap_np_count = 0;
	st->lap_min_alt_m = 0.f;
	st->lap_max_alt_m = 0.f;
	st->lap_sum_alt_m = 0.f;
	st->lap_nb_records = 0;

	ride_fram_save();

	LOG_INF("ride_recorder: lap %u closed (avg_power=%uW np=%uW)", recent->lap_number,
		avg_power_w, normalized_power_w);
	return true;
}

bool ride_recorder_get_current_lap(uint32_t *elapsed_s, uint16_t *avg_power_w,
				    uint16_t *normalized_power_w, uint32_t *lap_number)
{
	if (s_active_slot < 0) {
		return false;
	}

	const sRideSlotState *st = &s_state.slots[s_active_slot];

	*elapsed_s = st->last_timestamp - st->current_lap_start_timestamp;
	*lap_number = st->total_completed_laps + 1;

	FIT_UINT16 unused_alt1, unused_alt2;

	ride_finalize_lap_metrics(st, avg_power_w, normalized_power_w, &unused_alt1, &unused_alt2);
	return true;
}

uint8_t ride_recorder_get_recent_laps(struct ride_recorder_lap_info *out, uint8_t max_count)
{
	int8_t slot = s_active_slot;

	if (slot < 0) {
		/* Same "still show the last touched slot" convention as
		 * ride_recorder_get_live_totals() -- laps stay visible for a
		 * short while after a clean RIDE STOP too. */
		for (uint8_t i = 0; i < RIDE_NUM_SLOTS; i++) {
			if (s_state.slots[i].total_completed_laps > 0) {
				slot = (int8_t)i;
				break;
			}
		}
		if (slot < 0) {
			return 0;
		}
	}

	const sRideSlotState *st = &s_state.slots[slot];
	uint32_t total = st->total_completed_laps;
	uint8_t available = total < RIDE_LAP_DISPLAY_COUNT ? (uint8_t)total : RIDE_LAP_DISPLAY_COUNT;
	uint8_t count = available < max_count ? available : max_count;

	/* Most-recent first: total_completed_laps-1 is the last lap closed,
	 * stored at index (total_completed_laps-1) % RIDE_LAP_DISPLAY_COUNT. */
	for (uint8_t i = 0; i < count; i++) {
		uint32_t idx = (total - 1 - i) % RIDE_LAP_DISPLAY_COUNT;
		const sRideRecentLap *recent = &st->recent_laps[idx];

		out[i].lap_number = recent->lap_number;
		out[i].elapsed_s = recent->elapsed_s;
		out[i].avg_power_w = recent->avg_power_w;
		out[i].normalized_power_w = recent->normalized_power_w;
	}

	return count;
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
			float dist_delta = 0.f, climb_delta = 0.f, descent_delta = 0.f;

			if (s_have_prev_fix) {
				dist_delta = ride_haversine_m(s_prev_lat, s_prev_lon, lat, lon);
				if (have_alt && alt > s_prev_alt) {
					climb_delta = alt - s_prev_alt;
				} else if (have_alt && alt < s_prev_alt) {
					descent_delta = s_prev_alt - alt;
				}
			}

			/* Same degrees-to-semicircles conversion stravaV10's own
			 * fit_encode.cpp used (2^31 / 180). */
			int32_t lat_sc = (int32_t)(lat * 11930464.7111f);
			int32_t lon_sc = (int32_t)(lon * 11930464.7111f);
			int32_t alt_cm = (int32_t)(alt * 100.0f);
			/* power_provider.h/hrm_provider.h/cadence_provider.h
			 * (2026-09-12) unify ANT+ and BLE behind one call
			 * each -- see their own comments for the
			 * source-precedence rules. */
			uint8_t bpm = 0;
			uint32_t cad = 0;
			uint16_t power_w = 0;

			hrm_provider_get_bpm(&bpm);
			cadence_provider_get_rpm(&cad);
			power_provider_get_watts(&power_w);

			ride_recorder_add_sample(lat_sc, lon_sc, alt_cm, bpm, (uint8_t)cad, power_w,
						  ts, dist_delta, climb_delta, descent_delta);

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
