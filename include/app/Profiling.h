/**
 * @file    Profiling.h
 * @brief   Lightweight execution-time profiler using ARM Cortex-M4 DWT CYCCNT.
 *
 * Usage:
 *   #include "app/Profiling.h"
 *
 *   Profiling_Init();          // Call once at startup (before first measurement)
 *
 *   profiling_start(MY_SLOT);  // Start timing slot MY_SLOT
 *   ... code to measure ...
 *   profiling_stop(MY_SLOT);   // Stop timing, stores result in Profiling_Results[]
 *
 *   uint32 elapsed_us = Profiling_GetUs(MY_SLOT);    // Read last elapsed time in us
 *   uint32 elapsed_cy = Profiling_GetCycles(MY_SLOT); // ... or in raw CPU cycles
 *
 * Slots are zero-indexed. Define PROFILING_MAX_SLOTS before including this
 * header to override the default (8).
 *
 * Clock assumption: S32K144 System Clock = 80 MHz (CORE_CLOCK_HZ).
 * Override CORE_CLOCK_HZ via Makefile / project settings if needed.
 *
 * Thread / ISR safety: NOT re-entrant on the same slot. Each slot is
 * independent (safe to use different slots from different task contexts).
 *
 * @note    DWT must be unlocked by the debug interface OR by calling
 *          Profiling_Init() which performs the unlock sequence in software.
 */

#ifndef PROFILING_H
#define PROFILING_H

#include "Std_Types.h"

/* -----------------------------------------------------------------------
 * Configuration
 * ----------------------------------------------------------------------- */

/** Number of independent profiling slots available. */
#ifndef PROFILING_MAX_SLOTS
#define PROFILING_MAX_SLOTS   8U
#endif

/** CPU frequency in Hz -- must match your PLL / SPLL setting. */
#ifndef CORE_CLOCK_HZ
#define CORE_CLOCK_HZ         80000000UL   /* 80 MHz S32K144 default */
#endif

/* -----------------------------------------------------------------------
 * DWT register addresses (ARM Cortex-M4, no CMSIS dependency to keep
 * the project CMSIS-free, matching existing project style).
 * ----------------------------------------------------------------------- */
#define PROFILING_DWT_BASE        (0xE0001000UL)
#define PROFILING_DWT_CTRL        (*((volatile uint32 *)(PROFILING_DWT_BASE + 0x000U)))
#define PROFILING_DWT_CYCCNT      (*((volatile uint32 *)(PROFILING_DWT_BASE + 0x004U)))

#define PROFILING_CoreDebug_BASE  (0xE000EDF0UL)
#define PROFILING_DEMCR           (*((volatile uint32 *)(PROFILING_CoreDebug_BASE + 0x00CU)))

#define PROFILING_DEMCR_TRCENA_Msk       (1UL << 24U)  /* Enable DWT/ITM trace */
#define PROFILING_DWT_CTRL_CYCCNTENA_Msk (1UL)         /* Enable cycle counter  */

/* -----------------------------------------------------------------------
 * Result storage (defined in Profiling.c)
 * ----------------------------------------------------------------------- */
typedef struct {
    uint32 startCycles;    /**< Raw DWT snapshot at profiling_start()  */
    uint32 elapsedCycles;  /**< Cycles elapsed at last profiling_stop() */
    uint8  active;         /**< 1 = timer is running                    */
} Profiling_SlotType;

extern Profiling_SlotType Profiling_Results[PROFILING_MAX_SLOTS];

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/**
 * @brief Initialise DWT cycle counter.  Call ONCE before any measurement.
 *        Safe to call multiple times (idempotent).
 */
void Profiling_Init(void);

/**
 * @brief Return last measured elapsed time for slot in microseconds.
 * @param slot  Slot index [0 .. PROFILING_MAX_SLOTS-1]
 * @return      Elapsed microseconds (0 if slot never stopped)
 */
uint32 Profiling_GetUs(uint8 slot);

/**
 * @brief Return last measured elapsed time for slot in raw CPU cycles.
 * @param slot  Slot index [0 .. PROFILING_MAX_SLOTS-1]
 * @return      Elapsed cycles (0 if slot never stopped)
 */
uint32 Profiling_GetCycles(uint8 slot);

/* -----------------------------------------------------------------------
 * Inline start / stop pair -- kept as macros for guaranteed zero-overhead
 * ----------------------------------------------------------------------- */

/**
 * @brief  Record the current DWT cycle count for the given slot.
 *         Slot must be in [0 .. PROFILING_MAX_SLOTS-1].
 */
#define profiling_start(slot)                                               \
    do {                                                                    \
        if ((slot) < PROFILING_MAX_SLOTS) {                                 \
            Profiling_Results[(slot)].startCycles = PROFILING_DWT_CYCCNT;  \
            Profiling_Results[(slot)].active = 1U;                          \
        }                                                                   \
    } while (0)

/**
 * @brief  Compute elapsed cycles since the matching profiling_start().
 *         Handles 32-bit wrap-around (correct as long as elapsed < ~53 s
 *         at 80 MHz before the counter wraps a second time).
 */
#define profiling_stop(slot)                                                \
    do {                                                                    \
        if (((slot) < PROFILING_MAX_SLOTS) &&                              \
             (Profiling_Results[(slot)].active != 0U)) {                    \
            uint32 _now = PROFILING_DWT_CYCCNT;                             \
            Profiling_Results[(slot)].elapsedCycles =                       \
                _now - Profiling_Results[(slot)].startCycles;               \
            Profiling_Results[(slot)].active = 0U;                          \
        }                                                                   \
    } while (0)

/* -----------------------------------------------------------------------
 * Convenience slot name aliases (add more as needed)
 * ----------------------------------------------------------------------- */
#define PROF_SLOT_APP_1MS       0U  /**< App_Task_1ms() execution time    */
#define PROF_SLOT_APP_10MS      1U  /**< App_Task_10ms() execution time   */
#define PROF_SLOT_CANTP_MAIN    2U  /**< CanTp_MainFunction() time        */
#define PROF_SLOT_COM_RX        3U  /**< Com_MainFunction_Rx() time       */
#define PROF_SLOT_COM_TX        4U  /**< Com_MainFunction_Tx() time       */
#define PROF_SLOT_USER_0        5U  /**< Free for ad-hoc use              */
#define PROF_SLOT_USER_1        6U  /**< Free for ad-hoc use              */
#define PROF_SLOT_USER_2        7U  /**< Free for ad-hoc use              */

#endif /* PROFILING_H */
