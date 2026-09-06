/*
 * Phase 9: replaces the CDC-ACM (virtual COM port) half of stravaV10's
 * source/usb/usb_cdc.c with Zephyr's native USB device stack.
 *
 * stravaV10's usb_cdc.c is actually a composite CDC-ACM + MSC (mass
 * storage) device -- the MSC half exposes the SD card/NOR flash as a USB
 * drive for unloading GPX/segment files. That half isn't ported here: Zephyr's
 * USB MSC class needs a disk_access-registered block device, and Phase 4
 * deliberately didn't get the QSPI flash that far (NCS Partition Manager
 * issue, see CLAUDE.md) and the SD card has no card inserted anyway. A
 * real SD card or resolving that PM gap would unblock it -- noted as a gap,
 * not attempted here.
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
#include <zephyr/logging/log.h>

#include "usbd_init.h"
#include "usb_demo.h"

LOG_MODULE_REGISTER(usb_demo, LOG_LEVEL_INF);

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
