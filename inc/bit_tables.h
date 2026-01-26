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
extern const uint32_t BIT_MASK[32];

/*
 * BITS_BELOW_MASK: Mask with all bits below n set.
 * BITS_BELOW_MASK[n] = (1U << n) - 1 = bits 0..(n-1) set
 * Valid for n in [0, 32].
 * BITS_BELOW_MASK[0] = 0 (no bits set)
 * BITS_BELOW_MASK[7] = 0x7F (bits 0-6 set)
 * BITS_BELOW_MASK[32] = 0xFFFFFFFF (all bits set)
 */
extern const uint32_t BITS_BELOW_MASK[33];

#endif /* BIT_TABLES_H */
