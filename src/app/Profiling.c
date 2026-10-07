/**
 * @file    Profiling.c
 * @brief   DWT CYCCNT-based profiling implementation for S32K144.
 *
 * Provides the result storage array and the three public functions declared
 * in Profiling.h.  The hot-path start/stop pair are macros in the header
 * and therefore require no code here.
 */

#include "app/Profiling.h"

/* -----------------------------------------------------------------------
 * Result storage -- one slot per measurement point
 * ----------------------------------------------------------------------- */
Profiling_SlotType Profiling_Results[PROFILING_MAX_SLOTS];

/* -----------------------------------------------------------------------
 * Profiling_Init
 * ----------------------------------------------------------------------- */
void Profiling_Init(void)
{
    uint8 i;

    /* 1. Enable TRCENA bit in DEMCR so that DWT can run without a debugger */
    PROFILING_DEMCR |= PROFILING_DEMCR_TRCENA_Msk;

    /* 2. Reset cycle counter to a known zero */
    PROFILING_DWT_CYCCNT = 0U;

    /* 3. Enable the cycle counter */
    PROFILING_DWT_CTRL |= PROFILING_DWT_CTRL_CYCCNTENA_Msk;

    /* 4. Clear all result slots */
    for (i = 0U; i < PROFILING_MAX_SLOTS; i++) {
        Profiling_Results[i].startCycles   = 0U;
        Profiling_Results[i].elapsedCycles = 0U;
        Profiling_Results[i].active        = 0U;
    }
}

/* -----------------------------------------------------------------------
 * Profiling_GetUs
 * ----------------------------------------------------------------------- */
uint32 Profiling_GetUs(uint8 slot)
{
    if (slot >= PROFILING_MAX_SLOTS) {
        return 0U;
    }
    /* cycles / (MHz) == microseconds  (integer division, no float) */
    return Profiling_Results[slot].elapsedCycles / (CORE_CLOCK_HZ / 1000000UL);
}

/* -----------------------------------------------------------------------
 * Profiling_GetCycles
 * ----------------------------------------------------------------------- */
uint32 Profiling_GetCycles(uint8 slot)
{
    if (slot >= PROFILING_MAX_SLOTS) {
        return 0U;
    }
    return Profiling_Results[slot].elapsedCycles;
}
