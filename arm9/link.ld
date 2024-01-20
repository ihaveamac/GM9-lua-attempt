OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    AHBWRAM (RX) : ORIGIN = 0x08000000, LENGTH = 512K
    AHBWRAM2 (RWX) : ORIGIN = 0x080a0000, LENGTH = 384K
}

SECTIONS
{
    /* this has to come *first* so it picks up lua and libm */
    AHBWRAM2 : ALIGN(4) {
        build/lua*(.text*);
        libm*(.text*);

        . = ALIGN(4);
        *(.rodata*);

        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;

        . = ALIGN(4);
        *(.data*);

        . = ALIGN(4);
        __bss_start = .;
        *(.bss*);
        __bss_end = .;
        __end__ = .;
    } >AHBWRAM2


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
    } >AHBWRAM
}
