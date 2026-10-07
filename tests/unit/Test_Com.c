#include "com/Com.h"
#include "canif/CanIf.h"
#include "pdur/PduR.h"
#include "candrv/Can.h"
#include "trace/Trace.h"
#include "device_registers.h"
#include "cantp/CanTp.h"
#include "app/Role.h"
#include "app/App.h"
#include <string.h>

static int test_fails = 0;

#define ASSERT_EQ(actual, expected) \
    if ((actual) != (expected)) { \
        TRACE("[TEST] FAIL: %s (%u) != %s (%u) at line %d", #actual, (uint32)(actual), #expected, (uint32)(expected), __LINE__); \
        test_fails++; \
    }

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        TRACE("[TEST] FAIL: %s is FALSE at line %d", #condition, __LINE__); \
        test_fails++; \
    }

extern uint8   Com_IpduBuffer[6][8];
extern boolean Com_IsPending[6];
extern uint16  Com_TimerTicks[6];
extern uint8   Com_RetryCount[6];

/* --- Configuration Validation Tests --- */

void Test_Com_ConfigValidation(void) {
    /* Baseline: should pass */
    ASSERT_EQ(Com_ValidateConfig(), E_OK);
}

/* --- Packing/Unpacking Endianness Tests --- */

void Test_Com_PackUnpack_LittleEndian(void) {
    Com_Init();
    uint8 counter = 0x42; /* AliveCounter, LE, startBit 0, len 8 */
    Com_SendSignal(0, &counter);

    /* Encoded: (0x42 << 1) | 1 = 0x85 */
    ASSERT_EQ(Com_IpduBuffer[0][0], 0x85);

    uint8 rxCounter = 0;
    Com_ReceiveSignal(0, &rxCounter);
    ASSERT_EQ(rxCounter, 0x42);

    uint8 rate = 5; /* KeepAliveRateLevel, LE, startBit 8, len 8 */
    Com_SendSignal(1, &rate);

    /* Encoded: (5 << 1) | 1 = 0x0B */
    ASSERT_EQ(Com_IpduBuffer[0][1], 0x0B);

    uint8 rxRate = 0;
    Com_ReceiveSignal(1, &rxRate);
    ASSERT_EQ(rxRate, 5);
}

void Test_Com_UpdateBitLogic(void) {
    Com_Init();
    Can_Init(&Can_Config);
    CanIf_Init();
    PduR_Init();
    Role_Set(ROLE_1_ENGINE_TX); /* Role 1 owns IPDU 0 */

    uint8 counter = 0x42;
    uint8 rate    = 0x05;
    Com_SendSignal(0, &counter);
    Com_SendSignal(1, &rate);

    /* Update bits (bit 0 of encoded signal) are set */
    ASSERT_EQ(Com_IpduBuffer[0][0] & 1, 1);
    ASSERT_EQ(Com_IpduBuffer[0][1] & 1, 1);

    /* Transmit (trigger E_OK) */
    Com_TimerTicks[0] = 0;
    Com_MainFunction_Tx(); /* Deadline hit: pending=TRUE, retry=0, transmit -> E_OK */

    /* Should clear update bits, but leave payload */
    ASSERT_EQ(Com_IpduBuffer[0][0] & 1, 0);
    ASSERT_EQ(Com_IpduBuffer[0][1] & 1, 0);

    /* Payloads must be intact */
    uint8 rxCounter = 0;
    uint8 rxRate    = 0;
    Com_ReceiveSignal(0, &rxCounter);
    Com_ReceiveSignal(1, &rxRate);
    ASSERT_EQ(rxCounter, 0x42);
    ASSERT_EQ(rxRate, 0x05);

    /* Pending cleared */
    ASSERT_EQ(Com_IsPending[0], FALSE);
}

/* --- Scheduler and Retry Tests --- */

void Test_Com_RetrySemantics(void) {
    Com_Init();
    Can_Init(&Can_Config);
    Role_Set(ROLE_1_ENGINE_TX);

    /* Fake CAN MB0 to FULL so it's BUSY */
    CAN0->RAMn[0*4] = 0x02000000; /* CODE = FULL */

    Com_TimerTicks[0] = 0; /* Force deadline */

    /* Attempt 1 (Initial) */
    Com_MainFunction_Tx();
    ASSERT_EQ(Com_IsPending[0], TRUE);
    ASSERT_EQ(Com_RetryCount[0], 1);

    /* Attempt 2 (Retry 1) */
    Com_MainFunction_Tx();
    ASSERT_EQ(Com_RetryCount[0], 2);

    /* Attempt 3 (Retry 2) */
    Com_MainFunction_Tx();
    ASSERT_EQ(Com_RetryCount[0], 3);

    /* Attempt 4 (Retry 3) - Final allowed attempt */
    Com_MainFunction_Tx();
    ASSERT_EQ(Com_IsPending[0], FALSE);
    ASSERT_EQ(Com_RetryCount[0], 0); /* Dropped */

    /* Update bits should remain set after drop */
    uint8 counter = 10;
    Com_SendSignal(0, &counter);
    ASSERT_EQ(Com_IpduBuffer[0][0] & 1, 1);

    /* Clear fake BUSY */
    CAN0->RAMn[0*4] = 0;
}

/* --- Value Range Check Test (assignment §15 / Com_SendSignal) --- */

void Test_Com_SendSignal_RangeCheck(void) {
    Com_Init();

    /* AliveCounter: uint8, slotLength=8 → payloadBits=7 → max=127 */
    uint8 maxOk = 127U;
    ASSERT_EQ(Com_SendSignal(0, &maxOk), E_OK);

    uint8 overMax = 128U;
    ASSERT_EQ(Com_SendSignal(0, &overMax), E_NOT_OK);

    /* KeepAliveRateLevel: uint8, slotLength=8 → payloadBits=7 → max=127 */
    uint8 rateOk = 127U;
    ASSERT_EQ(Com_SendSignal(1, &rateOk), E_OK);

    uint8 rateBad = 128U;
    ASSERT_EQ(Com_SendSignal(1, &rateBad), E_NOT_OK);
}

/* --- ISR/Task Separation Test (architecture_notes §14) --- */

static boolean rxCallbackFired = FALSE;
static PduIdType rxCallbackPduId = 0xFFU;

static void Test_RxCallback(PduIdType pduId) {
    rxCallbackFired = TRUE;
    rxCallbackPduId = pduId;
}

void Test_Com_RxIsrTaskSeparation(void) {
    Com_Init();
    Can_Init(&Can_Config);
    CanIf_Init();
    PduR_Init();
    Com_SetRxCallback(Test_RxCallback);
    rxCallbackFired  = FALSE;
    rxCallbackPduId  = 0xFFU;

    /* Simulate ISR: inject Rx frame for KeepAliveRx (CAN ID=0x100, HRH=1) */
    Can_RxPduType rxPdu;
    rxPdu.id        = 0x100;
    rxPdu.length    = 8;
    /* AliveCounter=42 encoded LE: (42<<1)|1 = 0x55 */
    rxPdu.sdu[0] = 0x55; rxPdu.sdu[1] = 0x00;
    rxPdu.sdu[2] = 0x00; rxPdu.sdu[3] = 0x00;
    rxPdu.sdu[4] = 0x00; rxPdu.sdu[5] = 0x00;
    rxPdu.sdu[6] = 0x00; rxPdu.sdu[7] = 0x00;

    /* ISR path: CanIf_RxIndication → PduR → Com_RxIndication (flag only) */
    CanIf_RxIndication(1, &rxPdu);

    /* After ISR: callback must NOT have fired yet (Task not run) */
    ASSERT_EQ(rxCallbackFired, FALSE);

    /* Buffer must contain raw bytes (IPDU 3 is KeepAlive Rx) */
    ASSERT_EQ(Com_IpduBuffer[3][0], 0x55);

    /* Run Task context (1 ms scheduler tick) */
    Com_MainFunction_Rx();

    /* After Task: callback fires because UpdateBit of AliveCounter == 1 */
    ASSERT_EQ(rxCallbackFired, TRUE);
    ASSERT_EQ(rxCallbackPduId, 3);

    /* Decode payload */
    uint8 rxCounter = 0;
    Com_ReceiveSignal(4, &rxCounter);  /* Signal 4 = RxAliveCounter */
    ASSERT_EQ(rxCounter, 42);
}

/* --- PduR End-to-End Rx Test (F-03 / F-07) --- */

void Test_Com_RxEndToEnd(void) {
    Com_Init();
    Can_Init(&Can_Config);
    CanIf_Init();
    PduR_Init();

    /* Inject CAN RX frame ID 0x100 (KeepAliveRx) */
    Can_RxPduType rxPdu;
    rxPdu.id        = 0x100;
    rxPdu.length    = 8;
    rxPdu.sdu[0]    = (20 << 1) | 1; /* counter 20, update bit 1 */
    rxPdu.sdu[1]    = 0;

    /* Exercises: CanIf -> PduR -> Com_RxIndication (ISR part) */
    CanIf_RxIndication(1, &rxPdu);

    /* Tx buffer [0] not modified */
    ASSERT_EQ(Com_IpduBuffer[0][0], 0);

    /* Rx buffer [3] is modified */
    ASSERT_EQ(Com_IpduBuffer[3][0], 41); /* (20 << 1) | 1 */
}

void Test_Can_Ctrl1ClkSrc(void) {
    /* Ensure CAN clock and controller are initialized before reading CTRL1 (CLKSRC=0 for SOSC 8MHz) */
    Can_Init(&Can_Config);
    ASSERT_EQ(CAN0->CTRL1 & CAN_CTRL1_CLKSRC_MASK, 0U);
}

/* --- CanTP Tests are comprehensively implemented in Test_CanTp.c (T01 - T14) --- */
extern void Run_All_CanTp_Tests(void);


/* --- Role Selection Test --- */

void Test_Role_Selection(void) {
#if defined(__arm__) || defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT)
    /* On physical ARM target hardware, PTC->PDIR is a READ-ONLY register.
     * Writing to PDIR raises a hardware HardFault/UsageFault.
     * Simply test that Role_Init() completes safely and reads the current pin state. */
    Role_Init();
    RoleType currentRole = Role_Get();
    ASSERT_TRUE(currentRole <= ROLE_2_BODY_TX);
#else
    /* Save original PDIR state */
    uint32 savedPDIR = PTC->PDIR;

    /* Helper macro to write to read-only PDIR member in host simulation environment */
    #define PTC_PDIR_WRITE (*(volatile uint32_t *)(void *)&PTC->PDIR)

    /* Test Role 0: Both buttons released (HIGH) → role = 0 */
    PTC_PDIR_WRITE |=  (1U << ROLE_SW2_PIN);  /* SW2 HIGH (released) */
    PTC_PDIR_WRITE |=  (1U << ROLE_SW3_PIN);  /* SW3 HIGH (released) */
    Role_Init();
    ASSERT_EQ(Role_Get(), ROLE_0_VEHICLE_TX);

    /* Test Role 1: SW2 pressed (LOW), SW3 released (HIGH) → role = 1 */
    PTC_PDIR_WRITE &= ~(1U << ROLE_SW2_PIN);  /* SW2 LOW (pressed) */
    PTC_PDIR_WRITE |=  (1U << ROLE_SW3_PIN);  /* SW3 HIGH (released) */
    Role_Init();
    ASSERT_EQ(Role_Get(), ROLE_1_ENGINE_TX);

    /* Test Role 2: SW2 released (HIGH), SW3 pressed (LOW) → role = 2 */
    PTC_PDIR_WRITE |=  (1U << ROLE_SW2_PIN);  /* SW2 HIGH (released) */
    PTC_PDIR_WRITE &= ~(1U << ROLE_SW3_PIN);  /* SW3 LOW (pressed) */
    Role_Init();
    ASSERT_EQ(Role_Get(), ROLE_2_BODY_TX);

    /* Test Role Reserved: Both pressed → defaults to Role 0 */
    PTC_PDIR_WRITE &= ~(1U << ROLE_SW2_PIN);  /* SW2 LOW (pressed) */
    PTC_PDIR_WRITE &= ~(1U << ROLE_SW3_PIN);  /* SW3 LOW (pressed) */
    Role_Init();
    ASSERT_EQ(Role_Get(), ROLE_0_VEHICLE_TX);

    /* Restore original GPIO state */
    PTC_PDIR_WRITE = savedPDIR;
    Role_Init();

    #undef PTC_PDIR_WRITE
#endif
}

/* --- Task 3: Slave Status Monitoring Tests (Assignment §4 & §8) --- */

void Test_Task3_SlaveStatusMonitoring(void) {
    /* Set Role to Master (Role 1) */
    Role_Set(ROLE_1_ENGINE_TX);
    App_Init();
    Can_Init(&Can_Config);
    CanIf_Init();
    PduR_Init();

    /* Initially on boot, 0 slaves online */
    ASSERT_EQ(App_GetOnlineSlavesCount(), 0);
    ASSERT_EQ(App_IsSlaveOnline(1), FALSE);
    ASSERT_EQ(App_IsSlaveOnline(2), FALSE);

    /* Inject Slave 1 Status: CAN ID 0x201, HRH=1, Slave1Status=0 NORMAL */
    Can_RxPduType rxPdu1;
    rxPdu1.id     = 0x201;
    rxPdu1.length = 8;
    rxPdu1.sdu[0] = (0U << 1) | 1U; /* Slave1Status=0 (NORMAL), updateBit=1 */
    rxPdu1.sdu[1] = 0; rxPdu1.sdu[2] = 0; rxPdu1.sdu[3] = 0;
    rxPdu1.sdu[4] = 0; rxPdu1.sdu[5] = 0; rxPdu1.sdu[6] = 0; rxPdu1.sdu[7] = 0;

    CanIf_RxIndication(1, &rxPdu1);
    Com_MainFunction_Rx();

    /* Slave 1 should now be ONLINE, Slave 2 still OFFLINE -> 1/2 */
    ASSERT_EQ(App_IsSlaveOnline(1), TRUE);
    ASSERT_EQ(App_IsSlaveOnline(2), FALSE);
    ASSERT_EQ(App_GetOnlineSlavesCount(), 1);
    ASSERT_EQ(App_GetSlaveStatus(1), 0); /* NORMAL */

    /* Inject Slave 2 Status: CAN ID 0x202, HRH=1, Slave2Status=0 NORMAL */
    Can_RxPduType rxPdu2;
    rxPdu2.id     = 0x202;
    rxPdu2.length = 8;
    rxPdu2.sdu[0] = (0U << 1) | 1U; /* Slave2Status=0 (NORMAL), updateBit=1 */
    rxPdu2.sdu[1] = 0; rxPdu2.sdu[2] = 0; rxPdu2.sdu[3] = 0;
    rxPdu2.sdu[4] = 0; rxPdu2.sdu[5] = 0; rxPdu2.sdu[6] = 0; rxPdu2.sdu[7] = 0;

    CanIf_RxIndication(1, &rxPdu2);
    Com_MainFunction_Rx();

    /* Both slaves should now be ONLINE -> 2/2 */
    ASSERT_EQ(App_IsSlaveOnline(1), TRUE);
    ASSERT_EQ(App_IsSlaveOnline(2), TRUE);
    ASSERT_EQ(App_GetOnlineSlavesCount(), 2);

    /* Advance time by 2001ms without receiving any message from slaves */
    extern volatile uint32 g_sysTick_ms;
    g_sysTick_ms += 2001U;

    /* Run 10ms task to perform network monitoring timeout check */
    App_Task_10ms();

    /* Both slaves should now be OFFLINE (>2000ms timeout) -> 0/2 */
    ASSERT_EQ(App_IsSlaveOnline(1), FALSE);
    ASSERT_EQ(App_IsSlaveOnline(2), FALSE);
    ASSERT_EQ(App_GetOnlineSlavesCount(), 0);

    /* Reconnect Slave 1 -> reports 1/2 again */
    CanIf_RxIndication(1, &rxPdu1);
    Com_MainFunction_Rx();
    ASSERT_EQ(App_IsSlaveOnline(1), TRUE);
    ASSERT_EQ(App_GetOnlineSlavesCount(), 1);
}

void Run_All_Unit_Tests(void) {
    TRACE("[TEST] Starting Unit Tests...");
    test_fails = 0;

    Test_Com_ConfigValidation();
    Test_Com_PackUnpack_LittleEndian();
    Test_Com_UpdateBitLogic();
    Test_Com_RetrySemantics();
    Test_Com_SendSignal_RangeCheck();
    Test_Com_RxIsrTaskSeparation();
    Test_Com_RxEndToEnd();
    Test_Can_Ctrl1ClkSrc();
    Run_All_CanTp_Tests();
    Test_Role_Selection();
    Test_Task3_SlaveStatusMonitoring();

    if (test_fails == 0) {
        TRACE("[TEST] All Unit Tests PASSED.");
    } else {
        TRACE("[TEST] %d Unit Tests FAILED.", test_fails);
    }
}
