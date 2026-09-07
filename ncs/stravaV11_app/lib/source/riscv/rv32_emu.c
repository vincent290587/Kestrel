#include "rv32_emu.h"
#include "rv32_hostcalls.h"

#include <string.h>

static void reg_set(struct rv32_cpu *cpu, uint32_t rd, uint32_t val)
{
	if (rd != 0) { /* x0 is hardwired to zero -- writes to it are silently discarded */
		cpu->x[rd] = val;
	}
}

/* Bounds-checked access to the flat arena. `addr` is a guest address,
 * which for this loader is the same as a byte offset into cpu->mem (see
 * rv32_emu.h's top-of-file note). Rejects any access that would read/
 * write past the end of the arena, including the addr+size overflow
 * case for an addr near UINT32_MAX. Misaligned accesses are allowed
 * (not trapped) -- see rv32_emu.h. */
static int mem_bounds_ok(const struct rv32_cpu *cpu, uint32_t addr, uint32_t size)
{
	if (addr > cpu->mem_size || size > cpu->mem_size - addr) {
		return 0;
	}
	return 1;
}

static int mem_read(const struct rv32_cpu *cpu, uint32_t addr, uint32_t size, uint32_t *out)
{
	if (!mem_bounds_ok(cpu, addr, size)) {
		return 0;
	}
	uint32_t v = 0;

	memcpy(&v, &cpu->mem[addr], size);
	*out = v;
	return 1;
}

static int mem_write(struct rv32_cpu *cpu, uint32_t addr, uint32_t size, uint32_t val)
{
	if (!mem_bounds_ok(cpu, addr, size)) {
		return 0;
	}
	memcpy(&cpu->mem[addr], &val, size);
	return 1;
}

void rv32_emu_reset(struct rv32_cpu *cpu, uint8_t *mem, uint32_t mem_size)
{
	memset(cpu->x, 0, sizeof(cpu->x));
	cpu->pc = 0;
	cpu->mem = mem;
	cpu->mem_size = mem_size;
	cpu->hostcall_fn = NULL;
	cpu->hostcall_user_data = NULL;
	cpu->exit_code = 0;
}

void rv32_emu_set_hostcall_handler(struct rv32_cpu *cpu, rv32_hostcall_fn fn, void *user_data)
{
	cpu->hostcall_fn = fn;
	cpu->hostcall_user_data = user_data;
}

int rv32_emu_load_flat_binary(struct rv32_cpu *cpu, const uint8_t *flat_binary, uint32_t len)
{
	if (len > cpu->mem_size) {
		return -1;
	}
	memset(cpu->mem, 0, cpu->mem_size);
	memcpy(cpu->mem, flat_binary, len);

	memset(cpu->x, 0, sizeof(cpu->x));
	cpu->pc = 0;
	cpu->x[2] = cpu->mem_size & ~0xfu; /* sp = top of arena, 16-byte aligned per the calling convention */
	cpu->exit_code = 0;
	return 0;
}

/* -- Instruction field decode helpers -- all take the raw 32-bit word. -- */

static inline uint32_t f_opcode(uint32_t insn) { return insn & 0x7f; }
static inline uint32_t f_rd(uint32_t insn) { return (insn >> 7) & 0x1f; }
static inline uint32_t f_funct3(uint32_t insn) { return (insn >> 12) & 0x7; }
static inline uint32_t f_rs1(uint32_t insn) { return (insn >> 15) & 0x1f; }
static inline uint32_t f_rs2(uint32_t insn) { return (insn >> 20) & 0x1f; }
static inline uint32_t f_funct7(uint32_t insn) { return (insn >> 25) & 0x7f; }

static inline int32_t imm_i(uint32_t insn) { return (int32_t)insn >> 20; }

static inline int32_t imm_s(uint32_t insn)
{
	uint32_t u = ((insn >> 25) << 5) | ((insn >> 7) & 0x1f);

	return (int32_t)(u << 20) >> 20; /* sign-extend from bit 11 */
}

static inline int32_t imm_b(uint32_t insn)
{
	uint32_t u = (((insn >> 31) & 0x1) << 12) | (((insn >> 7) & 0x1) << 11) |
		     (((insn >> 25) & 0x3f) << 5) | (((insn >> 8) & 0xf) << 1);

	return (int32_t)(u << 19) >> 19; /* sign-extend from bit 12 */
}

static inline int32_t imm_u(uint32_t insn) { return (int32_t)(insn & 0xfffff000); }

static inline int32_t imm_j(uint32_t insn)
{
	uint32_t u = (((insn >> 31) & 0x1) << 20) | (((insn >> 12) & 0xff) << 12) |
		     (((insn >> 20) & 0x1) << 11) | (((insn >> 21) & 0x3ff) << 1);

	return (int32_t)(u << 11) >> 11; /* sign-extend from bit 20 */
}

enum rv32_result rv32_emu_step(struct rv32_cpu *cpu)
{
	if (cpu->pc % 4 != 0) {
		return RV32_ERR_MISALIGNED_PC;
	}

	uint32_t insn;

	if (!mem_read(cpu, cpu->pc, 4, &insn)) {
		return RV32_ERR_MEM_FAULT;
	}

	uint32_t opcode = f_opcode(insn);
	uint32_t rd = f_rd(insn);
	uint32_t funct3 = f_funct3(insn);
	uint32_t rs1 = f_rs1(insn);
	uint32_t rs2 = f_rs2(insn);
	uint32_t funct7 = f_funct7(insn);
	uint32_t next_pc = cpu->pc + 4;

	int32_t v1 = (int32_t)cpu->x[rs1];
	int32_t v2 = (int32_t)cpu->x[rs2];

	switch (opcode) {
	case 0x37: /* LUI */
		reg_set(cpu, rd, (uint32_t)imm_u(insn));
		break;

	case 0x17: /* AUIPC */
		reg_set(cpu, rd, cpu->pc + (uint32_t)imm_u(insn));
		break;

	case 0x6f: /* JAL */
		reg_set(cpu, rd, next_pc);
		next_pc = cpu->pc + (uint32_t)imm_j(insn);
		break;

	case 0x67: /* JALR */
		if (funct3 != 0) {
			return RV32_ERR_ILLEGAL_INSN;
		}
		{
			uint32_t target = ((uint32_t)v1 + (uint32_t)imm_i(insn)) & ~1u;

			reg_set(cpu, rd, next_pc);
			next_pc = target;
		}
		break;

	case 0x63: /* BRANCH */
	{
		int take;
		uint32_t u1 = cpu->x[rs1], u2 = cpu->x[rs2];

		switch (funct3) {
		case 0x0: take = (v1 == v2); break;         /* BEQ */
		case 0x1: take = (v1 != v2); break;          /* BNE */
		case 0x4: take = (v1 < v2); break;            /* BLT */
		case 0x5: take = (v1 >= v2); break;           /* BGE */
		case 0x6: take = (u1 < u2); break;            /* BLTU */
		case 0x7: take = (u1 >= u2); break;           /* BGEU */
		default: return RV32_ERR_ILLEGAL_INSN;
		}
		if (take) {
			next_pc = cpu->pc + (uint32_t)imm_b(insn);
		}
		break;
	}

	case 0x03: /* LOAD */
	{
		uint32_t addr = (uint32_t)v1 + (uint32_t)imm_i(insn);
		uint32_t raw;

		switch (funct3) {
		case 0x0: /* LB */
			if (!mem_read(cpu, addr, 1, &raw)) return RV32_ERR_MEM_FAULT;
			reg_set(cpu, rd, (uint32_t)(int32_t)(int8_t)raw);
			break;
		case 0x1: /* LH */
			if (!mem_read(cpu, addr, 2, &raw)) return RV32_ERR_MEM_FAULT;
			reg_set(cpu, rd, (uint32_t)(int32_t)(int16_t)raw);
			break;
		case 0x2: /* LW */
			if (!mem_read(cpu, addr, 4, &raw)) return RV32_ERR_MEM_FAULT;
			reg_set(cpu, rd, raw);
			break;
		case 0x4: /* LBU */
			if (!mem_read(cpu, addr, 1, &raw)) return RV32_ERR_MEM_FAULT;
			reg_set(cpu, rd, raw & 0xffu);
			break;
		case 0x5: /* LHU */
			if (!mem_read(cpu, addr, 2, &raw)) return RV32_ERR_MEM_FAULT;
			reg_set(cpu, rd, raw & 0xffffu);
			break;
		default:
			return RV32_ERR_ILLEGAL_INSN;
		}
		break;
	}

	case 0x23: /* STORE */
	{
		uint32_t addr = (uint32_t)v1 + (uint32_t)imm_s(insn);
		uint32_t val = cpu->x[rs2];

		switch (funct3) {
		case 0x0: /* SB */
			if (!mem_write(cpu, addr, 1, val)) return RV32_ERR_MEM_FAULT;
			break;
		case 0x1: /* SH */
			if (!mem_write(cpu, addr, 2, val)) return RV32_ERR_MEM_FAULT;
			break;
		case 0x2: /* SW */
			if (!mem_write(cpu, addr, 4, val)) return RV32_ERR_MEM_FAULT;
			break;
		default:
			return RV32_ERR_ILLEGAL_INSN;
		}
		break;
	}

	case 0x13: /* OP-IMM */
	{
		int32_t imm = imm_i(insn);
		uint32_t shamt = (uint32_t)imm & 0x1f;

		switch (funct3) {
		case 0x0: reg_set(cpu, rd, (uint32_t)(v1 + imm)); break;                 /* ADDI */
		case 0x2: reg_set(cpu, rd, (v1 < imm) ? 1 : 0); break;                    /* SLTI */
		case 0x3: reg_set(cpu, rd, ((uint32_t)v1 < (uint32_t)imm) ? 1 : 0); break; /* SLTIU */
		case 0x4: reg_set(cpu, rd, (uint32_t)(v1 ^ imm)); break;                  /* XORI */
		case 0x6: reg_set(cpu, rd, (uint32_t)(v1 | imm)); break;                  /* ORI */
		case 0x7: reg_set(cpu, rd, (uint32_t)(v1 & imm)); break;                  /* ANDI */
		case 0x1: /* SLLI */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (uint32_t)v1 << shamt);
			break;
		case 0x5: /* SRLI / SRAI */
			if (funct7 == 0x00) {
				reg_set(cpu, rd, (uint32_t)v1 >> shamt);
			} else if (funct7 == 0x20) {
				reg_set(cpu, rd, (uint32_t)(v1 >> shamt));
			} else {
				return RV32_ERR_ILLEGAL_INSN;
			}
			break;
		default:
			return RV32_ERR_ILLEGAL_INSN;
		}
		break;
	}

	case 0x33: /* OP (register-register): base ALU (funct7 0x00/0x20) or M extension (funct7 0x01) */
	{
		if (funct7 == 0x01) { /* RV32M */
			int64_t sv1 = v1, sv2 = v2;
			uint64_t uv1 = cpu->x[rs1], uv2 = cpu->x[rs2];

			switch (funct3) {
			case 0x0: reg_set(cpu, rd, (uint32_t)(v1 * v2)); break; /* MUL */
			case 0x1: reg_set(cpu, rd, (uint32_t)((sv1 * sv2) >> 32)); break; /* MULH */
			case 0x2: reg_set(cpu, rd, (uint32_t)((sv1 * (int64_t)uv2) >> 32)); break; /* MULHSU */
			case 0x3: reg_set(cpu, rd, (uint32_t)((uv1 * uv2) >> 32)); break; /* MULHU */
			case 0x4: /* DIV */
				if (v2 == 0) {
					reg_set(cpu, rd, 0xffffffffu);
				} else if (v1 == INT32_MIN && v2 == -1) {
					reg_set(cpu, rd, (uint32_t)INT32_MIN);
				} else {
					reg_set(cpu, rd, (uint32_t)(v1 / v2));
				}
				break;
			case 0x5: /* DIVU */
				reg_set(cpu, rd, (cpu->x[rs2] == 0) ? 0xffffffffu : cpu->x[rs1] / cpu->x[rs2]);
				break;
			case 0x6: /* REM */
				if (v2 == 0) {
					reg_set(cpu, rd, (uint32_t)v1);
				} else if (v1 == INT32_MIN && v2 == -1) {
					reg_set(cpu, rd, 0);
				} else {
					reg_set(cpu, rd, (uint32_t)(v1 % v2));
				}
				break;
			case 0x7: /* REMU */
				reg_set(cpu, rd, (cpu->x[rs2] == 0) ? cpu->x[rs1] : cpu->x[rs1] % cpu->x[rs2]);
				break;
			default:
				return RV32_ERR_ILLEGAL_INSN;
			}
			break;
		}

		uint32_t shamt = cpu->x[rs2] & 0x1f;

		switch (funct3) {
		case 0x0: /* ADD / SUB */
			if (funct7 == 0x00) reg_set(cpu, rd, (uint32_t)(v1 + v2));
			else if (funct7 == 0x20) reg_set(cpu, rd, (uint32_t)(v1 - v2));
			else return RV32_ERR_ILLEGAL_INSN;
			break;
		case 0x1: /* SLL */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (uint32_t)v1 << shamt);
			break;
		case 0x2: /* SLT */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (v1 < v2) ? 1 : 0);
			break;
		case 0x3: /* SLTU */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (cpu->x[rs1] < cpu->x[rs2]) ? 1 : 0);
			break;
		case 0x4: /* XOR */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (uint32_t)(v1 ^ v2));
			break;
		case 0x5: /* SRL / SRA */
			if (funct7 == 0x00) reg_set(cpu, rd, (uint32_t)v1 >> shamt);
			else if (funct7 == 0x20) reg_set(cpu, rd, (uint32_t)(v1 >> shamt));
			else return RV32_ERR_ILLEGAL_INSN;
			break;
		case 0x6: /* OR */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (uint32_t)(v1 | v2));
			break;
		case 0x7: /* AND */
			if (funct7 != 0x00) return RV32_ERR_ILLEGAL_INSN;
			reg_set(cpu, rd, (uint32_t)(v1 & v2));
			break;
		default:
			return RV32_ERR_ILLEGAL_INSN;
		}
		break;
	}

	case 0x0f: /* MISC-MEM (FENCE) -- no cache/ordering model here, accept as a no-op */
		break;

	case 0x73: /* SYSTEM: only the canonical ECALL/EBREAK encodings are accepted */
		if (insn == 0x00000073) { /* ECALL */
			int32_t id = (int32_t)cpu->x[17]; /* a7 */

			if (id == HOSTCALL_EXIT) {
				cpu->exit_code = (int32_t)cpu->x[10]; /* a0 */
				cpu->pc = next_pc;
				return RV32_HALTED_EXIT;
			}
			if (cpu->hostcall_fn == NULL) {
				return RV32_ERR_NO_HOSTCALL_HANDLER;
			}
			int32_t ret = cpu->hostcall_fn(cpu, cpu->hostcall_user_data, id,
							(int32_t)cpu->x[10], (int32_t)cpu->x[11],
							(int32_t)cpu->x[12], (int32_t)cpu->x[13],
							(int32_t)cpu->x[14], (int32_t)cpu->x[15]);
			reg_set(cpu, 10, (uint32_t)ret);
		} else if (insn == 0x00100073) { /* EBREAK -- no debugger attached, treat as illegal */
			return RV32_ERR_ILLEGAL_INSN;
		} else {
			return RV32_ERR_ILLEGAL_INSN;
		}
		break;

	default:
		return RV32_ERR_ILLEGAL_INSN;
	}

	cpu->pc = next_pc;
	return RV32_RUNNING;
}

enum rv32_result rv32_emu_run(struct rv32_cpu *cpu, uint32_t max_instructions, uint32_t *out_steps)
{
	uint32_t i;
	enum rv32_result r = RV32_RUNNING;

	for (i = 0; i < max_instructions; i++) {
		r = rv32_emu_step(cpu);
		if (r != RV32_RUNNING) {
			if (out_steps) *out_steps = i + 1;
			return r;
		}
	}
	if (out_steps) *out_steps = i;
	return RV32_ERR_INSN_BUDGET_EXCEEDED;
}
