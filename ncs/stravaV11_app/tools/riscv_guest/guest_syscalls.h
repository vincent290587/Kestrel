/*
 * Guest-side wrappers around the host calls defined in
 * ../../lib/source/riscv/rv32_hostcalls.h (same numeric ids -- kept as a
 * separate copy rather than a shared #include since the guest and host
 * are built by two entirely different toolchains/build systems; if you
 * add a hostcall, update both files).
 */
#ifndef GUEST_SYSCALLS_H_
#define GUEST_SYSCALLS_H_

#define HOSTCALL_EXIT 0
#define HOSTCALL_DRAW_PIXEL 1
#define HOSTCALL_LED_SET 2
#define HOSTCALL_DEBUG_PRINT 3

static inline int hostcall(int id, int a0, int a1, int a2, int a3)
{
	register int r_a7 asm("a7") = id;
	register int r_a0 asm("a0") = a0;
	register int r_a1 asm("a1") = a1;
	register int r_a2 asm("a2") = a2;
	register int r_a3 asm("a3") = a3;

	asm volatile("ecall"
		     : "+r"(r_a0)
		     : "r"(r_a7), "r"(r_a1), "r"(r_a2), "r"(r_a3)
		     : "memory");
	return r_a0;
}

static inline void draw_pixel(int x, int y, int color)
{
	hostcall(HOSTCALL_DRAW_PIXEL, x, y, color, 0);
}

static inline void led_set(int rgb)
{
	hostcall(HOSTCALL_LED_SET, rgb, 0, 0, 0);
}

static inline void debug_print(const char *s, int len)
{
	hostcall(HOSTCALL_DEBUG_PRINT, (int)(long)s, len, 0, 0);
}

#endif /* GUEST_SYSCALLS_H_ */
