| Sega Genesis Boot Code
| Correct ROM header format for emulator compatibility

    .text
    .globl _start
    .globl __start

| Vector table (0x000-0x0FF) - exactly 64 longwords = 256 bytes
_start:
__start:
    .long   0x00FFE000          | 00: Initial stack pointer
    .long   _entry              | 04: Entry point (PC)
    .long   _exception          | 08: Bus error
    .long   _exception          | 0C: Address error
    .long   _exception          | 10: Illegal instruction
    .long   _exception          | 14: Zero divide
    .long   _exception          | 18: CHK instruction
    .long   _exception          | 1C: TRAPV instruction
    .long   _exception          | 20: Privilege violation
    .long   _exception          | 24: Trace
    .long   _exception          | 28: Line 1010 emulator
    .long   _exception          | 2C: Line 1111 emulator
    .long   _exception          | 30: Reserved
    .long   _exception          | 34: Reserved
    .long   _exception          | 38: Reserved
    .long   _exception          | 3C: Uninitialized interrupt
    .long   _exception          | 40: Reserved
    .long   _exception          | 44: Reserved
    .long   _exception          | 48: Reserved
    .long   _exception          | 4C: Reserved
    .long   _exception          | 50: Reserved
    .long   _exception          | 54: Reserved
    .long   _exception          | 58: Reserved
    .long   _exception          | 5C: Reserved
    .long   _exception          | 60: Spurious interrupt
    .long   _exception          | 64: IRQ level 1
    .long   _ext_int            | 68: IRQ level 2 (external)
    .long   _exception          | 6C: IRQ level 3
    .long   _hblank             | 70: IRQ level 4 (H-blank)
    .long   _exception          | 74: IRQ level 5
    .long   _vblank             | 78: IRQ level 6 (V-blank)
    .long   _exception          | 7C: IRQ level 7
    .long   _exception          | 80: TRAP #0
    .long   _exception          | 84: TRAP #1
    .long   _exception          | 88: TRAP #2
    .long   _exception          | 8C: TRAP #3
    .long   _exception          | 90: TRAP #4
    .long   _exception          | 94: TRAP #5
    .long   _exception          | 98: TRAP #6
    .long   _exception          | 9C: TRAP #7
    .long   _exception          | A0: TRAP #8
    .long   _exception          | A4: TRAP #9
    .long   _exception          | A8: TRAP #10
    .long   _exception          | AC: TRAP #11
    .long   _exception          | B0: TRAP #12
    .long   _exception          | B4: TRAP #13
    .long   _exception          | B8: TRAP #14
    .long   _exception          | BC: TRAP #15
    .long   _exception          | C0: Reserved
    .long   _exception          | C4: Reserved
    .long   _exception          | C8: Reserved
    .long   _exception          | CC: Reserved
    .long   _exception          | D0: Reserved
    .long   _exception          | D4: Reserved
    .long   _exception          | D8: Reserved
    .long   _exception          | DC: Reserved
    .long   _exception          | E0: Reserved
    .long   _exception          | E4: Reserved
    .long   _exception          | E8: Reserved
    .long   _exception          | EC: Reserved
    .long   _exception          | F0: Reserved
    .long   _exception          | F4: Reserved
    .long   _exception          | F8: Reserved
    .long   _exception          | FC: Reserved

| ROM Header (0x100-0x1FF) - exactly 256 bytes
    .ascii  "SEGA MEGA DRIVE "                  | 100: Console name (16 bytes)
    .ascii  "(C)2025         "                  | 110: Copyright (16 bytes)
    .ascii  "SCRABBLE                                        "  | 120: Domestic name (48 bytes)
    .ascii  "SCRABBLE                                        "  | 150: Overseas name (48 bytes)
    .ascii  "GM 00000000-00"                    | 180: Serial/version (14 bytes)
    .word   0x0000                              | 18E: Checksum (placeholder)
    .ascii  "J               "                  | 190: I/O support (16 bytes)
    .long   0x00000000                          | 1A0: ROM start address
    .long   0x003FFFFF                          | 1A4: ROM end address
    .long   0x00FF0000                          | 1A8: RAM start address
    .long   0x00FFFFFF                          | 1AC: RAM end address
    .ascii  "            "                      | 1B0: SRAM info (12 bytes)
    .ascii  "            "                      | 1BC: Modem info (12 bytes)
    .ascii  "                                        "  | 1C8: Memo (40 bytes)
    .ascii  "JUE             "                  | 1F0: Country codes (16 bytes)

| Entry point (address 0x200)
_entry:
    | Disable interrupts
    move.w  #0x2700, %sr

    | Initialize TMSS (Trademark Security System)
    move.b  0xA10001, %d0
    andi.b  #0x0F, %d0
    beq.s   skip_tmss
    move.l  #0x53454741, 0xA14000    | Write "SEGA" to TMSS register
skip_tmss:

    | Clear work RAM (0xFF0000-0xFFFFFF)
    lea     0xFF0000, %a0
    move.w  #0x3FFF, %d0
clear_ram:
    clr.l   (%a0)+
    dbra    %d0, clear_ram

    | Initialize Z80
    move.w  #0x0100, 0xA11100       | Request Z80 bus
    move.w  #0x0100, 0xA11200       | Hold Z80 reset
wait_z80:
    btst    #0, 0xA11100
    bne.s   wait_z80

    | Clear Z80 RAM
    lea     0xA00000, %a0
    move.w  #0x1FFF, %d0
clear_z80:
    clr.b   (%a0)+
    dbra    %d0, clear_z80

    | Release Z80
    move.w  #0x0000, 0xA11200       | Release Z80 reset
    move.w  #0x0000, 0xA11100       | Release Z80 bus

    | Initialize VDP
    lea     vdp_init_data, %a0
    lea     0xC00004, %a1           | VDP control port
    moveq   #18, %d0                | 19 registers to set
init_vdp_loop:
    move.w  (%a0)+, (%a1)
    dbra    %d0, init_vdp_loop

    | Clear VRAM
    move.l  #0x40000000, 0xC00004   | VRAM write to 0x0000
    move.w  #0x7FFF, %d0
    lea     0xC00000, %a0
clear_vram:
    clr.w   (%a0)
    dbra    %d0, clear_vram

    | Clear CRAM (palette)
    move.l  #0xC0000000, 0xC00004   | CRAM write to 0x0000
    moveq   #31, %d0
clear_cram:
    clr.w   (%a0)
    dbra    %d0, clear_cram

    | Clear VSRAM
    move.l  #0x40000010, 0xC00004   | VSRAM write to 0x0000
    moveq   #19, %d0
clear_vsram:
    clr.w   (%a0)
    dbra    %d0, clear_vsram

    | Set background to blue as debug indicator
    move.l  #0xC0000000, 0xC00004   | CRAM write address 0
    move.w  #0x0E00, 0xC00000       | Blue color

    | Enable display and VBlank interrupt
    move.w  #0x8174, 0xC00004

    | Enable interrupts
    move.w  #0x2000, %sr

    | Jump to main C code
    jmp     main

| VDP initialization data
vdp_init_data:
    .word   0x8004          | Reg 0: H-Int off, HV counter on
    .word   0x8114          | Reg 1: Display off, VBlank on, DMA off, V28 mode
    .word   0x8230          | Reg 2: Plane A at 0xC000
    .word   0x832C          | Reg 3: Window at 0xB000
    .word   0x8407          | Reg 4: Plane B at 0xE000
    .word   0x856C          | Reg 5: Sprite table at 0xD800
    .word   0x8600          | Reg 6: unused
    .word   0x8700          | Reg 7: Background color palette 0, index 0
    .word   0x8800          | Reg 8: unused
    .word   0x8900          | Reg 9: unused
    .word   0x8AFF          | Reg 10: H-Int every 256 lines (off)
    .word   0x8B00          | Reg 11: Full screen scroll, no ext int
    .word   0x8C81          | Reg 12: H40 mode (320 pixels), no interlace
    .word   0x8D3F          | Reg 13: H-scroll table at 0xFC00
    .word   0x8E00          | Reg 14: unused
    .word   0x8F02          | Reg 15: Auto-increment 2
    .word   0x9001          | Reg 16: Scroll size 64x32
    .word   0x9100          | Reg 17: Window H position
    .word   0x9200          | Reg 18: Window V position

| Exception/interrupt handlers
_exception:
    rte

_ext_int:
    rte

_hblank:
    rte

_vblank:
    addq.l  #1, frame_counter
    rte

    .data
    .globl  frame_counter
frame_counter:
    .long   0

    .end
