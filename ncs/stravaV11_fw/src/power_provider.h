#ifndef POWER_PROVIDER_H_
#define POWER_PROVIDER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Manual-lap-feature follow-up (2026-09-12): a real power meter can show
 * up on ANT+ (fec_demo.h) or BLE (ble_demo.h's Cycling Power Service
 * client) -- this hides the choice behind one call so ride_recorder.c
 * and model_glue.c don't each need their own copy of that logic.
 *
 * Source precedence (explicit user direction): BLE whenever it has a
 * fresh reading. But this deliberately does NOT recompute "which source
 * is best" from scratch on every call -- it only re-evaluates once the
 * *currently selected* source's own reading goes stale (see
 * power_provider.c's POWER_PROVIDER_STALE_MS), so two simultaneously
 * live sources (e.g. a meter briefly visible on both radios) can't cause
 * this to flip-flop tick to tick.
 *
 * Same convention as every other live-data getter in this port
 * (model_glue.h's own doc comment): true and *watts written only when a
 * source is actually live right now; false and *watts left untouched
 * otherwise -- so a caller that only wants "0 if nothing's connected"
 * can just pre-zero its own local and ignore the return value, exactly
 * like ride_recorder.c's existing hrm_demo_is_paired() ? ... : 0 style
 * did before this existed.
 */
bool power_provider_get_watts(uint16_t *watts);

#ifdef __cplusplus
}
#endif

#endif /* POWER_PROVIDER_H_ */
