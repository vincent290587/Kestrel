#ifndef HRM_PROVIDER_H_
#define HRM_PROVIDER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Manual-lap-feature follow-up (2026-09-12): same idea as
 * power_provider.h, for heart rate -- a strap can show up on ANT+
 * (hrm_demo.h) or BLE (ble_demo.h's Heart Rate Service client). See
 * power_provider.h's own comment for the precedence rule (BLE preferred,
 * sticky until the *selected* source goes stale) and the true/false +
 * untouched-on-false convention.
 */
bool hrm_provider_get_bpm(uint8_t *bpm);

#ifdef __cplusplus
}
#endif

#endif /* HRM_PROVIDER_H_ */
