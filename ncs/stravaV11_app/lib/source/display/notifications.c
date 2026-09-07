/*
 * Ported unmodified from stravaV10's source/display/notifications.c --
 * this state machine only ever touches drv_ws2812_*() (see drv_ws2812.h)
 * and LOG_DEBUG (segger_wrapper.h), both already portable/adapter-backed
 * elsewhere in this port, so no logic changes were needed. Dropped the
 * original's "#include boards.h" -- nothing in this file actually used
 * anything from it, an unused leftover include in the original.
 *
 * Known pre-existing quirk, kept as-is for fidelity to the real hardware
 * behavior riders have already seen: notifications.h's SET_NEO_EVENT_BLUE
 * macro sets rgb[1] (green), and SET_NEO_EVENT_GREEN sets rgb[2] (blue) --
 * the two are swapped relative to their names. get_notifications_color()
 * below maps rgb[0..2] directly to R/G/B (bits 23:16/15:8/7:0), so
 * SET_NEO_EVENT_BLUE actually produces green light and vice versa. Not
 * fixed here -- flagged for the user to decide whether to correct it.
 */

#include "stdint.h"
#include <string.h>
#include <stdbool.h>
#include "drv_ws2812.h"
#include "notifications.h"
#include "segger_wrapper.h"

#define ON_STEPS_NB      5
#define ON_TICKS_DEFAULT 3

static neo_sb_init_params_t _params;
static neo_sb_seg_params m_seg_notif;
static bool leds_is_on;     /**< Flag for indicating if LEDs are on. */
static bool is_counting_up; /**< Flag for indicating if counter is incrementing or decrementing. */
static int32_t pause_ticks;
static uint32_t ratio;

static uint32_t get_notifications_color(uint8_t red, uint8_t green, uint8_t blue)
{
	uint32_t res = (red << 16) | (green << 8) | (blue);

	return res;
}

static void notifications_setColor(uint8_t red, uint8_t green, uint8_t blue)
{
	// update neopixel
	uint32_t color = get_notifications_color(red, green, blue);

	drv_ws2812_set_pixel_all(color);
}

void notifications_init(uint8_t pin_num)
{
	_params.max = 10;
	_params.min = 0;

	ratio = 0;
	leds_is_on = false;
	pause_ticks = 0;

	_params.rgb[0] = 0;
	_params.rgb[1] = 0;
	_params.rgb[2] = 0;

	_params.step = _params.max / ON_STEPS_NB;
	_params.on_time_ticks = ON_TICKS_DEFAULT;

	memset(&m_seg_notif, 0, sizeof(m_seg_notif));

	is_counting_up = true;

	drv_ws2812_init(pin_num);
}

static void _set_notify(uint8_t red, uint8_t green, uint8_t blue, uint8_t on_time)
{
	_params.rgb[0] = red;
	_params.rgb[1] = green;
	_params.rgb[2] = blue;

	// start process
	is_counting_up = true;
	leds_is_on = true;
	ratio = _params.min;
	pause_ticks = 0;

	_params.on_time_ticks = on_time == 0 ? ON_TICKS_DEFAULT : on_time;
}

void notifications_setNotify(sNeopixelOrders *orders)
{
	switch (orders->event_type) {
	case eNeoEventEmpty:
		break;

	case eNeoEventWeakNotify: {
		if (leds_is_on) {

			memset(orders, 0, sizeof(sNeopixelOrders));

			return;
		}
	}
		// no break

	case eNeoEventNotify: {
		_set_notify(orders->rgb[0], orders->rgb[1], orders->rgb[2], orders->on_time);

		memset(orders, 0, sizeof(sNeopixelOrders));
	} break;

	default:
		break;
	}
}

void notifications_segNotify(neo_sb_seg_params *orders)
{
	memcpy(&m_seg_notif, orders, sizeof(m_seg_notif));
}

uint8_t notifications_tasks(void)
{
	if (!leds_is_on) {

		static int on_time = 0;
		static int off_time = 0;

		// handle notifications
		if (m_seg_notif.active) {

			if (on_time == -1) {
				on_time = m_seg_notif.on_time;
				off_time = 0;
			}

			if (on_time) {

				LOG_DEBUG("Light ON");

				on_time--;
				notifications_setColor(m_seg_notif.rgb[0], m_seg_notif.rgb[1],
							m_seg_notif.rgb[2]);
			} else if (off_time) {

				LOG_DEBUG("Light OFF");

				off_time--;
				notifications_setColor(0, 0, 0);
			} else {

				on_time = m_seg_notif.on_time;
				off_time = m_seg_notif.off_time;
				notifications_setColor(0, 0, 0);
			}

		} else {

			on_time = -1;
			off_time = 0;
			notifications_setColor(0, 0, 0);
		}

	} else {

		// continue process
		if (pause_ticks <= 0) {
			if (is_counting_up) {
				if ((int)(ratio) >= (int)(_params.max - _params.step)) {
					// start decrementing.
					is_counting_up = false;
					pause_ticks = _params.on_time_ticks;
				} else {
					ratio += _params.step;
				}
			} else {
				if ((int)(ratio) <= (int)(_params.min + _params.step)) {
					// Min is reached, we are done
					// end process
					leds_is_on = false;
					ratio = 0;
				} else {
					ratio -= _params.step;
				}
			}
		} else {
			pause_ticks -= 1;
		}

		notifications_setColor(_params.rgb[0] * ratio * ratio / 255,
					_params.rgb[1] * ratio * ratio / 255,
					_params.rgb[2] * ratio * ratio / 255);
	}

	drv_ws2812_display(NULL, NULL);

	return 0;
}
