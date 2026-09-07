/*
 * Places .text.start (start.S's entry stub) at address 0 of the linked
 * image, followed by the rest of .text/.rodata/.data. There is no ELF
 * loader on the host side (see rv32_emu.h) -- build.sh's objcopy dumps
 * this image straight to a flat binary, and the host memcpy's it to
 * offset 0 of the emulated arena with pc initialized to 0, so "address 0
 * in this linker script" and "guest address 0 at runtime" are the same
 * thing by construction. .bss is NOLOAD (not present in the flat binary
 * at all) -- the host zeroes the whole arena before loading, so it comes
 * up zeroed for free.
 */
ENTRY(_start)

MEMORY
{
	ram (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

SECTIONS
{
	. = 0x00000000;

	.text : {
		*(.text.start)
		*(.text*)
	} > ram

	.rodata : {
		*(.rodata*)
	} > ram

	.data : {
		*(.data*)
	} > ram

	.bss (NOLOAD) : {
		*(.bss*)
		*(COMMON)
	} > ram

	/DISCARD/ : {
		*(.comment)
		*(.riscv.attributes)
		*(.eh_frame*)
	}
}
