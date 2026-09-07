/*
 * Deliberately hanging guest: an infinite loop. Exists to prove
 * rv32_emu_run()'s instruction budget actually trips
 * (RV32_ERR_INSN_BUDGET_EXCEEDED) instead of hanging the host caller --
 * the other half of this emulator's isolation guarantee, alongside
 * test_oob.c's memory-bounds check.
 */
int main(void)
{
	volatile int x = 0;

	while (1) {
		x++;
	}
	return x;
}
