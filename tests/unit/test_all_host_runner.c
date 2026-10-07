#if !defined(__arm__) && !defined(CPU_S32K144HFT0VLLT) && !defined(CPU_S32K144LFT0MLLT)

#include <stdio.h>
#include <stdarg.h>
#include "Std_Types.h"
#include "device_registers.h"
#include "cantp/CanTp.h"
#include "candrv/Can.h"
#include "canif/CanIf.h"
#include "pdur/PduR.h"
#include "com/Com.h"
#include "app/Role.h"
#include "app/App.h"

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
    printf("\n");
    va_end(args);
    fflush(stdout);
}

/* Mock hardware registers for host */
CAN_Type g_mock_can0;
PORT_Type g_mock_portc;
GPIO_Type g_mock_ptc;

/* Lower CAN driver stubs for CanIf */
const Can_ConfigType Can_Config;

Can_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType* PduInfo) {
    (void)PduInfo;
    uint32 code = g_mock_can0.RAMn[Hth * 4] & 0x0F000000U;
    if (code != 0x08000000U && code != 0x00000000U && code != 0x09000000U) {
        return CAN_BUSY;
    }
    return CAN_OK;
}
void Can_Init(const Can_ConfigType* Config) {
    (void)Config;
    g_mock_can0.CTRL1 &= ~CAN_CTRL1_CLKSRC_MASK;
}
void Can_MainFunction_Write(void) {}
void Can_MainFunction_Read(void) {}

/* Platform stubs for App */
void Platform_LedSet(uint8 r, uint8 g, uint8 b) { (void)r; (void)g; (void)b; }
void Platform_LedToggleBlue(void) {}
void Platform_LedToggleGreen(void) {}
uint16 Platform_AdcReadPot(void) { return 0; }

extern void Run_All_Unit_Tests(void);

int main(void) {
    printf("\n==================================================\n");
    printf("RUNNING ALL UNIT TESTS (COM + APP + CANTP)\n");
    printf("==================================================\n\n");
    Run_All_Unit_Tests();
    return 0;
}

#endif /* !defined(__arm__) */
