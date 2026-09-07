#!/usr/bin/env python3
"""
Modern replacement for the old LS027simulator.jar (Java/Swing) dev-tool
viewer -- speaks the exact same wire protocol stravaV11_app's own
adapters/gui_connector.c already sends (unchanged, no firmware-side
changes needed), just with a Python + Pygame client instead of a JVM.

Protocol (confirmed against gui_connector.c, itself confirmed against
the original Java source at
https://github.com/vincent290587/Netbeans/tree/master/LS027simulator):
  - This script is the TCP *client*; the native_sim process is the
    server (127.0.0.1:8080 by default -- matches
    GUI_DEFAULT_PORT/LS027_GUI_PORT in gui_connector.c).
  - Each frame is exactly 12004 bytes: 12000 bytes of framebuffer
    (400x240, 1bpp, LSB-first within each byte, row-major, 50
    bytes/row), then a 1-byte flag (unused), then 3 neopixel RGB bytes.
  - Bit convention on the wire (after gui_connector.c's own inversion of
    ZephyrGFX's 1=white/0=black): 1=black, 0=white.
  - No rotation is applied to the wire format -- this viewer applies
    the same 90-degree rotation to portrait (240 wide x 400 tall) the
    original Java tool did, for the same physical case-mounting reason
    (stravaV10's own hardcoded rotation=3).

Usage:
    python3 ls027_viewer.py [--port 8080]

Then run the native_sim binary with LS027_GUI=1 set (see
stravaV11_app/adapters/gui_connector.h) -- either order works, this
script just waits and retries the connection until the server is up.
Press 's' to save a PNG screenshot (stravaV11_ls027_<n>.png in the
current directory) -- same purpose as the old jar's screenshot key, no
X11/Wayland permission dance needed since this draws its own window
directly rather than going through a portal.
"""

import argparse
import socket
import sys
import time

import numpy as np
import pygame

FRAME_W = 400
FRAME_H = 240
FRAME_BYTES = (FRAME_W // 8) * FRAME_H  # 12000
PACKET_BYTES = FRAME_BYTES + 1 + 3  # + flag + neopixel RGB

WINDOW_W = FRAME_H  # portrait: raw height becomes screen width
WINDOW_H = FRAME_W  # raw width becomes screen height
NEOPIXEL_PANEL_W = 60


def recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("server closed the connection")
        buf.extend(chunk)
    return bytes(buf)


def decode_frame(packet: bytes):
    """Returns (portrait_rgb_array[h,w,3], (r,g,b)) from one 12004-byte packet."""
    raw = np.frombuffer(packet[:FRAME_BYTES], dtype=np.uint8).reshape(FRAME_H, FRAME_W // 8)
    bits = np.unpackbits(raw, axis=1, bitorder="little")  # shape (240, 400), 1=black/0=white

    # Same coordinate transform as the original Java tool's paint():
    # screen_x = FRAME_H-1-i (raw row, flipped), screen_y = raw column.
    portrait_bits = bits.T[:, ::-1]  # shape (400, 240) = (height, width)

    gray = np.where(portrait_bits == 1, 0, 255).astype(np.uint8)
    rgb = np.repeat(gray[:, :, None], 3, axis=2)

    neo = packet[FRAME_BYTES + 1 : FRAME_BYTES + 4]
    return rgb, (neo[0], neo[1], neo[2])


def main():
    # Without this, stdout is fully block-buffered whenever it isn't a
    # TTY (e.g. redirected to a log file), so "connecting..."/"frame
    # received" messages only appear once the process exits rather than
    # as they actually happen -- confusing for a tool whose whole point
    # is showing what's happening right now.
    sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080, help="TCP port gui_connector.c listens on")
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()

    print(f"ls027_viewer: connecting to {args.host}:{args.port} (retrying until the server is up)...")
    sock = None
    while sock is None:
        try:
            sock = socket.create_connection((args.host, args.port), timeout=2)
        except OSError:
            time.sleep(0.5)
    sock.settimeout(None)  # the 2s connect-retry timeout must not apply to later recv() calls
    print("ls027_viewer: connected")

    pygame.init()
    screen = pygame.display.set_mode((WINDOW_W + NEOPIXEL_PANEL_W, WINDOW_H))
    pygame.display.set_caption("LS027 viewer (stravaV11)")
    clock = pygame.time.Clock()

    shot_count = 0
    running = True
    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_s:
                fname = f"stravaV11_ls027_{shot_count}.png"
                pygame.image.save(screen, fname)
                shot_count += 1
                print(f"ls027_viewer: saved {fname}")

        try:
            packet = recv_exact(sock, PACKET_BYTES)
        except (ConnectionError, OSError) as e:
            print(f"ls027_viewer: connection lost ({e}), exiting")
            break

        rgb, neo = decode_frame(packet)
        print(f"ls027_viewer: frame received, neopixel={neo}")

        # pygame.surfarray wants (width, height, 3) with array[x, y] indexing --
        # our rgb array is (height, width, 3) row-major, so transpose the first
        # two axes.
        surf = pygame.surfarray.make_surface(np.transpose(rgb, (1, 0, 2)))
        screen.blit(surf, (0, 0))

        panel_rect = pygame.Rect(WINDOW_W, 0, NEOPIXEL_PANEL_W, WINDOW_H)
        pygame.draw.rect(screen, (30, 30, 30), panel_rect)
        if neo != (0, 0, 0):
            pygame.draw.circle(screen, neo, (WINDOW_W + NEOPIXEL_PANEL_W // 2, 40), 18)

        pygame.display.flip()
        clock.tick(30)

    pygame.quit()
    sock.close()


if __name__ == "__main__":
    main()
