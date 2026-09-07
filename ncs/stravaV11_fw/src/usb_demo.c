/*
 * Phase 9: replaces the CDC-ACM (virtual COM port) half of stravaV10's
 * source/usb/usb_cdc.c with Zephyr's native USB device stack.
 *
 * stravaV10's usb_cdc.c is actually a composite CDC-ACM + MSC (mass
 * storage) device -- the MSC half exposes the SD card/NOR flash as a USB
 * drive for unloading GPX/segment files. Both LUNs are ported below:
 * "SD" (disk_access device sd_fat_demo()/map_screen_demo/sd_stress_demo
 * already use) and "NOR" (the external QSPI flash, disk_access-registered
 * via Partition Manager -- see pm_static_<board>.yml and the
 * "nordic,pm-ext-flash" chosen entry in each board's devicetree file,
 * closing the gap Phase 4 left open). NOR is deliberately unformatted -- it's raw
 * data, not a filesystem, unlike the SD card -- so it shows up on the host
 * as a second, blank-looking USB drive; that's expected, not a bug.
 *
 * Real hazard worth calling out explicitly: this app's own demos
 * (sd_stress_demo, map_screen_demo) mount/read/write "SD" via the fs layer
 * on their own schedule, transiently, while MSC exposes the identical
 * physical disk directly to whatever the host OS decides to do with it at
 * any time -- there's no mutex or handoff between the two. On a real
 * product this would need gating (e.g. only exposing MSC in a dedicated
 * "connect to PC" mode, not during live logging), but that's out of scope
 * for this bring-up; here it's tolerated since the card is explicitly a
 * blank/scratch card, not something a corrupted FAT table would lose real
 * data from.
 *
 * Real-hardware note unlike every other "nothing attached" peripheral in
 * this port: this DK's own USB peripheral (not the J-Link's) needs a
 * physical USB cable plugged into the DK's second connector to be
 * host-visible at all (a new CDC-ACM device enumerating over on the
 * connected machine) -- see CLAUDE.md for what was actually checked in this
 * session vs what still needs that cable connected.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/logging/log.h>

#include "usbd_init.h"
#include "usb_demo.h"

LOG_MODULE_REGISTER(usb_demo, LOG_LEVEL_INF);

/* Disk name "SD" matches the zephyr,sdmmc-disk node's disk-name property in
 * both boards' devicetree (boards/nrf52840dk_nrf52840.overlay and
 * boards/vincent/stravav11/stravav11_nrf52840.dts) -- the same disk every
 * SD demo in main.c already opens by that name. */
USBD_DEFINE_MSC_LUN(sd, "SD", "stravaV11", "SD Card", "0.00");

/* Disk name "NOR" matches pm_static_<board>.yml's external_flash_disk
 * partition's extra_params.disk_name -- that's the actual name flashdisk.c
 * registers with disk_access for this partition, the devicetree
 * nor-msc-disk node's own "disk-name" property is a separate, unused
 * placeholder (see the comment next to that node). */
USBD_DEFINE_MSC_LUN(nor, "NOR", "stravaV11", "Raw NOR Flash", "0.00");

static void usbd_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable USB device support");
			}
		}
		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			(void)usbd_disable(ctx);
		}
	}

	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		LOG_INF("CDC-ACM DTR: %u (a host terminal opened/closed the port)", dtr);
	}
}

void usb_demo_start(void)
{
	const struct device *uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("CDC-ACM device not ready");
		return;
	}

	struct usbd_context *usbd = stravav11_usbd_init(usbd_msg_cb);

	if (usbd == NULL) {
		LOG_ERR("USB device init failed");
		return;
	}

	if (!usbd_can_detect_vbus(usbd)) {
		int err = usbd_enable(usbd);

		if (err) {
			LOG_ERR("Failed to enable USB device support (%d)", err);
			return;
		}
	}

	/* Deliberately not blocking on DTR here (the reference sample does,
	 * waiting for a host terminal to open the port) -- this is a
	 * one-shot bring-up smoke test, not an interactive console. */
	LOG_INF("USB device support enabled; CDC-ACM ready for a host to open");
}
