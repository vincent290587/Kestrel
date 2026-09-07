
#ifndef NOTIFICATIONS_H_
#define NOTIFICATIONS_H_

#include <stdint.h>
#include "mk64f_parser.h"


typedef enum {
	eNotificationTypePartial, //!< eNotificationTypePartial
	eNotificationTypeComplete,//!< eNotificationTypeComplete
} eNotificationType;

typedef enum {
	eNeoEventEmpty       = 0U,
	eNeoEventWeakNotify  = 1U,
	eNeoEventNotify      = 2U,
} eNeoEventType;

typedef struct
{
    uint8_t         max;  /**< Maximum */
    uint8_t         min;  /**< Minimum */
    uint8_t         step; /**< step. */
    uint8_t         rgb[3];
    uint32_t        on_time_ticks;   /**< Ticks to stay in high impulse state. */
} neo_sb_init_params_t;

typedef struct {
	uint8_t         active;
	uint8_t         on_time;
	uint8_t         off_time;
	uint8_t         rgb[3];
} neo_sb_seg_params;

//////////////////////////     MACROS

/* Fixed 2026-09-07: BLUE and GREEN were swapped in stravaV10's original
 * header (BLUE set rgb[1], GREEN set rgb[2]) -- get_notifications_color()
 * in notifications.c maps rgb[0..2] directly to R/G/B, so the original
 * macros produced the wrong color by name (confirmed on real hardware:
 * SET_NEO_EVENT_GREEN lit the LED blue). Corrected here so each macro's
 * name matches what it actually shows. */

#define SET_NEO_EVENT_RED(X, Y, TIME) \
	X.event_type = Y; \
	X.on_time = 5; \
	X.rgb[0] = 0xFF; \
	X.rgb[1] = 0x00; \
	X.rgb[2] = 0x00

#define SET_NEO_EVENT_GREEN(X, Y, TIME) \
	X.event_type = Y; \
	X.on_time = 5; \
	X.rgb[0] = 0x00; \
	X.rgb[1] = 0xFF; \
	X.rgb[2] = 0x00

#define SET_NEO_EVENT_BLUE(X, Y, TIME) \
	X.event_type = Y; \
	X.on_time = 5; \
	X.rgb[0] = 0x00; \
	X.rgb[1] = 0x00; \
	X.rgb[2] = 0xFF

//////////////////////////     FUNCTIONS

#ifdef __cplusplus
extern "C" {
#endif

void notifications_init(uint8_t pin_num);

void notifications_setNotify(sNeopixelOrders* orders);

void notifications_segNotify(neo_sb_seg_params* orders);

uint8_t notifications_tasks(void);

#ifdef __cplusplus
}
#endif

#endif /* NOTIFICATIONS_H_ */
