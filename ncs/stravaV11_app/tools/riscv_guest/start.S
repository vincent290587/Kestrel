/*
 * Guest entry stub -- placed at address 0 by link.ld's .text.start
 * section, so this is the first instruction the emulator executes. No
 * crt0/libc startup: the host loader sets sp directly (top of the
 * arena, see rv32_emu_load_flat_binary()), so all this needs to do is
 * call main() and turn its return value into a clean exit.
 */
	.section .text.start
	.global _start
_start:
	call main
	/* a0 already holds main()'s return value. HOSTCALL_EXIT is 0 --
	 * must match rv32_hostcalls.h; a plain .S file can't include that
	 * C header, so this constant is duplicated here deliberately. */
	li a7, 0
	ecall
1:	j 1b /* unreachable: the host halts on the exit ecall above */
