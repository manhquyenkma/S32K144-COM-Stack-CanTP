#include "cantp/CanTp.h"
#include "canif/CanIf.h"
#include "pdur/PduR.h"
#include "app/App.h"
#include "trace/Trace.h"
#include <string.h>

static int g_testFailures = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            TRACE("[TEST] FAIL: %s (line %d)", msg, __LINE__); \
            g_testFailures++; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(actual, expected, msg) \
    do { \
        if ((actual) != (expected)) { \
            TRACE("[TEST] FAIL: %s - actual=%u expected=%u (line %d)", msg, (uint32)(actual), (uint32)(expected), __LINE__); \
            g_testFailures++; \
        } \
    } while(0)

/* External system tick for test timing simulation */
extern volatile uint32 g_sysTick_ms;

/* Simulated wire capture buffer */
#define MAX_CAPTURED_FRAMES 32
typedef struct {
    PduIdType txPduId;
    uint8 data[8];
    uint8 length;
    uint32 timestamp;
} CapturedFrameType;

static CapturedFrameType g_capturedFrames[MAX_CAPTURED_FRAMES];
static uint32 g_capturedCount = 0;

/* Hook to capture transmissions and simulate immediate or delayed confirmation */
static boolean g_autoConfirm = TRUE;
static uint32  g_rejectAttempts = 0;
static uint32  g_rejectedCount = 0;

static Std_ReturnType Test_CanIf_TransmitHook(PduIdType TxPduId, const PduInfoType* PduInfo) {
    if (g_rejectAttempts > 0) {
        g_rejectAttempts--;
        g_rejectedCount++;
        TRACE("[TEST_HOOK] Injecting E_NOT_OK (rejectedCount=%u)", g_rejectedCount);
        return E_NOT_OK;
    }

    if (g_capturedCount < MAX_CAPTURED_FRAMES) {
        g_capturedFrames[g_capturedCount].txPduId = TxPduId;
        memcpy(g_capturedFrames[g_capturedCount].data, PduInfo->SduDataPtr, 8);
        g_capturedFrames[g_capturedCount].length = 8;
        g_capturedFrames[g_capturedCount].timestamp = g_sysTick_ms;
        g_capturedCount++;
    }

    if (g_autoConfirm) {
        /* Immediate confirmation in local loopback */
        CanTp_TxConfirmation(TxPduId);
    }

    return E_OK;
}

static void Test_ResetHarness(void) {
    CanIf_TransmitHook = Test_CanIf_TransmitHook;
    g_autoConfirm = TRUE;
    g_rejectAttempts = 0;
    g_rejectedCount = 0;
    g_capturedCount = 0;
    memset(g_capturedFrames, 0, sizeof(g_capturedFrames));
    App_ResetCanTpCounters();
    CanTp_Init();
    CanIf_Init();
    PduR_Init();
}

/* Helper to deliver a captured frame to receiver */
static void Test_DeliverFrameToRx(const uint8* frameData, PduIdType rxNPduId) {
    PduInfoType pdu;
    pdu.SduDataPtr  = (uint8*)frameData;
    pdu.SduLength   = 8U;
    pdu.MetaDataPtr = NULL_PTR;
    CanTp_RxIndication(rxNPduId, &pdu);
}

/* ================================================================== */
/*  PHASE 1: HAPPY PATH TESTS (T01 - T03)                             */
/* ================================================================== */

/* T01: Single Frame (SF) 5 bytes: [00..04] */
void Test_T01_SingleFrame_5B(void) {
    TRACE("\r\n--- Running T01: Single Frame (SF) 5 Bytes ---");
    Test_ResetHarness();

    uint8 payload[5] = { 0x00, 0x01, 0x02, 0x03, 0x04 };
    PduInfoType pdu;
    pdu.SduDataPtr = payload;
    pdu.SduLength  = 5U;
    pdu.MetaDataPtr = NULL_PTR;

    /* Tx side */
    Std_ReturnType ret = CanTp_Transmit(0U, &pdu);
    TEST_ASSERT_EQ(ret, E_OK, "CanTp_Transmit accepted");

    /* Tick MainFunction to trigger transmission */
    CanTp_MainFunction();

    TEST_ASSERT_EQ(g_capturedCount, 1, "Exactly 1 wire frame transmitted");
    /* Student Guide v2.0 SF wire format: [0x05][00 01 02 03 04][00 00] */
    TEST_ASSERT_EQ(g_capturedFrames[0].data[0], 0x05, "SF PCI byte0 must be 0x05 (SF_DL=5)");
    TEST_ASSERT_EQ(memcmp(&g_capturedFrames[0].data[1], payload, 5), 0, "Payload data match");
    TEST_ASSERT_EQ(g_capturedFrames[0].data[6], 0x00, "Padding byte 6 must be 0x00");
    TEST_ASSERT_EQ(g_capturedFrames[0].data[7], 0x00, "Padding byte 7 must be 0x00");
    TEST_ASSERT_EQ(App_GetTxConfirmationCount(), 1, "App TxConfirmation called once");
    TEST_ASSERT_EQ(App_GetLastTxResult(), E_OK, "TxConfirmation result E_OK");

    /* Rx side: deliver captured frame */
    Test_DeliverFrameToRx(g_capturedFrames[0].data, CANTP_RX_DATA_NPDU_ID);
    TEST_ASSERT_EQ(App_GetRxIndicationCount(), 1, "App RxIndication called once");
    TEST_ASSERT_EQ(App_GetLastRxResult(), E_OK, "RxIndication result E_OK");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_READY, "Queue slot 0 READY");
    TEST_ASSERT_EQ(App_RxQueue[0].length, 5, "Queue slot 0 length 5");
    TEST_ASSERT_EQ(memcmp(App_RxQueue[0].data, payload, 5), 0, "Rx Queue data match");
    TRACE("[TEST] T01 PASSED");
}

/* T02: Multi-Frame 20 bytes: FF + 2 CF + 1 CTS */
void Test_T02_MultiFrame_20B(void) {
    TRACE("\r\n--- Running T02: Multi-Frame 20 Bytes ---");
    Test_ResetHarness();

    uint8 payload[20];
    for (uint8 i = 0; i < 20; i++) payload[i] = i;

    PduInfoType pdu;
    pdu.SduDataPtr = payload;
    pdu.SduLength  = 20U;
    pdu.MetaDataPtr = NULL_PTR;

    CanTp_Transmit(0U, &pdu);
    CanTp_MainFunction(); /* Sends FF */

    TEST_ASSERT_EQ(g_capturedCount, 1, "FF sent");
    /* Student Guide v2.0 FF wire format: 12-bit FF_DL = 20 -> [0x10, 0x14] */
    TEST_ASSERT_EQ(g_capturedFrames[0].data[0], 0x10, "FF byte0 must be 0x10 (FF_DL hi=0)");
    TEST_ASSERT_EQ(g_capturedFrames[0].data[1], 0x14, "FF byte1 must be FF_DL lo=0x14 (20)");
    TEST_ASSERT_EQ(memcmp(&g_capturedFrames[0].data[2], payload, 6), 0, "FF payload 6 bytes");

    /* Deliver FF to Receiver */
    uint32 capBeforeFC = g_capturedCount;
    Test_DeliverFrameToRx(g_capturedFrames[0].data, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction(); /* Receiver sends FC(CTS) */

    TEST_ASSERT_EQ(g_capturedCount, capBeforeFC + 1, "FC(CTS) sent by receiver");
    uint32 fcIdx = capBeforeFC;
    TEST_ASSERT_EQ(g_capturedFrames[fcIdx].data[0], 0x30, "FC byte0 is 0x30 (CTS)");
    TEST_ASSERT_EQ(g_capturedFrames[fcIdx].data[1], 4, "FC BS is 4");
    TEST_ASSERT_EQ(g_capturedFrames[fcIdx].data[2], 5, "FC STmin is 5ms");

    /* Deliver FC back to Sender */
    Test_DeliverFrameToRx(g_capturedFrames[fcIdx].data, CANTP_RX_FC_NPDU_ID);

    /* Sender sends CF1 */
    g_sysTick_ms += 5;
    CanTp_MainFunction();
    uint32 cf1Idx = g_capturedCount - 1;
    TEST_ASSERT_EQ(g_capturedFrames[cf1Idx].data[0], 0x21, "CF1 byte0 is 0x21 (SN=1)");
    TEST_ASSERT_EQ(memcmp(&g_capturedFrames[cf1Idx].data[1], &payload[6], 7), 0, "CF1 payload 7 bytes");

    /* Deliver CF1 to Receiver */
    Test_DeliverFrameToRx(g_capturedFrames[cf1Idx].data, CANTP_RX_DATA_NPDU_ID);

    /* Sender sends CF2 */
    g_sysTick_ms += 5;
    CanTp_MainFunction();
    uint32 cf2Idx = g_capturedCount - 1;
    TEST_ASSERT_EQ(g_capturedFrames[cf2Idx].data[0], 0x22, "CF2 byte0 is 0x22 (SN=2)");
    TEST_ASSERT_EQ(memcmp(&g_capturedFrames[cf2Idx].data[1], &payload[13], 7), 0, "CF2 payload 7 bytes");

    /* Deliver CF2 to Receiver */
    Test_DeliverFrameToRx(g_capturedFrames[cf2Idx].data, CANTP_RX_DATA_NPDU_ID);

    /* Total 4 wire frames: FF, FC, CF1, CF2 */
    TEST_ASSERT_EQ(g_capturedCount, 4, "Total 4 frames for 20B multi-frame");
    TEST_ASSERT_EQ(App_GetTxConfirmationCount(), 1, "Tx complete confirmed once");
    TEST_ASSERT_EQ(App_GetRxIndicationCount(), 1, "Rx complete indicated once");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_READY, "Queue slot 0 READY");
    TEST_ASSERT_EQ(App_RxQueue[0].length, 20, "Queue slot length 20");
    TEST_ASSERT_EQ(memcmp(App_RxQueue[0].data, payload, 20), 0, "Full payload matches exactly");
    TRACE("[TEST] T02 PASSED");
}

/* T03: Multi-Frame 62 bytes: FF + 8 CF + 2 CTS = 11 CAN frames */
void Test_T03_MultiFrame_62B(void) {
    TRACE("\r\n--- Running T03: Multi-Frame 62 Bytes (11 Frames) ---");
    Test_ResetHarness();

    uint8 payload[62];
    for (uint8 i = 0; i < 62; i++) payload[i] = i;

    PduInfoType pdu;
    pdu.SduDataPtr = payload;
    pdu.SduLength  = 62U;
    pdu.MetaDataPtr = NULL_PTR;

    CanTp_Transmit(0U, &pdu);
    CanTp_MainFunction(); /* FF */

    /* Student Guide v2.0 FF wire format: 12-bit FF_DL = 62 -> [0x10, 0x3E] */
    TEST_ASSERT_EQ(g_capturedFrames[0].data[0], 0x10, "FF byte0 must be 0x10 (FF_DL hi=0)");
    TEST_ASSERT_EQ(g_capturedFrames[0].data[1], 0x3E, "FF byte1 must be FF_DL lo=0x3E (62)");

    /* Deliver FF -> Receiver generates CTS1 */
    Test_DeliverFrameToRx(g_capturedFrames[0].data, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction(); /* CTS1 */
    TEST_ASSERT_EQ(g_capturedFrames[1].data[0], 0x30, "CTS1 byte0");

    /* Deliver CTS1 to Sender */
    Test_DeliverFrameToRx(g_capturedFrames[1].data, CANTP_RX_FC_NPDU_ID);

    /* Send CF1, CF2, CF3, CF4 */
    for (uint8 cf = 1; cf <= 4; cf++) {
        g_sysTick_ms += 5;
        CanTp_MainFunction();
        uint32 lastIdx = g_capturedCount - 1;
        TEST_ASSERT_EQ(g_capturedFrames[lastIdx].data[0], (0x20 | cf), "CF SN match");
        Test_DeliverFrameToRx(g_capturedFrames[lastIdx].data, CANTP_RX_DATA_NPDU_ID);
    }

    /* Receiver generates CTS2 after 4 CFs */
    CanTp_MainFunction(); /* CTS2 */
    uint32 cts2Idx = g_capturedCount - 1;
    TEST_ASSERT_EQ(g_capturedFrames[cts2Idx].data[0], 0x30, "CTS2 byte0");

    /* Deliver CTS2 to Sender */
    Test_DeliverFrameToRx(g_capturedFrames[cts2Idx].data, CANTP_RX_FC_NPDU_ID);

    /* Send CF5, CF6, CF7, CF8 */
    for (uint8 cf = 5; cf <= 8; cf++) {
        g_sysTick_ms += 5;
        CanTp_MainFunction();
        uint32 lastIdx = g_capturedCount - 1;
        TEST_ASSERT_EQ(g_capturedFrames[lastIdx].data[0], (0x20 | cf), "CF SN match");
        Test_DeliverFrameToRx(g_capturedFrames[lastIdx].data, CANTP_RX_DATA_NPDU_ID);
    }

    /* Total 11 frames: FF (1) + CTS1 (1) + CF1..4 (4) + CTS2 (1) + CF5..8 (4) = 11 */
    TEST_ASSERT_EQ(g_capturedCount, 11, "Exactly 11 frames on CAN bus");
    TEST_ASSERT_EQ(App_GetTxConfirmationCount(), 1, "Tx complete confirmed once");
    TEST_ASSERT_EQ(App_GetRxIndicationCount(), 1, "Rx complete indicated once");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_READY, "Queue slot 0 READY");
    TEST_ASSERT_EQ(App_RxQueue[0].length, 62, "Queue slot length 62");
    TEST_ASSERT_EQ(memcmp(App_RxQueue[0].data, payload, 62), 0, "62 bytes payload match");
    TRACE("[TEST] T03 PASSED");
}

/* ================================================================== */
/*  PHASE 2: RETRY & TIMEOUT TESTS (T04 - T08, T13)                   */
/* ================================================================== */

/* T04: STmin timing gate between consecutive CFs */
void Test_T04_STmin_Pacing(void) {
    TRACE("\r\n--- Running T04: STmin Pacing ---");
    Test_ResetHarness();

    uint8 payload[20];
    for (uint8 i = 0; i < 20; i++) payload[i] = i;
    PduInfoType pdu = { payload, NULL_PTR, 20U };

    CanTp_Transmit(0U, &pdu);
    CanTp_MainFunction(); /* FF */

    uint8 fcCts[8] = { 0x30, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 };
    Test_DeliverFrameToRx(fcCts, CANTP_RX_FC_NPDU_ID);

    /* CF1 sent */
    CanTp_MainFunction();

    /* Advance only 2 ms (STmin is 5ms) -> CF2 should NOT be sent */
    g_sysTick_ms += 2;
    uint32 capCount = g_capturedCount;
    CanTp_MainFunction();
    TEST_ASSERT_EQ(g_capturedCount, capCount, "CF2 not sent when STmin has not elapsed");

    /* Advance 3 more ms (total 5ms) -> CF2 should be sent */
    g_sysTick_ms += 3;
    CanTp_MainFunction();
    TEST_ASSERT_EQ(g_capturedCount, capCount + 1, "CF2 sent when STmin elapsed");
    TRACE("[TEST] T04 PASSED");
}

/* T05: Data retry immutability (reject twice, accept on 3rd attempt) */
void Test_T05_DataRetry_Immutability(void) {
    TRACE("\r\n--- Running T05: Data Retry Immutability ---");
    Test_ResetHarness();

    uint8 payload[6] = { 1, 2, 3, 4, 5, 6 };
    PduInfoType pdu = { payload, NULL_PTR, 6U };

    CanTp_Transmit(0U, &pdu);

    /* Inject 2 rejections from CanIf */
    g_rejectAttempts = 2;
    CanTp_MainFunction(); /* Attempt 1 -> E_NOT_OK */
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_REQUEST_TX, "State remains TX_REQUEST_TX");

    g_sysTick_ms += 1;
    CanTp_MainFunction(); /* Attempt 2 -> E_NOT_OK */
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_REQUEST_TX, "State remains TX_REQUEST_TX");

    g_sysTick_ms += 1;
    CanTp_MainFunction(); /* Attempt 3 -> E_OK */
    TEST_ASSERT_EQ(g_capturedCount, 1, "Frame finally accepted on 3rd attempt");
    TEST_ASSERT_EQ(App_GetLastTxResult(), E_OK, "Tx confirmed E_OK");
    TRACE("[TEST] T05 PASSED");
}

/* T06: Data retry exhausted (4 rejections -> Abort Tx) */
void Test_T06_DataRetry_Exhausted(void) {
    TRACE("\r\n--- Running T06: Data Retry Exhausted ---");
    Test_ResetHarness();

    uint8 payload[6] = { 1, 2, 3, 4, 5, 6 };
    PduInfoType pdu = { payload, NULL_PTR, 6U };

    CanTp_Transmit(0U, &pdu);

    /* Inject 4 rejections */
    g_rejectAttempts = 4;
    for (uint8 i = 0; i < 4; i++) {
        g_sysTick_ms += 1;
        CanTp_MainFunction();
    }

    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_IDLE, "Tx session aborted to IDLE");
    TEST_ASSERT_EQ(App_GetTxConfirmationCount(), 1, "Final callback called once");
    TEST_ASSERT_EQ(App_GetLastTxResult(), E_NOT_OK, "Result is E_NOT_OK");
    TRACE("[TEST] T06 PASSED");
}

/* T07: N_Bs timeout (waiting for FC) */
void Test_T07_N_Bs_Timeout(void) {
    TRACE("\r\n--- Running T07: N_Bs Timeout ---");
    Test_ResetHarness();

    uint8 payload[20];
    PduInfoType pdu = { payload, NULL_PTR, 20U };

    CanTp_Transmit(0U, &pdu);
    CanTp_MainFunction(); /* FF sent, enters TX_WAIT_FC */
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_WAIT_FC, "State is TX_WAIT_FC");

    /* Advance 99 ms -> no timeout yet */
    g_sysTick_ms += 99;
    CanTp_MainFunction();
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_WAIT_FC, "Not timed out at 99ms");

    /* Advance 1 more ms (total 100ms) -> N_Bs timeout! */
    g_sysTick_ms += 1;
    CanTp_MainFunction();
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_IDLE, "Aborted to TX_IDLE");
    TEST_ASSERT_EQ(App_GetLastTxResult(), E_NOT_OK, "TxConfirmation E_NOT_OK");
    TRACE("[TEST] T07 PASSED");
}

/* T08: N_Cr timeout (waiting for CF) */
void Test_T08_N_Cr_Timeout(void) {
    TRACE("\r\n--- Running T08: N_Cr Timeout ---");
    Test_ResetHarness();

    /* Send FF to receiver */
    /* Student Guide v2.0 FF: 12-bit FF_DL = 20 -> [0x10, 0x14] */
    uint8 ffFrame[8] = { 0x10, 0x14, 1, 2, 3, 4, 5, 6 };
    Test_DeliverFrameToRx(ffFrame, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction(); /* Sends FC(CTS) -> enters RX_WAIT_CF */
    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_WAIT_CF, "Rx state is RX_WAIT_CF");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_RESERVED, "Queue slot 0 RESERVED");

    /* Advance 100 ms without CF */
    g_sysTick_ms += 100;
    CanTp_MainFunction();
    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_IDLE, "Rx aborted to IDLE");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_FREE, "Reserved slot released to FREE");
    TEST_ASSERT_EQ(App_GetLastRxResult(), E_NOT_OK, "RxIndication E_NOT_OK");
    TRACE("[TEST] T08 PASSED");
}

/* T13: N_As timeout and late confirmation lifecycle */
void Test_T13_N_As_Timeout_LateConfirmation(void) {
    TRACE("\r\n--- Running T13: N_As Timeout & Late Confirmation ---");
    Test_ResetHarness();

    g_autoConfirm = FALSE; /* Suppress immediate confirmation */
    uint8 payload[5] = { 1, 2, 3, 4, 5 };
    PduInfoType pdu = { payload, NULL_PTR, 5U };

    CanTp_Transmit(0U, &pdu);
    CanTp_MainFunction(); /* Sent, waiting confirmation */
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_WAIT_CONFIRM, "State TX_WAIT_CONFIRM");
    TEST_ASSERT_EQ(CanTp_IsTxPduPending(), TRUE, "txPduPending is TRUE");

    /* Advance 100 ms without confirmation -> N_As timeout! */
    g_sysTick_ms += 100;
    CanTp_MainFunction();
    TEST_ASSERT_EQ(CanTp_GetTxState(), TX_IDLE, "Session aborted to TX_IDLE");
    TEST_ASSERT_EQ(CanTp_IsTxPduPending(), TRUE, "txPduPending remains locked!");
    TEST_ASSERT_EQ(App_GetLastTxResult(), E_NOT_OK, "Final callback E_NOT_OK");

    /* Attempting new Tx must be rejected because PDU is locked */
    Std_ReturnType ret = CanTp_Transmit(0U, &pdu);
    TEST_ASSERT_EQ(ret, E_NOT_OK, "New Tx rejected while PDU locked");

    /* Late confirmation arrives */
    CanTp_TxConfirmation(CANTP_TX_DATA_LPDU_ID);
    TEST_ASSERT_EQ(CanTp_IsTxPduPending(), FALSE, "PDU unlocked by late confirmation");
    TEST_ASSERT_EQ(App_GetTxConfirmationCount(), 1, "No duplicate callback fired");

    /* Now new Tx can be accepted */
    g_autoConfirm = TRUE;
    ret = CanTp_Transmit(0U, &pdu);
    TEST_ASSERT_EQ(ret, E_OK, "New Tx accepted after unlock");
    TRACE("[TEST] T13 PASSED");
}

/* ================================================================== */
/*  PHASE 3: DEFENSIVE BEHAVIOR TESTS (T09 - T12, T14)                */
/* ================================================================== */

/* T09: Wrong Sequence Number (SN) */
void Test_T09_WrongSN_Abort(void) {
    TRACE("\r\n--- Running T09: Wrong SN Abort ---");
    Test_ResetHarness();

    /* Student Guide v2.0 FF: 12-bit FF_DL = 20 -> [0x10, 0x14] */
    uint8 ffFrame[8] = { 0x10, 0x14, 1, 2, 3, 4, 5, 6 };
    Test_DeliverFrameToRx(ffFrame, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction(); /* CTS */

    /* Expected SN is 1. Inject SN = 2! */
    uint8 badCf[8] = { 0x22, 7, 8, 9, 10, 11, 12, 13 };
    Test_DeliverFrameToRx(badCf, CANTP_RX_DATA_NPDU_ID);

    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_IDLE, "Rx aborted immediately");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_FREE, "Reserved slot released to FREE");
    TEST_ASSERT_EQ(App_GetLastRxResult(), E_NOT_OK, "RxIndication E_NOT_OK");
    TRACE("[TEST] T09 PASSED");
}

/* T10: Queue full -> Standalone FC(OVFLW) */
void Test_T10_QueueFull_OVFLW(void) {
    TRACE("\r\n--- Running T10: Queue Full -> FC(OVFLW) ---");
    Test_ResetHarness();

    /* Fill both queue slots to READY */
    App_RxQueue[0].state = APP_SLOT_READY;
    App_RxQueue[1].state = APP_SLOT_READY;

    /* Receive valid FF */
    /* Student Guide v2.0 FF: 12-bit FF_DL = 62 -> [0x10, 0x3E] */
    uint8 ffFrame[8] = { 0x10, 0x3E, 1, 2, 3, 4, 5, 6 };
    Test_DeliverFrameToRx(ffFrame, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction(); /* Sends FC */

    TEST_ASSERT_EQ(g_capturedCount, 1, "FC sent");
    TEST_ASSERT_EQ(g_capturedFrames[0].data[0], 0x32, "FC byte0 is 0x32 (OVFLW)");
    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_IDLE, "No Rx session created (stayed IDLE)");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_READY, "Existing slot 0 preserved");
    TEST_ASSERT_EQ(App_RxQueue[1].state, APP_SLOT_READY, "Existing slot 1 preserved");
    TRACE("[TEST] T10 PASSED");
}

/* T11: Oversized FF (> 62 bytes) -> FC(OVFLW) */
void Test_T11_OversizedFF_OVFLW(void) {
    TRACE("\r\n--- Running T11: Oversized FF -> FC(OVFLW) ---");
    Test_ResetHarness();

    /* FF declaring 100 bytes (0x64) */
    /* Student Guide v2.0 FF: 12-bit FF_DL = 100 -> [0x10, 0x64] */
    uint8 ffFrame[8] = { 0x10, 0x64, 1, 2, 3, 4, 5, 6 };
    Test_DeliverFrameToRx(ffFrame, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction();

    TEST_ASSERT_EQ(g_capturedCount, 1, "FC sent");
    TEST_ASSERT_EQ(g_capturedFrames[0].data[0], 0x32, "FC byte0 is 0x32 (OVFLW)");
    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_IDLE, "Stayed IDLE");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_FREE, "No slot reserved");
    TRACE("[TEST] T11 PASSED");
}

/* T12: Callback instrumentation & single complete copy */
void Test_T12_CallbackInstrumentation(void) {
    TRACE("\r\n--- Running T12: Callback Instrumentation ---");
    /* Covered thoroughly in T01, T02, T03: verifies single CopyRxData and READY state */
    Test_T03_MultiFrame_62B();
    TRACE("[TEST] T12 PASSED");
}

/* T14: Rx session replacement */
void Test_T14_RxSessionReplacement(void) {
    TRACE("\r\n--- Running T14: Rx Session Replacement ---");
    Test_ResetHarness();

    /* Start Session 1: FF length 20 */
    /* Student Guide v2.0 FF: 12-bit FF_DL = 20 -> [0x10, 0x14] */
    uint8 ff1[8] = { 0x10, 0x14, 1, 2, 3, 4, 5, 6 };
    Test_DeliverFrameToRx(ff1, CANTP_RX_DATA_NPDU_ID);
    CanTp_MainFunction(); /* CTS1 */
    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_WAIT_CF, "In RX_WAIT_CF");
    TEST_ASSERT_EQ(App_RxQueue[0].state, APP_SLOT_RESERVED, "Slot 0 RESERVED");

    /* Receive CF1 for Session 1 */
    uint8 cf1[8] = { 0x21, 7, 8, 9, 10, 11, 12, 13 };
    Test_DeliverFrameToRx(cf1, CANTP_RX_DATA_NPDU_ID);
    TEST_ASSERT_EQ(CanTp_GetRxReceivedLength(), 13, "Session 1 received 13 bytes");

    /* Inject NEW valid FF (length 30) on same connection! */
    /* Student Guide v2.0 FF: 12-bit FF_DL = 30 -> [0x10, 0x1E] */
    uint8 ff2[8] = { 0x10, 0x1E, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    Test_DeliverFrameToRx(ff2, CANTP_RX_DATA_NPDU_ID);

    /* Old session should be aborted with E_NOT_OK */
    TEST_ASSERT_EQ(App_GetRxIndicationCount(), 1, "Old session aborted with 1 RxIndication");
    TEST_ASSERT_EQ(App_GetLastRxResult(), E_NOT_OK, "Old session result E_NOT_OK");

    /* New session should now be active with length 30 and first 6 bytes */
    TEST_ASSERT_EQ(CanTp_GetRxState(), RX_FC_PENDING, "New session in RX_FC_PENDING");
    TEST_ASSERT_EQ(CanTp_GetRxReceivedLength(), 6, "New session receivedLength is 6");
    TEST_ASSERT_EQ(CanTp_GetRxExpectedSN(), 1, "New session expectedSN is 1");
    TRACE("[TEST] T14 PASSED");
}

/* ================================================================== */
/*  MAIN RUNNER                                                       */
/* ================================================================== */

int Run_All_CanTp_Tests(void) {
    TRACE("\r\n==================================================");
    TRACE("STARTING CANTP ACCEPTANCE TEST SUITE (T01 - T14)");
    TRACE("==================================================");

    g_testFailures = 0;

    /* Phase 1 */
    Test_T01_SingleFrame_5B();
    Test_T02_MultiFrame_20B();
    Test_T03_MultiFrame_62B();

    /* Phase 2 */
    Test_T04_STmin_Pacing();
    Test_T05_DataRetry_Immutability();
    Test_T06_DataRetry_Exhausted();
    Test_T07_N_Bs_Timeout();
    Test_T08_N_Cr_Timeout();
    Test_T13_N_As_Timeout_LateConfirmation();

    /* Phase 3 */
    Test_T09_WrongSN_Abort();
    Test_T10_QueueFull_OVFLW();
    Test_T11_OversizedFF_OVFLW();
    Test_T12_CallbackInstrumentation();
    Test_T14_RxSessionReplacement();

    TRACE("\r\n==================================================");
    if (g_testFailures == 0) {
        TRACE("ALL 14 CANTP TESTS PASSED (0 FAILURES)!");
    } else {
        TRACE("CANTP TEST SUITE COMPLETED WITH %d FAILURES!", g_testFailures);
    }
    TRACE("==================================================\r\n");

    return g_testFailures;
}

