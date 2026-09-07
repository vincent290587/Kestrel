#include "gui_connector.h"

#if defined(CONFIG_ARCH_POSIX)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GUI_DEFAULT_PORT 8080
#define LS027_FRAME_SIZE 12000 /* ZEPHYR_GFX_BUFFER_SIZE / stravaV10's LS027_BUFFER_SIZE */

/* Matches stravaV10's own sGUIComm layout exactly (TDD/sys/GUI_connector.cpp):
 * framebuffer, then a 1-byte flag (read but unused by the Java client), then
 * 3 neopixel RGB bytes. Not used here (no WS2812 rendering in this bridge),
 * always sent as zero. */
#define GUI_PACKET_SIZE (LS027_FRAME_SIZE + 1 + 3)

static int s_client_fd = -1;

void gui_connector_init(void)
{
	if (!getenv("LS027_GUI")) {
		return;
	}

	/* LS027simulator.jar hardcodes 8080 (no override in its own source)
	 * -- this env var exists only so this port can be tested against a
	 * throwaway copy of the jar on a machine where 8080 is already
	 * taken by something else; leave it unset for real use against the
	 * unmodified, prebuilt jar. */
	int port = GUI_DEFAULT_PORT;
	const char *port_env = getenv("LS027_GUI_PORT");

	if (port_env) {
		port = atoi(port_env);
	}

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);

	if (server_fd < 0) {
		perror("gui_connector: socket");
		return;
	}

	int opt = 1;

	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in address;

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr("127.0.0.1");
	address.sin_port = htons(port);

	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("gui_connector: bind (is another instance already running?)");
		close(server_fd);
		return;
	}

	if (listen(server_fd, 1) < 0) {
		perror("gui_connector: listen");
		close(server_fd);
		return;
	}

	printf("gui_connector: waiting for LS027simulator.jar to connect on 127.0.0.1:%d...\n",
	       port);

	socklen_t addrlen = sizeof(address);

	s_client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
	close(server_fd); /* one client only, matching the Java tool's own single-instance use */

	if (s_client_fd < 0) {
		perror("gui_connector: accept");
		return;
	}

	printf("gui_connector: LS027simulator.jar connected\n");
}

void gui_connector_update_ls027(const uint8_t *buf, size_t len)
{
	if (s_client_fd < 0 || len != LS027_FRAME_SIZE) {
		return;
	}

	static uint8_t packet[GUI_PACKET_SIZE];

	/* ZephyrGFX: 1=white/0=black (see its own top-of-file comment).
	 * stravaV10's TDD simulator/protocol: LS027_PIXEL_BLACK=1, i.e. the
	 * opposite polarity -- confirmed against the actual Java source
	 * (isKthBitSet() false -> WHITE, true -> BLACK). Bit order and
	 * row-major 400x240 layout already match (both LSB-first), so
	 * inverting each byte is the only transform needed. */
	for (size_t i = 0; i < LS027_FRAME_SIZE; i++) {
		packet[i] = (uint8_t)~buf[i];
	}
	packet[LS027_FRAME_SIZE] = 0; /* data_flags */
	packet[LS027_FRAME_SIZE + 1] = 0; /* neopixel R */
	packet[LS027_FRAME_SIZE + 2] = 0; /* neopixel G */
	packet[LS027_FRAME_SIZE + 3] = 0; /* neopixel B */

	send(s_client_fd, packet, sizeof(packet), 0);
}

#else

void gui_connector_init(void)
{
}

void gui_connector_update_ls027(const uint8_t *buf, size_t len)
{
	(void)buf;
	(void)len;
}

#endif
