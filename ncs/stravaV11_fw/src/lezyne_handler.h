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

/* Debug-only, unrelated to the L-protocol itself: hold /SD: mounted (or
 * release it) on demand, from cmd_console.c's "MSC MOUNT"/"MSC UNMOUNT".
 * Reuses this file's own dedicated FatFS thread purely as
 * already-provisioned infrastructure (see LEZ_CMD_STACK_SIZE's comment)
 * -- fs_mount()/fs_unmount() are real FatFS/SD work and must not run on
 * cmd_console.c's own caller thread (the system workqueue). USB MSC's
 * SCSI layer (usbd_msc_scsi.c's update_disk_info()) only reports real
 * capacity to the host while disk_access_status() is OK, which this
 * port's usual mount-do-one-thing-unmount convention leaves true only
 * for the instant of each transient operation -- not while the host is
 * idly polling. Mounting here and deliberately NOT unmounting until
 * asked keeps the card visible to a host doing USB MSC for as long as
 * needed. */
void lezyne_handler_msc_mount(void);
void lezyne_handler_msc_unmount(void);

#endif /* LEZYNE_HANDLER_H_ */
