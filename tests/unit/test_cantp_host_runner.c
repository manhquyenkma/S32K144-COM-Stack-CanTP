#if !defined(__arm__) && !defined(CPU_S32K144HFT0VLLT) && !defined(CPU_S32K144LFT0MLLT)

#include <stdio.h>
#include <stdarg.h>
#include "Std_Types.h"
#include "cantp/CanTp.h"
#include "candrv/Can.h"

volatile uint32 g_sysTick_ms = 0;
volatile uint32 Trace_DroppedCount = 0;

void Trace_Init(void) {}
void Trace_Flush(void) { fflush(stdout); }
void Trace_FlushBlocking(void) { fflush(stdout); }
char Trace_GetChar(void) { return '\0'; }

void Trace_Enqueue(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

/* Lower CAN driver stubs for CanIf */
Can_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType* PduInfo) {
    (void)Hth;
    (void)PduInfo;
    return CAN_OK;
}
void Can_Init(const Can_ConfigType* Config) { (void)Config; }
void Can_MainFunction_Write(void) {}
void Can_MainFunction_Read(void) {}

/* COM stubs for PduR and App */
void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr) {
    (void)RxPduId;
    (void)PduInfoPtr;
}
void Com_TxConfirmation(PduIdType TxPduId) {
    (void)TxPduId;
}
typedef void (*Com_RxCallbackType)(PduIdType RxPduId);
void Com_SetRxCallback(Com_RxCallbackType callback) {
    (void)callback;
}
Std_ReturnType Com_ValidateConfig(void) { return E_OK; }
uint8 Com_SendSignal(uint8 SignalId, const void* SignalDataPtr) {
    (void)SignalId; (void)SignalDataPtr; return 0;
}
uint8 Com_ReceiveSignal(uint8 SignalId, void* SignalDataPtr) {
    (void)SignalId; (void)SignalDataPtr; return 0;
}

/* Role stubs for App */
#include "app/Role.h"
RoleType Role_Get(void) { return ROLE_0_VEHICLE_TX; }
const char* Role_GetName(void) { return "ROLE_0_VEHICLE_TX"; }

/* Platform stubs for App */
void Platform_LedSet(uint8 r, uint8 g, uint8 b) { (void)r; (void)g; (void)b; }
void Platform_LedToggleBlue(void) {}
void Platform_LedToggleGreen(void) {}
uint16 Platform_AdcReadPot(void) { return 0; }



extern int Run_All_CanTp_Tests(void);

int main(void) {
    int failures = Run_All_CanTp_Tests();
    if (failures == 0) {
        printf("\n>>> [HOST RUNNER SUCCESS] ALL 14 TESTS PASSED! <<<\n");
        return 0;
    } else {
        printf("\n>>> [HOST RUNNER FAILURE] %d TESTS FAILED! <<<\n", failures);
        return 1;
    }
}

#endif /* !defined(__arm__) */

