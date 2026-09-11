#ifndef LEZYNE_HANDLER_H_
#define LEZYNE_HANDLER_H_

#include <stdint.h>

/* L-protocol command dispatch for the Lezyne feature -- the Zephyr-native
 * replacement for stravaV10's rf/app_packets_handler.c, trimmed to this
 * port's recommended first-pass scope (see CLAUDE.md/todo.md 2026-09-11):
 * FIT file list/download/delete only. Segment sync, navigation/route
 * upload, and phone-notification passthrough are NOT implemented --
 * segments are already descoped project-wide (2026-09-07 decision), and
 * navigation/notifications need real dependencies (Model.h-equivalent
 * orchestration) this port doesn't have yet. Unhandled/descoped commands
 * are logged, not silently ignored, so a real GPS Ally session shows
 * exactly what it tried to do that this firmware doesn't support yet.
 *
 * Real, working synergy with the ride-recording feature: FIT file list/
 * download/delete are served directly from the same SD-card
 * "%08lX.FIT" files ride_recorder.c's RIDE STOP export produces -- no
 * new file format or naming scheme needed.
 *
 * Known limitation: a RequestFitFileList/RequestFitFileDelete arriving
 * while a FIT download is in progress fails cleanly (fs_mount("/SD:")
 * returns -EBUSY, since Zephyr's fs layer allows only one mount per path)
 * rather than actually serving it -- not expected in normal GPS Ally usage
 * (one operation at a time), not fixed here.
 */

void lezyne_handler_init(void);

void lezyne_handler_on_connected(void);
void lezyne_handler_on_disconnected(void);
void lezyne_handler_on_rx(const uint8_t *data, uint16_t length);

void lezyne_handler_log_status(void);

#endif /* LEZYNE_HANDLER_H_ */
