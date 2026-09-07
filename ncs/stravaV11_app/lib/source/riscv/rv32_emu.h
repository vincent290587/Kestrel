/*
 * A small RV32IM interpreter for running untrusted/sandboxed guest code
 * inside the firmware -- the isolation mechanism is deliberately crude
 * and explicit rather than relying on an MPU/MMU: every memory access
 * (fetch, load, store) is bounds-checked against a single flat arena the
 * host allocates and owns, and rv32_emu_run() takes an instruction
 * budget so a guest that loops forever can't hang the caller. Guest code
 * talks to the host only through ecall (see rv32_hostcalls.h) -- there
 * is no other way out of the sandbox (no MMIO, no access to host memory
 * outside the arena).
 *
 * No ELF parsing: tools/riscv_guest/'s linker script places the guest's
 * entry stub at address 0 of the linked image, and rv32_elf... isn't
 * needed at all -- the build produces a flat binary (objcopy -O binary)
 * that's just memcpy'd straight into the arena at offset 0, with pc and
 * sp set directly by the loader. This is a deliberate v1 simplification,
 * fine for hand-built single-file guest programs; a real loader for
 * multiple/relocatable guests would need to parse the ELF instead.
 *
 * Implements the RV32I base integer ISA plus the M extension (mul/div) --
 * M was included so ordinary guest C arithmetic doesn't need libgcc soft-
 * multiply/divide routines linked in, keeping guest builds genuinely
 * freestanding with no libc/libgcc at all. No A/F/D/C extensions, no CSR
 * instructions (FENCE is accepted as a no-op; anything else undecodable
 * is RV32_ERR_ILLEGAL_INSN).
 */

#ifndef RV32_EMU_H_
#define RV32_EMU_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RV32_NUM_REGS 32

enum rv32_result {
	RV32_RUNNING = 0,       /* rv32_emu_step() only: instruction executed, not halted */
	RV32_HALTED_EXIT = 1,   /* guest called HOSTCALL_EXIT; cpu->exit_code is valid */
	RV32_ERR_MEM_FAULT = -1,       /* fetch/load/store address outside the arena */
	RV32_ERR_ILLEGAL_INSN = -2,    /* undecodable opcode/funct combination */
	RV32_ERR_MISALIGNED_PC = -3,   /* pc not 4-byte aligned (no C extension) */
	RV32_ERR_NO_HOSTCALL_HANDLER = -4, /* ecall with an id but no handler registered */
	RV32_ERR_INSN_BUDGET_EXCEEDED = -5, /* rv32_emu_run() only: hit max_instructions */
};

struct rv32_cpu;

/* Registered via rv32_emu_set_hostcall_handler(); called on every ecall
 * except HOSTCALL_EXIT (handled internally -- see rv32_hostcalls.h).
 * `id` and `a0`..`a5` come from the guest's a7/a0-a5 registers verbatim
 * (unsigned regs reinterpreted as int32_t, matching typical syscall-arg
 * conventions -- a callee that wants an address or unsigned count just
 * casts back). The return value is written into the guest's a0. */
typedef int32_t (*rv32_hostcall_fn)(struct rv32_cpu *cpu, void *user_data, int32_t id, int32_t a0,
				     int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5);

struct rv32_cpu {
	uint32_t x[RV32_NUM_REGS]; /* x[0] is wired to zero -- enforced on every write, not just at init */
	uint32_t pc;

	uint8_t *mem;      /* host-owned arena, size mem_size; guest address == byte offset into it */
	uint32_t mem_size;

	rv32_hostcall_fn hostcall_fn;
	void *hostcall_user_data;

	int32_t exit_code; /* valid once rv32_emu_step()/run() returns RV32_HALTED_EXIT */
};

/* Zeroes all registers and pc. Does not touch `mem` -- caller allocates/
 * zeroes/loads the arena separately (see rv32_emu_load_flat_binary()). */
void rv32_emu_reset(struct rv32_cpu *cpu, uint8_t *mem, uint32_t mem_size);

void rv32_emu_set_hostcall_handler(struct rv32_cpu *cpu, rv32_hostcall_fn fn, void *user_data);

/* Zeroes the whole arena, copies `len` bytes from `flat_binary` to
 * offset 0 (where the guest's entry stub lives, per this file's own
 * top-of-file note), sets pc = 0, and sets sp (x2) to the top of the
 * arena (mem_size, rounded down to 16-byte alignment per the standard
 * RISC-V calling convention). Returns 0 on success, -1 if `len` doesn't
 * fit in `mem_size`. */
int rv32_emu_load_flat_binary(struct rv32_cpu *cpu, const uint8_t *flat_binary, uint32_t len);

/* Executes exactly one instruction. Returns RV32_RUNNING to keep going,
 * RV32_HALTED_EXIT if this instruction was the guest's exit ecall, or a
 * negative rv32_result on a fault -- the cpu is left stopped at the
 * faulting instruction either way (pc not advanced past it). */
enum rv32_result rv32_emu_step(struct rv32_cpu *cpu);

/* Calls rv32_emu_step() in a loop until it stops returning RV32_RUNNING,
 * or `max_instructions` steps have executed (whichever comes first --
 * the budget is the isolation mechanism against an infinite-looping
 * guest, see this file's top-of-file note). Returns whatever the last
 * step returned, or RV32_ERR_INSN_BUDGET_EXCEEDED if the budget ran out
 * first. `*out_steps`, if non-NULL, receives the number of instructions
 * actually executed. */
enum rv32_result rv32_emu_run(struct rv32_cpu *cpu, uint32_t max_instructions,
			       uint32_t *out_steps);

#ifdef __cplusplus
}
#endif

#endif /* RV32_EMU_H_ */
