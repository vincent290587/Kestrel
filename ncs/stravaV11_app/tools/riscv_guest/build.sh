#!/usr/bin/env bash
# Builds each test*.c guest program into a flat rv32im binary (test*.bin)
# in this directory. Requires gcc-riscv64-unknown-elf (the bare-metal
# newlib toolchain -- installed via `apt install gcc-riscv64-unknown-elf`,
# not the Zephyr SDK's arm-zephyr-eabi one).
#
# This is a manual, standalone step -- not wired into `west build` for
# stravaV11_app. The emulator core (rv32_emu.c) only needs the resulting
# .bin files to exist on disk; regenerate them by re-running this script
# whenever a test*.c guest program changes.
set -euo pipefail

CC=riscv64-unknown-elf-gcc
OBJCOPY=riscv64-unknown-elf-objcopy
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

CFLAGS=(-march=rv32im -mabi=ilp32 -Os -Wall -Wextra
        -nostdlib -nostartfiles -ffreestanding -fno-builtin
        -fno-stack-protector -fomit-frame-pointer
        -T "$DIR/link.ld")

for src in "$DIR"/test*.c; do
	name="$(basename "$src" .c)"
	elf="$DIR/$name.elf"
	bin="$DIR/$name.bin"

	echo "building $name..."
	"$CC" "${CFLAGS[@]}" "$DIR/start.S" "$DIR/guest_rt.c" "$src" -o "$elf"
	"$OBJCOPY" -O binary "$elf" "$bin"
	echo "  -> $bin ($(stat -c%s "$bin") bytes)"
done
