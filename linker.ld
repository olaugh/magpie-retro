/* Sega Genesis Linker Script */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY
{
    /* ROM: 0x000000 - 0xFFFFFF (up to 16MB) */
    rom (rx)  : ORIGIN = 0x000000, LENGTH = 16M

    /* Work RAM: 0xFF0000 - 0xFFFFFF (64KB) */
    ram (rwx) : ORIGIN = 0xFF0000, LENGTH = 64K
}

SECTIONS
{
    /* ROM Header and boot code (must be at 0x000000) */
    .text 0x000000 :
    {
        *boot.o(.text)
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > rom

    /* KWG lexicon data (large, goes after code) */
    .kwg_data :
    {
        _kwg_start = .;
        *kwg_data.o(.data*)
        *kwg_data.o(.rodata*)
        _kwg_end = .;
        . = ALIGN(4);
    } > rom

    /* Initialized data (copied to RAM at startup) */
    .data :
    {
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > ram AT > rom

    _data_load = LOADADDR(.data);

    /* Uninitialized data (zeroed at startup) */
    .bss (NOLOAD) :
    {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > ram

    /* Stack at top of RAM */
    _stack_top = ORIGIN(ram) + LENGTH(ram);

    /* Discard debug sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.debug*)
    }
}
