/*
 * Tiny freestanding runtime: with -nostdlib there's no libc, but gcc's
 * own codegen (e.g. initializing a local array from a string literal)
 * can still emit calls to memcpy/memset regardless of -fno-builtin --
 * that flag only affects whether gcc treats calls *written in the
 * source* as builtins, not whether its own codegen decides to emit one.
 * These plain byte-at-a-time implementations are enough for guest
 * programs this small; not meant to be fast.
 */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--) {
		*d++ = *s++;
	}
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;

	while (n--) {
		*d++ = (unsigned char)c;
	}
	return dst;
}
