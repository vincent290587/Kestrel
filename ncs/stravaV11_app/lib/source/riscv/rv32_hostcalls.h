/*
 * Syscall-ID contract shared verbatim between the host (rv32_emu.c's
 * ecall dispatch, compiled for the real target arch) and guest programs
 * (compiled for rv32im by tools/riscv_guest/'s toolchain). Plain integer
 * constants only, no arch-specific types -- must compile cleanly under
 * both toolchains.
 *
 * Calling convention on ecall (standard RISC-V syscall ABI, not Linux-
 * specific, just the familiar one): a7 = id, a0-a5 = up to 6 args,
 * return value in a0. HOSTCALL_EXIT is handled inside rv32_emu.c itself
 * (halts the CPU, does not reach the host's registered handler) -- every
 * other id is application-defined and dispatched to whatever handler the
 * host registered via rv32_emu_set_hostcall_handler().
 */

#ifndef RV32_HOSTCALLS_H_
#define RV32_HOSTCALLS_H_

#define HOSTCALL_EXIT 0        /* a0 = exit code. Never reaches the host handler. */
#define HOSTCALL_DRAW_PIXEL 1  /* a0 = x, a1 = y, a2 = color (0/1). Returns 0. */
#define HOSTCALL_LED_SET 2     /* a0 = 0xRRGGBB. Returns 0. */
#define HOSTCALL_DEBUG_PRINT 3 /* a0 = guest ptr to bytes, a1 = length. Returns 0, or -1 if out of bounds. */

#endif /* RV32_HOSTCALLS_H_ */
