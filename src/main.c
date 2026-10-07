#include "Platform_Init.h"
#include "trace/Trace.h"
#include "candrv/Can.h"
#include "canif/CanIf.h"
#include "pdur/PduR.h"
#include "com/Com.h"
#include "cantp/CanTp.h"
#include "app/Role.h"
#include "app/App.h"
#include "app/UartRxQueue.h"
#include "app/Profiling.h"
#include "device_registers.h"

int main(void) {
    Platform_Init();
    Trace_Init();

    /* Clear screen / print prominent banner */
    TRACE("\r\n\r\n=========================================");
    TRACE("[SYS] S32K144 AUTOSAR Mock COM Stack");
    TRACE("[SYS] Platform & Trace Init Complete");
    TRACE("[SYS] SOSC 8MHz Crystal: %s (CSR=0x%08lX)", (SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK) ? "VALID (Locked)" : "INVALID (Check Crystal)", (unsigned long)SCG->SOSCCSR);
    TRACE("[SBC] Forced Normal Mode active (Watchdog disabled, matching Can_Ex)");

    Role_Init();
    RoleType selectedRole = Role_Get(); /* Hardware button detection */

    /* Default to Role 1 (Master) when no buttons are pressed (matching Can_Ex) */
    if (selectedRole == ROLE_0_VEHICLE_TX) {
        selectedRole = ROLE_1_ENGINE_TX;
    }

    TRACE("=========================================");
    TRACE("[SYS] Default Role: %s", (selectedRole == ROLE_1_ENGINE_TX) ? "ROLE 1 (Master)" : Role_GetName());
    TRACE("[SYS] Type key within 1.5s to override: [1=Master, 2=Slave1, 0=Slave2]");
    TRACE("=========================================");
    Trace_FlushBlocking();

    /* Fast 1.5-second countdown loop with UART polling */
    uint8 roleOverridden = 0;
    for (uint32 sec = 2; sec > 0; sec--) {
        for (volatile uint32 wait = 0; wait < 1500000U; wait++) {
            Trace_Flush();
            char c = Trace_GetChar();
            if (c == '1') {
                selectedRole = ROLE_1_ENGINE_TX;
                TRACE("[SYS] -> KEY PRESSED: 1 (ROLE 1 - Master)\r\n");
                Trace_FlushBlocking();
                roleOverridden = 1;
                break;
            } else if (c == '2') {
                selectedRole = ROLE_2_BODY_TX;
                TRACE("[SYS] -> KEY PRESSED: 2 (ROLE 2 - Slave 1)\r\n");
                Trace_FlushBlocking();
                roleOverridden = 1;
                break;
            } else if (c == '0') {
                selectedRole = ROLE_0_VEHICLE_TX;
                TRACE("[SYS] -> KEY PRESSED: 0 (ROLE 0 - Slave 2)\r\n");
                Trace_FlushBlocking();
                roleOverridden = 1;
                break;
            }
        }
        if (roleOverridden != 0) {
            break;
        }
    }

    Role_Set(selectedRole);
    TRACE("\r\n=========================================");
    TRACE("[SYS] >>> FINAL SELECTED ROLE: %s <<<", Role_GetName());
    if (selectedRole == ROLE_1_ENGINE_TX) {
        TRACE("[SYS] ROLE 1: MASTER ECU (KeepAlive TX 0x100 + Image SENDER)");
    } else if (selectedRole == ROLE_2_BODY_TX) {
        TRACE("[SYS] ROLE 2: SLAVE 1 (Status TX 0x201 + Image RECEIVER)");
    } else {
        TRACE("[SYS] ROLE 0: SLAVE 2 (Status TX 0x202)");
    }
    TRACE("=========================================\r\n");
    Trace_FlushBlocking();

    TRACE("[DIAG] >> Can_Init");   Trace_Flush();
    Can_Init(&Can_Config);
    TRACE("[DIAG] << Can_Init OK");Trace_Flush();

    TRACE("[DIAG] >> CanIf_Init"); Trace_Flush();
    CanIf_Init();
    TRACE("[DIAG] << CanIf OK");   Trace_Flush();

    TRACE("[DIAG] >> PduR_Init");  Trace_Flush();
    PduR_Init();
    TRACE("[DIAG] << PduR OK");    Trace_Flush();

    TRACE("[DIAG] >> Com_Init");   Trace_Flush();
    Com_Init();
    TRACE("[DIAG] << Com OK");     Trace_Flush();

    TRACE("[DIAG] >> CanTp_Init"); Trace_Flush();
    CanTp_Init();
    TRACE("[DIAG] << CanTp OK");   Trace_Flush();

    TRACE("[DIAG] >> App_Init");   Trace_Flush();
    App_Init();
    TRACE("[DIAG] << App OK");     Trace_Flush();

    TRACE("[DIAG] >> UartRxQueue_Init"); Trace_Flush();
    UartRxQueue_Init();
    TRACE("[DIAG] << UartRxQueue OK");   Trace_Flush();

    TRACE("[DIAG] >> Profiling_Init");   Trace_Flush();
    Profiling_Init();
    TRACE("[DIAG] << Profiling OK (DWT CYCCNT enabled)"); Trace_Flush();

#ifdef ENABLE_ON_TARGET_UNIT_TESTS
    extern void Run_All_Unit_Tests(void);
    Run_All_Unit_Tests();
    Trace_Flush();
    Role_Set(selectedRole);
    Can_Init(&Can_Config);
    CanIf_Init();
    PduR_Init();
    Com_Init();
    CanTp_Init();
#endif

    TRACE("[SYS] System ready - entering scheduler loop as %s", Role_GetName());
    if (selectedRole == ROLE_2_BODY_TX) {
        TRACE("\r\n========================================================");
        TRACE(">>> ASCII ART DISPLAY TERMINAL READY <<<");
        TRACE("========================================================\r\n");
        Trace_FlushBlocking();
        /* Mute all debug TRACE logs so UART on Slave 1 is 100% dedicated to raw ASCII image stream */
        Trace_SetEnabled(FALSE);
    } else {
        Trace_Flush();
    }

    uint32 lastTick = g_sysTick_ms;
    uint32 appTick  = 0U;

    for (;;) {
        while ((uint32)(g_sysTick_ms - lastTick) > 0U) {
            lastTick++;
            appTick++;
            Can_MainFunction_Write();
            Can_MainFunction_Read();

            profiling_start(PROF_SLOT_COM_RX);
            Com_MainFunction_Rx();
            profiling_stop(PROF_SLOT_COM_RX);

            profiling_start(PROF_SLOT_CANTP_MAIN);
            CanTp_MainFunction();
            profiling_stop(PROF_SLOT_CANTP_MAIN);

            profiling_start(PROF_SLOT_COM_TX);
            Com_MainFunction_Tx();
            profiling_stop(PROF_SLOT_COM_TX);

            UartRxQueue_Poll();  /* Collect UART Rx bytes for image reception */

            profiling_start(PROF_SLOT_APP_1MS);
            App_Task_1ms();      /* 1ms KeepAlive ADC update and Slave LED modulation */
            profiling_stop(PROF_SLOT_APP_1MS);

            if ((appTick % 10U) == 0U) {
                profiling_start(PROF_SLOT_APP_10MS);
                App_Task_10ms();
                profiling_stop(PROF_SLOT_APP_10MS);
            }
        }
        UartRxQueue_Poll(); /* Fast polling during idle periods to prevent FIFO overrun */
        Trace_Flush();
    }

    return 0;
}

