#ifndef USBD_INIT_H_
#define USBD_INIT_H_

#include <zephyr/usb/usbd.h>

/*
 * Adapted from Zephyr's own zephyr/samples/subsys/usb/common/sample_usbd.h:
 * that helper explicitly says "you should not use it in your own
 * application" (it's wired to a Kconfig menu -- Kconfig.sample_usbd --
 * that's only sourced for in-tree samples, plus it defaults to the Zephyr
 * Project USB VID, which is off-limits outside Zephyr's own samples). This
 * is our own copy with the same VID caveat -- see usbd_init.c.
 */
struct usbd_context *stravav11_usbd_init(usbd_msg_cb_t msg_cb);

#endif /* USBD_INIT_H_ */
