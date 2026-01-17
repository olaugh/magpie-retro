| Minimal Genesis Test ROM
| Just sets background to blue and loops

    .text
    .globl _start

| Vector table
_start:
    .long   0x00FFFE00          | Stack pointer
    .long   start               | Entry point
    .rept 62
    .long   handler             | All exceptions go to handler
    .endr

| ROM Header at 0x100
    .ascii  "SEGA MEGA DRIVE "
    .ascii  "(C)TEST         "
    .ascii  "MINIMAL TEST                                    "
    .ascii  "MINIMAL TEST                                    "
    .ascii  "GM 00000000-00"
    .word   0x0000
    .ascii  "J               "
    .long   0x00000000
    .long   0x0000FFFF
    .long   0x00FF0000
    .long   0x00FFFFFF
    .ascii  "            "
    .ascii  "            "
    .ascii  "                                        "
    .ascii  "JUE             "

| Entry point at 0x200
start:
    | Disable interrupts
    move.w  #0x2700, %sr

    | TMSS
    move.b  0xA10001, %d0
    andi.b  #0x0F, %d0
    beq.s   no_tmss
    move.l  #0x53454741, 0xA14000
no_tmss:

    | Set VDP registers
    lea     0xC00004, %a0
    move.w  #0x8004, (%a0)      | Mode 1
    move.w  #0x8144, (%a0)      | Mode 2: Display ON
    move.w  #0x8230, (%a0)      | Plane A
    move.w  #0x8407, (%a0)      | Plane B
    move.w  #0x8700, (%a0)      | BG color = 0
    move.w  #0x8C81, (%a0)      | H40 mode
    move.w  #0x8F02, (%a0)      | Auto-inc

    | Set palette color 0 to BLUE
    move.l  #0xC0000000, (%a0)  | CRAM address 0
    lea     0xC00000, %a1
    move.w  #0x0E00, (%a1)      | Blue (BGR format)

    | Infinite loop
loop:
    bra.s   loop

handler:
    rte

    .end
