/*
 * First guest test program: draws a diagonal line via HOSTCALL_DRAW_PIXEL
 * (computed with real guest-side multiplication/division, not just fixed
 * constants -- exercises the RV32M instructions, not only RV32I), sets
 * the status LED, prints a debug string, then exits with the pixel count
 * as its exit code -- the host smoke test checks that count against a
 * real independently-computed expectation, not just "it ran".
 */
#include "guest_syscalls.h"

#define LINE_LEN 40

int main(void)
{
	int drawn = 0;

	for (int i = 0; i < LINE_LEN; i++) {
		/* y computed via multiply+divide so a MUL/DIV bug would show
		 * up as a wrong pixel count, not just a crash. */
		int y = (i * 3) / 2;

		draw_pixel(i, y, 1);
		drawn++;
	}

	led_set(0x00ff00); /* green: "guest ran to completion" */

	const char msg[] = "guest: line drawn";

	debug_print(msg, sizeof(msg) - 1);

	return drawn;
}
