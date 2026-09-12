#ifndef CADENCE_PROVIDER_H_
#define CADENCE_PROVIDER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Same idea as power_provider.h/hrm_provider.h, for cadence -- except
 * cadence has a real *third* source, the dedicated ANT+ speed/cadence
 * sensor (bsc_demo.h), alongside the two power meters' own incidental
 * cadence fields (fec_demo.h's ANT+ FE-C page 25, ble_demo.h's BLE
 * Cycling Power Measurement crank-revolution delta).
 *
 * Source precedence (explicit user direction, same session as
 * power_provider.h): BLE first, then the dedicated ANT+ BSC sensor
 * (presumably more accurate/responsive than a power meter's secondary
 * field), then ANT+ FE-C last. Same sticky-until-stale behavior as the
 * other two providers -- see power_provider.h's own comment for why.
 */
bool cadence_provider_get_rpm(uint32_t *rpm);

#ifdef __cplusplus
}
#endif

#endif /* CADENCE_PROVIDER_H_ */
