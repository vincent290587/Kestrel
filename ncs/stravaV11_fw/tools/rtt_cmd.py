#!/usr/bin/env python3
"""
Headless RTT terminal for the stravaV11 board: connects, optionally writes a
command line to down channel 0, then reads and prints up channel 0 for a
fixed duration. No fake-TTY needed (unlike `pyocd rtt`'s interactive KBHit
loop, which requires a real TTY for keyboard input) since this drives
pyocd's RTTControlBlock/RTTUpChannel/RTTDownChannel classes directly --
useful for scripting a command into gps_sim_demo.c's RTT-based
"SIM START"/"SIM STOP" console (see CLAUDE.md, Phase 11) without a host
serial console at all.

Usage: rtt_cmd.py <probe-serial> <capture-seconds> [command-to-send]
"""
import sys
import time

from pyocd.core.helpers import ConnectHelper
from pyocd.debug.rtt import RTTControlBlock


def main():
    probe_serial = sys.argv[1]
    capture_seconds = float(sys.argv[2])
    command = sys.argv[3] if len(sys.argv) > 3 else None

    session = ConnectHelper.session_with_chosen_probe(
        unique_id=probe_serial, blocking=False, target_override="nrf52840")
    if session is None:
        print("ERROR: no target device available", file=sys.stderr)
        sys.exit(1)

    with session:
        target = session.board.target
        cb = RTTControlBlock.from_target(target)
        cb.start()
        print(f"{len(cb.up_channels)} up channels, {len(cb.down_channels)} down channels")
        up = cb.up_channels[0]
        down = cb.down_channels[0] if cb.down_channels else None

        target.resume()

        if command is not None and down is not None:
            data = (command + "\r\n").encode()
            written = down.write(data, blocking=True)
            print(f"wrote {written}/{len(data)} bytes to down channel 0: {command!r}")

        end = time.time() + capture_seconds
        while time.time() < end:
            chunk = up.read()
            if chunk:
                sys.stdout.write(chunk.decode(errors="replace"))
                sys.stdout.flush()
            else:
                time.sleep(0.05)


if __name__ == "__main__":
    main()
