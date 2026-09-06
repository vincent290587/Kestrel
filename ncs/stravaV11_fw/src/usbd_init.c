/*
 * Phase 9: USB device setup, adapted from Zephyr's own
 * samples/subsys/usb/common/sample_usbd_init.c (the standard, correct way
 * to bring up the new USB device stack -- USBD_DEVICE_DEFINE, the
 * USBD_DESC_ macros, USBD_CONFIGURATION_DEFINE -- but de-sample-ified: that file leans on
 * Kconfig.sample_usbd, which is explicitly documented as only sourced for
 * in-tree Zephyr samples, not applications -- so this app defines its own
 * VID/PID/strings directly instead.
 *
 * VID 0x2FE3 below is still the Zephyr Project's placeholder VID, same as
 * the sample used -- fine for this bring-up/personal-project stage (same
 * category as Phase 5's ANT+ evaluation license key), but a real product
 * needs its own purchased VID before shipping.
 */

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>
#include <zephyr/logging/log.h>

#include "usbd_init.h"

/* Not "usbd_init" -- collides with Zephyr's own internal log module of
 * that same name in subsys/usb/device_next/usbd_init.c. */
LOG_MODULE_REGISTER(stravav11_usbd_init, LOG_LEVEL_INF);

#define STRAVAV11_USBD_VID 0x2FE3 /* Zephyr placeholder -- see file comment */
#define STRAVAV11_USBD_PID 0x0002
#define STRAVAV11_USBD_MANUFACTURER "stravaV11"
#define STRAVAV11_USBD_PRODUCT      "stravaV11 GPS computer"
#define STRAVAV11_USBD_MAX_POWER    125 /* mA, matches sample default */

static const char *const blocklist[] = {
	"dfu_dfu",
	NULL,
};

USBD_DEVICE_DEFINE(stravav11_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), STRAVAV11_USBD_VID,
		    STRAVAV11_USBD_PID);

USBD_DESC_LANG_DEFINE(stravav11_lang);
USBD_DESC_MANUFACTURER_DEFINE(stravav11_mfr, STRAVAV11_USBD_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(stravav11_product, STRAVAV11_USBD_PRODUCT);

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_CONFIGURATION_DEFINE(stravav11_fs_config, 0, STRAVAV11_USBD_MAX_POWER, &fs_cfg_desc);

struct usbd_context *stravav11_usbd_init(usbd_msg_cb_t msg_cb)
{
	int err;

	err = usbd_add_descriptor(&stravav11_usbd, &stravav11_lang);
	err = err ? err : usbd_add_descriptor(&stravav11_usbd, &stravav11_mfr);
	err = err ? err : usbd_add_descriptor(&stravav11_usbd, &stravav11_product);
	if (err) {
		LOG_ERR("Failed to add USB string descriptor(s) (%d)", err);
		return NULL;
	}

	err = usbd_add_configuration(&stravav11_usbd, USBD_SPEED_FS, &stravav11_fs_config);
	if (err) {
		LOG_ERR("Failed to add Full-Speed configuration (%d)", err);
		return NULL;
	}

	err = usbd_register_all_classes(&stravav11_usbd, USBD_SPEED_FS, 1, blocklist);
	if (err) {
		LOG_ERR("Failed to register USB classes (%d)", err);
		return NULL;
	}

	/* Single CDC-ACM interface: no IAD needed. */
	usbd_device_set_code_triple(&stravav11_usbd, USBD_SPEED_FS, 0, 0, 0);

	if (msg_cb != NULL) {
		err = usbd_msg_register_cb(&stravav11_usbd, msg_cb);
		if (err) {
			LOG_ERR("Failed to register USB message callback (%d)", err);
			return NULL;
		}
	}

	err = usbd_init(&stravav11_usbd);
	if (err) {
		LOG_ERR("Failed to initialize USB device support (%d)", err);
		return NULL;
	}

	return &stravav11_usbd;
}
