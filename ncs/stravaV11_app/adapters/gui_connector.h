/*
 * Bridge to the standalone LS027simulator.jar Java tool (source:
 * https://github.com/vincent290587/Netbeans/tree/master/LS027simulator),
 * so ZephyrGFX's framebuffer can actually be *seen* during development,
 * not just pixel-counted -- see stravaV11_app's own main.cpp smoke test
 * comments for why pixel counts alone can't prove a render is visually
 * correct.
 *
 * native_sim only (a real Linux process, so real POSIX sockets are
 * available) -- a no-op on the DK build, which stravaV11_app also
 * targets. Protocol and role match stravaV10's own
 * TDD/sys/GUI_connector.cpp exactly (confirmed against the Java
 * source, not guessed): this process is the TCP *server* on
 * 127.0.0.1:8080, the prebuilt jar connects as a *client* -- so the
 * existing jar needs no changes at all to work with this port.
 */

#ifndef GUI_CONNECTOR_H_
#define GUI_CONNECTOR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the bridge only if the LS027_GUI environment variable is set
 * (kept out of Kconfig entirely -- this is a per-run dev-tool toggle,
 * not a build-time option, so normal automated smoke-test runs are
 * never blocked waiting for a GUI that isn't there). When enabled,
 * blocks waiting for LS027simulator.jar to connect. No-op if the env
 * var isn't set, or on non-native_sim builds. */
void gui_connector_init(void);

/* Sends one framebuffer update. `buf` must be ZephyrGFX's own
 * getBuffer() (400x240, LSB-first, 1=white/0=black, per its own
 * top-of-file comment) -- this function inverts polarity to what
 * stravaV10's TDD simulator protocol expects (0=white/1=black, matching
 * its own LS027_PIXEL_BLACK=1 convention) before sending; no other
 * transform is needed since the raw dimensions/bit-order already match
 * (the simulator applies its own rotation for display, on the receiving
 * end). No-op if gui_connector_init() was never called, found no
 * client, or wasn't enabled. */
void gui_connector_update_ls027(const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* GUI_CONNECTOR_H_ */
