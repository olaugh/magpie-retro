/*
 * Bit manipulation lookup tables for 68000 optimization
 *
 * On 68000, variable shifts like (1U << n) cost 2 cycles per bit shifted,
 * up to 52 cycles for n=26. Table lookups cost only ~8 cycles.
 *
 * These tables are used throughout the codebase for:
 * - Cross-set computation (kwg.c, kwg.h)
 * - Leave map indexing (klv.c)
 * - Rack bitmask operations (movegen.c)
 */

#ifndef BIT_TABLES_H
#define BIT_TABLES_H

#include <stdint.h>

/*
 * BIT_MASK: Single-bit mask lookup table.
 * BIT_MASK[n] = (1U << n)
 * Valid for n in [0, 31].
 */
static const uint32_t BIT_MASK[32] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000
};

/*
 * BITS_BELOW_MASK: Mask with all bits below n set.
 * BITS_BELOW_MASK[n] = (1U << n) - 1 = bits 0..(n-1) set
 * Valid for n in [0, 32].
 * BITS_BELOW_MASK[0] = 0 (no bits set)
 * BITS_BELOW_MASK[7] = 0x7F (bits 0-6 set)
 * BITS_BELOW_MASK[32] = 0xFFFFFFFF (all bits set)
 */
static const uint32_t BITS_BELOW_MASK[33] = {
    0x00000000,  /* n=0: no bits */
    0x00000001, 0x00000003, 0x00000007, 0x0000000F,
    0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
    0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
    0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
    0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
    0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
    0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
    0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
};

#endif /* BIT_TABLES_H */
