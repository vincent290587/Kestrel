/*
 * Deliberately faulting guest: dereferences an address far outside the
 * 64KB arena. Exists to prove the host's bounds checking actually traps
 * this (RV32_ERR_MEM_FAULT) instead of corrupting host memory -- the
 * isolation property this whole emulator exists for, not just an
 * afterthought to test once things work.
 */
int main(void)
{
	volatile int *p = (int *)0x7ffffff0;

	return *p;
}
