OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    AHBWRAM (RWX) : ORIGIN = 0x08000000, LENGTH = 512K
    AHBWRAM2 (RWX) : ORIGIN = 0x080a0000, LENGTH = 128K
}

SECTIONS
{
    AHBWRAM : ALIGN(4) {
		/* .vectors */
        __vectors_lma = LOADADDR(AHBWRAM);
        __vectors_vma = ABSOLUTE(.);
        KEEP(*(.vectors));
        . = ALIGN(4);
        __vectors_len = ABSOLUTE(.) - __vectors_vma;
        . = . + 0x10;

		/* .text */
        . = ALIGN(4);
        __text_s = ABSOLUTE(.);
        *(.text.start);
        *(.text*);
        __text_e = ABSOLUTE(.);

		/* .data (should be moved to WRAM2) */
        . = ALIGN(4);
        *(.data*);

		/* .bss (should be moved to WRAM2) */
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*);
        __bss_end = .;
    } >AHBWRAM

    AHBWRAM2 : ALIGN(4) {
		/* .rodata (should be moved to WRAM) */
        . = ALIGN(4);
        *(.rodata*);

		/* .ARM.exidx (should be moved to WRAM) */
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } >AHBWRAM2

    __end__ = ABSOLUTE(.);
}
