/*
 * Ride recording feature (2026-09-11 design, see CLAUDE.md/todo.md for the
 * full brainstorm trail). FIT-only (no CSV history writer, unlike
 * stravaV10's sd_save_pos_buffer()); records go to a raw, filesystem-free
 * region of the QSPI NOR chip during the ride (crash/power-loss-safe fast
 * tier, ride_storage_partition -- see stravav11_nrf52840.dts), and get
 * finalized into a real, spec-valid .FIT file on the SD card only once the
 * ride stops cleanly (or on a later retry, if SD wasn't available at stop
 * time). FRAM (not the old stravaV10 .noinit-RAM trick -- that only
 * survives a warm reset, and this exact board has lost power outright
 * more than once this project) is the sole source of truth for resume:
 * which of RIDE_NUM_SLOTS is active, how many bytes are already written,
 * and cached live totals for the on-screen display.
 */

#ifndef RIDE_RECORDER_H_
#define RIDE_RECORDER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RIDE_NUM_SLOTS 5
#define RIDE_SLOT_SIZE (2 * 1024 * 1024) /* 2MB/slot * 5 = the 10MB ride_storage_partition */

/* Call once at boot, after storage_demo() (SD + QSPI) has run. Reads the
 * FRAM ride state: resumes a slot left mid-RECORDING (device crashed or
 * lost power before ride_recorder_stop() ever ran -- new samples just
 * keep appending, no NOR/SD re-read needed, FRAM already has the cursor
 * and cached totals), and retries SD export for any slot left
 * PENDING_EXPORT (stopped cleanly, but the SD write itself didn't
 * succeed at the time -- no card, full card, etc). */
void ride_recorder_init(void);

/* Starts a new ride in the next free slot (round-robin over RIDE_NUM_SLOTS
 * for wear-spreading -- see ride_recorder.c). Returns false, logged, if
 * every slot is already RECORDING or PENDING_EXPORT -- refuses rather
 * than silently overwriting unexported data. */
bool ride_recorder_start(uint32_t start_unix_timestamp);

bool ride_recorder_is_active(void);

/* Call at whatever cadence samples should be recorded (1Hz suggested);
 * no-op if no ride is active. lat/lon are semicircles (FIT's own unit,
 * same *1.1930464E7f conversion stravaV10's fit_encode.cpp used from
 * degrees), alt_cm is centimetres, power_w is not yet written to the FIT
 * record (this profile's FIT_RECORD_MESG has no active power field, see
 * ride_recorder.c's own note) but is folded into nothing else either --
 * accepted as a parameter now so the call site doesn't need to change
 * again once that's added. distance_delta_m/climb_delta_m are this
 * sample's own contribution (not running totals) -- ride_recorder.c
 * accumulates and caches the totals in FRAM itself. */
void ride_recorder_add_sample(int32_t lat_semicircles, int32_t lon_semicircles, int32_t alt_cm,
			       uint8_t hrm_bpm, uint8_t cadence, uint16_t power_w,
			       uint32_t unix_timestamp, float distance_delta_m,
			       float climb_delta_m);

/* Ends the active ride: marks it PENDING_EXPORT in FRAM, then attempts to
 * finalize it straight to the SD card in one pass (see
 * ride_export_slot_to_sd() in ride_recorder.c for why a single pass is
 * possible here -- the final data_size is always known in advance).
 * Returns true only if that export actually succeeded (slot freed in
 * FRAM); on failure the slot stays PENDING_EXPORT for
 * ride_recorder_init()'s own retry on the next boot, or a future manual
 * retry hook. */
bool ride_recorder_stop(void);

/* Cached live totals for the currently-active ride (or the most recently
 * active one, if none is active right now) -- for the on-screen display.
 * Returns false, distance_m/climb_m untouched, if no ride has ever
 * been started this boot and nothing was resumed either. */
bool ride_recorder_get_live_totals(float *distance_m, float *climb_m);

/* Starts a self-resubmitting 1Hz k_work_delayable that pulls GPS/HRM/BSC
 * data (gps_demo.h/hrm_demo.h/bsc_demo.h's existing getters) into
 * ride_recorder_add_sample() whenever a ride is active -- a no-op tick
 * otherwise. Call once from main(), same "start once, runs itself"
 * convention as poll_demo.c/sensor_screen_demo.c. */
void ride_recorder_start_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* RIDE_RECORDER_H_ */
