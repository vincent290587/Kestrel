#!/usr/bin/env python3
"""Update stravaV11_fw over BLE using MCUmgr/SMP, from this host machine.

Real, on-device support for this (advertising + Zephyr's own
CONFIG_MCUMGR_GRP_IMG/OS + CONFIG_MCUMGR_TRANSPORT_BT) has existed since
Phase 10 -- see stravaV11_fw/src/smp_demo.{h,c} and CLAUDE.md's Phase 10
section. Until now the actual host-side upload/reset/verify cycle used to
validate that was run ad hoc against the `smpclient` library, with no
script kept in the repo. This is that script.

Requires the `smpclient[ble]` extra (already in this repo's venv -- see
`source .venv/bin/activate` from the repo root) and a Bluetooth adapter on
this machine. No `mcumgr` CLI needed.

Usage:
    python3 tools/dfu_update.py scan
    python3 tools/dfu_update.py status [--address AA:BB:CC:DD:EE:FF]
    python3 tools/dfu_update.py upload <build-dir>/stravaV11_fw/zephyr/zephyr.signed.bin
    python3 tools/dfu_update.py confirm [--address AA:BB:CC:DD:EE:FF]

With no --address, the board is found by scanning for its advertised name
(CONFIG_BT_DEVICE_NAME, "stravaV11") among devices advertising the SMP
service UUID. Pass --address to skip scanning, or --name to match a
different advertised name.

See COMMANDS.md's "Firmware update over BLE (DFU)" section for the full
walkthrough, including what "test" vs "confirm" means and why images
aren't auto-confirmed by this script.
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import sys
from pathlib import Path

from smpclient import SMPClient
from smpclient.generics import error
from bleak.exc import BleakError
from smp.image_management import ImageState
from smpclient.transport import SMPTransportDisconnected
from smpclient.mcuboot import IMAGE_TLV, ImageInfo
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import ResetWrite
from smpclient.transport.ble import SMPBLETransport

# Real, reproducible failure mode observed on this project's own hardware/host
# combo: the *first* GATT connect or write after a fresh BLE scan frequently
# fails at the host BlueZ/bleak level (service discovery aborting, or the
# very first upload chunk causing an outright disconnect) -- a bare retry of
# the same operation has succeeded every time this was hit. Not a firmware
# bug: RTT captures during these failures show the board staying healthy
# throughout. See COMMANDS.md's DFU section.
_RETRYABLE_EXCEPTIONS = (BleakError, SMPTransportDisconnected, TimeoutError, OSError)

DEFAULT_DEVICE_NAME = "stravaV11"


def _short_hash(h: bytes | None) -> str:
    return h.hex()[:16] + "..." if h else "(none)"


def _print_image_states(images: list[ImageState]) -> None:
    for img in images:
        flags = [
            name
            for name, val in (
                ("bootable", img.bootable),
                ("pending", img.pending),
                ("confirmed", img.confirmed),
                ("active", img.active),
                ("permanent", img.permanent),
            )
            if val
        ]
        print(
            f"  slot={img.slot} version={img.version} "
            f"hash={_short_hash(img.hash)} [{' '.join(flags) or '-'}]"
        )


async def _scan(timeout_s: float):
    print(f"Scanning for {timeout_s:.0f}s for SMP-over-BLE devices...")
    return await SMPBLETransport.scan(timeout=int(timeout_s))


async def _resolve_address(address: str | None, name: str | None, timeout_s: float) -> str:
    if address:
        return address

    devices = await _scan(timeout_s)
    name = name or DEFAULT_DEVICE_NAME
    matches = [d for d in devices if d.name and name.lower() in d.name.lower()]

    if not matches:
        listing = "\n".join(f"  {d.address}  {d.name or '(no name)'}" for d in devices) or "  (none found)"
        raise SystemExit(
            f'No SMP device matching name "{name}" found. Devices seen:\n{listing}\n'
            "Pass --address to connect directly, or --name to match something else."
        )
    if len(matches) > 1:
        listing = "\n".join(f"  {d.address}  {d.name}" for d in matches)
        raise SystemExit(f"Multiple matching SMP devices found, pass --address:\n{listing}")

    print(f"Found {matches[0].name} at {matches[0].address}")
    return matches[0].address


async def cmd_scan(args: argparse.Namespace) -> None:
    devices = await _scan(args.scan_timeout)
    if not devices:
        print("No SMP-over-BLE devices found.")
        return
    for d in devices:
        print(f"{d.address}  {d.name or '(no name)'}")


async def cmd_status(args: argparse.Namespace) -> None:
    address = await _resolve_address(args.address, args.name, args.scan_timeout)
    async with SMPClient(SMPBLETransport(), address, timeout_s=args.conn_timeout) as client:
        response = await client.request(ImageStatesRead())
        if error(response):
            raise SystemExit(f"ImageStatesRead failed: {response}")
        print(f"Connected to {address}")
        _print_image_states(response.images)


async def cmd_confirm(args: argparse.Namespace) -> None:
    address = await _resolve_address(args.address, args.name, args.scan_timeout)
    async with SMPClient(SMPBLETransport(), address, timeout_s=args.conn_timeout) as client:
        response = await client.request(ImageStatesWrite(confirm=True))
        if error(response):
            raise SystemExit(f"ImageStatesWrite(confirm=True) failed: {response}")
        print("Confirmed the currently running image as permanent:")
        _print_image_states(response.images)


async def cmd_upload(args: argparse.Namespace) -> None:
    image_path = Path(args.image)
    image_bytes = image_path.read_bytes()

    try:
        expected_hash = ImageInfo.load_file(str(image_path)).get_tlv(IMAGE_TLV.SHA256).value
    except Exception as e:
        raise SystemExit(f"{image_path} doesn't look like a signed MCUboot image ({e})")

    print(
        f"Image: {image_path} ({len(image_bytes)} bytes, "
        f"header sha256={expected_hash.hex()[:16]}...)"
    )

    address = await _resolve_address(args.address, args.name, args.scan_timeout)

    async with SMPClient(SMPBLETransport(), address, timeout_s=args.conn_timeout) as client:
        print(f"Connected to {address}")

        baseline = await client.request(ImageStatesRead())
        if error(baseline):
            raise SystemExit(f"ImageStatesRead failed: {baseline}")
        print("Before upload:")
        _print_image_states(baseline.images)

        print(f"Uploading {len(image_bytes)} bytes (test upgrade, not made permanent yet)...")
        last_pct = -1
        async for offset in client.upload(image_bytes):
            pct = offset * 100 // len(image_bytes)
            if pct != last_pct:
                print(f"  {pct:3d}% ({offset}/{len(image_bytes)})", end="\r", flush=True)
                last_pct = pct
        print()

        after = await client.request(ImageStatesRead())
        if error(after):
            raise SystemExit(f"ImageStatesRead failed: {after}")
        print("After upload:")
        _print_image_states(after.images)

        uploaded = next(
            (img for img in after.images if not img.active and img.hash == expected_hash), None
        )
        if uploaded is None:
            raise SystemExit(
                "No inactive slot reports the uploaded image's hash after upload -- something's wrong."
            )
        print("Uploaded image hash matches the signed file. Good.")

        # A fresh upload is NOT automatically marked pending/test -- that needs
        # its own explicit ImageStatesWrite(hash=..., confirm=False) request
        # (this is real MCUmgr protocol behavior, not a Zephyr quirk: the
        # upload and "mark for test" steps are always separate mcumgr
        # operations, e.g. `mcumgr image upload` then `mcumgr image test`).
        print("Marking the uploaded image for test boot...")
        marked = await client.request(ImageStatesWrite(hash=expected_hash, confirm=False))
        if error(marked):
            raise SystemExit(f"ImageStatesWrite(test) failed: {marked}")
        _print_image_states(marked.images)

        pending = next((img for img in marked.images if img.pending), None)
        if pending is None or pending.hash != expected_hash:
            raise SystemExit("Marked for test but no image reports pending with the expected hash.")

        if args.no_reset:
            print(
                "--no-reset given: image is staged (pending) but not booted. "
                "Reset the device (or rerun without --no-reset) to trigger the MCUboot swap."
            )
            return

        print("Requesting reset to trigger the swap...")
        try:
            await client.request(ResetWrite())
        except Exception:
            # Expected: the device reboots mid-response and the BLE link drops
            # before a reply can come back.
            pass

    if args.no_verify:
        print("--no-verify given: not reconnecting to check the swap.")
        return

    print(f"Waiting {args.reboot_wait:.0f}s for the device to reboot and re-advertise...")
    await asyncio.sleep(args.reboot_wait)

    print("Reconnecting to verify the swap...")
    address = await _resolve_address(args.address, args.name, args.scan_timeout)
    async with SMPClient(SMPBLETransport(), address, timeout_s=args.conn_timeout) as client:
        post = await client.request(ImageStatesRead())
        if error(post):
            raise SystemExit(f"Post-swap ImageStatesRead failed: {post}")
        print("After swap:")
        _print_image_states(post.images)

        active = next((img for img in post.images if img.active), None)
        if active is None:
            raise SystemExit("No active image reported after reboot -- something's wrong.")
        if active.hash != expected_hash:
            raise SystemExit(
                f"Swap did not produce the expected image: active hash "
                f"{_short_hash(active.hash)}, expected {_short_hash(expected_hash)}. "
                "MCUboot may have reverted -- the new image might be failing to boot/confirm."
            )
        print("Active image hash matches the uploaded image. Swap succeeded.")

        if active.confirmed:
            print("Already confirmed permanent (nothing more to do).")
        elif args.confirm:
            confirm_response = await client.request(ImageStatesWrite(confirm=True))
            if error(confirm_response):
                raise SystemExit(f"ImageStatesWrite(confirm=True) failed: {confirm_response}")
            print("Confirmed as permanent:")
            _print_image_states(confirm_response.images)
        else:
            print(
                "Not yet confirmed permanent. It will revert to the old image on the next "
                "reset unless the app calls boot_write_img_confirmed() itself, or you run:\n"
                f"  python3 {sys.argv[0]} confirm"
            )


async def _run_with_retries(handler, args: argparse.Namespace) -> None:
    attempts = max(1, args.retries)
    for attempt in range(1, attempts + 1):
        try:
            await handler(args)
            return
        except _RETRYABLE_EXCEPTIONS as e:
            if attempt == attempts:
                raise
            print(f"BLE hiccup ({e.__class__.__name__}: {e}) -- retrying ({attempt}/{attempts})...")


def main() -> None:
    # Same fix as stravaV11_app/tools/ls027_viewer.py: stdout is fully
    # block-buffered when it isn't a TTY (e.g. piped/captured), which
    # otherwise makes our own progress/status prints appear wildly
    # out-of-order relative to the smpclient library's unbuffered logging.
    sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--address", help="BLE address of the device (skips scanning)")
    parser.add_argument(
        "--name", default=DEFAULT_DEVICE_NAME, help=f"advertised name to scan for (default: {DEFAULT_DEVICE_NAME})"
    )
    parser.add_argument("--scan-timeout", type=float, default=5.0, help="BLE scan duration in seconds")
    parser.add_argument(
        "--conn-timeout",
        type=float,
        default=15.0,
        help="connect/request timeout in seconds (default 15 -- smpclient's own 2.5s default "
        "is too tight for this board, real connects have taken several seconds)",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=3,
        help="retry the whole command this many times on a BLE connect/transport failure "
        "(default 3 -- on real hardware, the first connect/GATT-write attempt after a fresh "
        "scan frequently fails at the host BlueZ/bleak level and a bare retry succeeds; this "
        "isn't a firmware issue, see COMMANDS.md)",
    )

    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("scan", help="list nearby SMP-over-BLE devices")
    sub.add_parser("status", help="show image slot states on the device")
    sub.add_parser("confirm", help="mark the currently running image as permanent")

    p_upload = sub.add_parser("upload", help="upload a signed image, reset, and verify the swap")
    p_upload.add_argument("image", help="path to a signed image, e.g. <build-dir>/stravaV11_fw/zephyr/zephyr.signed.bin")
    p_upload.add_argument(
        "--no-reset", action="store_true", help="upload only; don't reset the device afterward"
    )
    p_upload.add_argument(
        "--no-verify", action="store_true", help="reset but don't reconnect afterward to verify the swap"
    )
    p_upload.add_argument(
        "--reboot-wait",
        type=float,
        default=45.0,
        help="seconds to wait after reset before reconnecting (default 45 -- main() brings up "
        "many other subsystems before BLE/SMP advertising starts; measured as long as ~70s "
        "on this board, so don't set this too low)",
    )
    p_upload.add_argument(
        "--confirm",
        action="store_true",
        help="mark the new image permanent immediately after a verified swap "
        "(default: leave it as a test boot -- it reverts on the next reset until confirmed)",
    )

    args = parser.parse_args()

    handlers = {
        "scan": cmd_scan,
        "status": cmd_status,
        "confirm": cmd_confirm,
        "upload": cmd_upload,
    }
    asyncio.run(_run_with_retries(handlers[args.command], args))


if __name__ == "__main__":
    main()
