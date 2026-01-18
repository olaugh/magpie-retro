/*
 * Equity definitions for Sega Genesis
 *
 * Adapted from original magpie equity_defs.h
 * Uses 16-bit signed integers with resolution of 8 (eighths of a point)
 * This gives a range of roughly -4095 to +4095 points with 0.125 precision.
 */

#ifndef EQUITY_H
#define EQUITY_H

#include <stdint.h>

/* Equity type: 16-bit signed, in eighths of a point */
typedef int16_t Equity;

/* Resolution: 8 means equity is stored in eighths of a point */
#define EQUITY_RESOLUTION 8

/*
 * Special values (matching original magpie pattern):
 * - UNDEFINED: sentinel for uninitialized equity
 * - INITIAL: sentinel for "no move found yet" (used in movegen)
 * - PASS: sentinel for pass move equity
 * - MIN/MAX: actual usable range boundaries
 */
enum {
    EQUITY_UNDEFINED_VALUE = INT16_MIN,      /* -32768 */
    EQUITY_INITIAL_VALUE   = INT16_MIN + 1,  /* -32767 */
    EQUITY_PASS_VALUE      = INT16_MIN + 2,  /* -32766 */
    /* Three reserved values at bottom, one at top for symmetry */
    EQUITY_MIN_VALUE       = INT16_MIN + 3,  /* -32765 */
    EQUITY_MAX_VALUE       = -EQUITY_MIN_VALUE, /* 32765 */
};

/* Convert raw points to equity (multiply by resolution) */
#define POINTS_TO_EQUITY(pts) ((Equity)((pts) * EQUITY_RESOLUTION))

/* Convert equity to raw points (divide by resolution, truncates) */
#define EQUITY_TO_POINTS(eq) ((eq) / EQUITY_RESOLUTION)

/* Min/max in actual point values */
#define EQUITY_MIN_POINTS (EQUITY_MIN_VALUE / EQUITY_RESOLUTION)
#define EQUITY_MAX_POINTS (EQUITY_MAX_VALUE / EQUITY_RESOLUTION)

#endif /* EQUITY_H */
