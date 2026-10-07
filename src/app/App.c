#include "app/App.h"
#include "app/Role.h"
#include "app/UartRxQueue.h"
#include "com/Com.h"
#include "cantp/CanTp.h"
#include "candrv/Can.h"
#include "trace/Trace.h"
#include "Platform_Init.h"
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
#include "device_registers.h"
#else
#include <stdio.h>
#endif
#include <string.h>

#ifndef APP_VERBOSE_DEBUG
#define APP_VERBOSE_DEBUG 0
#endif

#if APP_VERBOSE_DEBUG
#define APP_DBG(...) TRACE(__VA_ARGS__)
#else
#define APP_DBG(...) ((void)0)
#endif

/* ================================================================== */
/*  TX SIGNAL DATA — one set per role                                   */
/* ================================================================== */

/* Role 0 (Slave 2) — Slave2Status TX (CAN ID 0x202) */
static uint8 slave2Status = 0U; /* 0=NORMAL, 1=MASTER_LOST */

/* Role 1 (Master) — KeepAlive TX (CAN ID 0x100) */
static uint8 aliveCounter = 0U;
static uint8 rateLevelVal = 0U;

/* Role 2 (Slave 1) — Slave1Status TX (CAN ID 0x201) */
static uint8 slave1Status = 0U; /* 0=NORMAL, 1=MASTER_LOST */

/* ================================================================== */
/*  RX SIGNAL DATA — inspect these in debugger / UART trace            */
/* ================================================================== */

volatile uint8 rxAliveCounter        = 0U;
volatile uint8 rxKeepAliveRateLevel  = 0U;
volatile uint8 rxSlave1Status        = 0U;
volatile uint8 rxSlave2Status        = 0U;

/* ================================================================== */
/*  CANTP APPLICATION QUEUE & STREAM DATA                              */
/* ================================================================== */

App_RxQueueSlotType App_RxQueue[APP_RX_QUEUE_SLOTS];

static uint32 g_appTxConfirmCount = 0U;
static uint32 g_appRxIndicateCount = 0U;
static Std_ReturnType g_appLastTxResult = E_NOT_OK;
static Std_ReturnType g_appLastRxResult = E_NOT_OK;

/*
 * Simulated DTC (Diagnostic Trouble Code) payload for CanTP demo.
 * 20 bytes — multi-frame transmission (FF + 2 CFs + 1 CTS).
 */
static const uint8 App_DtcPayload[20] = {
    0x07,                           /* 7 DTCs */
    0xC1, 0x00,                     /* DTC 0: P0100 — MAF sensor */
    0xC1, 0x13,                     /* DTC 1: P0113 — IAT sensor high */
    0xC1, 0x7E,                     /* DTC 2: P017E — fuel trim */
    0xC0, 0x30,                     /* DTC 3: P0030 — O2 sensor heater */
    0xC0, 0x71,                     /* DTC 4: P0071 — ambient temp sensor */
    0xC0, 0xA3,                     /* DTC 5: P00A3 — fuel system */
    0xC0, 0xB0,                     /* DTC 6: P00B0 — power supply */
    0x01,                           /* Status: confirmed, current */
    0x00, 0x00                      /* Reserved padding */
};

/* Pointer to active Tx source data */
static const uint8* g_appTxSourceData = App_DtcPayload;
static PduLengthType g_appTxSourceLen = sizeof(App_DtcPayload);

/* ================================================================== */
/*  IMAGE SENDER STATE (Role 1 — Master)                               */
/*  Assignment §5.3: Pop UART Rx Queue → CanTp chunks (≤62 bytes)      */
/*  Assignment §5.4: 1 initial + max 3 retries per chunk               */
/* ================================================================== */

static App_ImgTxStateType g_imgTxState = APP_IMG_TX_IDLE;
static uint8  g_imgTxChunk[CANTP_MAX_NSDU];   /* Current chunk buffer (≤62 bytes) */
static uint16 g_imgTxChunkLen = 0U;             /* Bytes in current chunk */
static uint8  g_imgTxRetryCount = 0U;           /* Retries for current chunk */
static uint32 g_imgTxChunksSent = 0U;           /* Total chunks sent for current image */
static uint32 g_imgTxChunksDropped = 0U;        /* Chunks dropped due to retry exhaustion */
static boolean g_imgTxConfirmPending = FALSE;   /* Waiting for TxConfirmation callback */
static Std_ReturnType g_imgTxLastResult = E_NOT_OK;

extern volatile uint32 g_sysTick_ms;
static uint32 g_lastImageRxTimeMs = 0U;

/* ================================================================== */
/*  IMAGE RECEIVER — RAW UART TX (Role 2 — Slave 1)                    */
/*  Assignment §6: Forward received N-SDU payload → UART Tx            */
/* ================================================================== */

/**
 * @brief Send raw bytes directly to LPUART1 TX (blocking).
 *        Used by Slave to output received image data to PC terminal.
 *        62 bytes @ 115200 baud ≈ 5.4ms — acceptable within 10ms task period.
 */
static void Uart_SendRawBytes(const uint8 *data, uint16 len) {
    static uint8 lastByte = 0U;
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    for (uint16 i = 0U; i < len; i++) {
        uint8 b = data[i];
        if (b == '\n' && lastByte != '\r') {
            while ((LPUART1->STAT & LPUART_STAT_TDRE_MASK) == 0U) {
                Can_MainFunction_Read();
                Can_MainFunction_Write();
            }
            LPUART1->DATA = '\r';
        }
        /* Wait for TX Data Register Empty while serving CAN driver to prevent packet drops */
        while ((LPUART1->STAT & LPUART_STAT_TDRE_MASK) == 0U) {
            Can_MainFunction_Read();
            Can_MainFunction_Write();
        }
        LPUART1->DATA = b;
        lastByte = b;
    }
#else
    for (uint16 i = 0U; i < len; i++) {
        uint8 b = data[i];
        if (b == '\n' && lastByte != '\r') {
            putchar('\r');
        }
        putchar(b);
        lastByte = b;
    }
#endif
}

/* ================================================================== */
/*  PDUR / CANTP APPLICATION INTERFACES                                */
/* ================================================================== */

BufReq_ReturnType App_CanTpCopyTxData(PduIdType txNSduId, uint8 *dst, PduLengthType length) {
    (void)txNSduId;
    if (dst == NULL_PTR || g_appTxSourceData == NULL_PTR || length > g_appTxSourceLen) {
        TRACE("[APP] CopyTxData BUFREQ_E_NOT_OK (len=%u srcLen=%u)", (uint32)length, (uint32)g_appTxSourceLen);
        return BUFREQ_E_NOT_OK;
    }
    memcpy(dst, g_appTxSourceData, length);
    APP_DBG("[APP] CopyTxData OK (%u bytes snapshotted)", (uint32)length);
    return BUFREQ_OK;
}

void App_CanTpTxConfirmation(PduIdType txNSduId, Std_ReturnType result) {
    (void)txNSduId;
    g_appTxConfirmCount++;
    g_appLastTxResult = result;

    /* Image Sender callback handling (Role 1) */
    if (g_imgTxConfirmPending) {
        g_imgTxConfirmPending = FALSE;
        g_imgTxLastResult = result;

        if (result == E_OK) {
            APP_DBG("[APP] ImageSender: chunk #%u TxConfirm SUCCESS (%u bytes)",
                  g_imgTxChunksSent + 1U, (uint32)g_imgTxChunkLen);
            g_imgTxChunksSent++;
            g_imgTxState = APP_IMG_TX_LOAD_CHUNK; /* Ready for next chunk */
        } else {
            TRACE("[APP] ImageSender: chunk TxConfirm FAILED (retry %u/%u)",
                  (uint32)g_imgTxRetryCount, (uint32)APP_IMG_TX_MAX_RETRIES);
            g_imgTxState = APP_IMG_TX_RETRY;      /* Will retry in next task cycle */
        }
        return;
    }

    /* Legacy DTC Tx handling */
    if (result == E_OK) {
        APP_DBG("[APP] CanTp TxConfirmation SUCCESS (#%u)", g_appTxConfirmCount);
    } else {
        TRACE("[APP] CanTp TxConfirmation FAILED (#%u)", g_appTxConfirmCount);
    }
}

BufReq_ReturnType App_CanTpStartOfReception(PduIdType rxNSduId, PduLengthType totalLength) {
    (void)rxNSduId;
    if (Role_Get() == ROLE_2_BODY_TX) {
        g_lastImageRxTimeMs = g_sysTick_ms;
    }
    if (totalLength > CANTP_MAX_NSDU) {
        TRACE("[APP] StartOfReception OVFLW (len=%u > max=%u)", (uint32)totalLength, CANTP_MAX_NSDU);
        return BUFREQ_E_OVFLW;
    }

    /* Find first FREE slot */
    for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
        if (App_RxQueue[i].state == APP_SLOT_FREE) {
            App_RxQueue[i].state  = APP_SLOT_RESERVED;
            App_RxQueue[i].length = totalLength;
            APP_DBG("[APP] Queue slot %u RESERVED for len=%u", (uint32)i, (uint32)totalLength);
            return BUFREQ_OK;
        }
    }

    TRACE("[APP] Queue FULL (no free slots) -> BUFREQ_E_OVFLW");
    return BUFREQ_E_OVFLW;
}

BufReq_ReturnType App_CanTpCopyRxData(PduIdType rxNSduId, const uint8 *completeData, PduLengthType length) {
    (void)rxNSduId;
    if (completeData == NULL_PTR) {
        return BUFREQ_E_NOT_OK;
    }

    /* Find the RESERVED slot */
    for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
        if (App_RxQueue[i].state == APP_SLOT_RESERVED) {
            uint16 copyLen = (length < sizeof(App_RxQueue[i].data)) ? length : sizeof(App_RxQueue[i].data);
            memcpy(App_RxQueue[i].data, completeData, copyLen);
            App_RxQueue[i].length = copyLen;
            App_RxQueue[i].state  = APP_SLOT_READY;
            APP_DBG("[APP] Queue slot %u -> READY (%u bytes)", (uint32)i, (uint32)copyLen);
            return BUFREQ_OK;
        }
    }

    TRACE("[APP] CopyRxData: no reserved slot found!");
    return BUFREQ_E_NOT_OK;
}

void App_CanTpRxIndication(PduIdType rxNSduId, Std_ReturnType result) {
    (void)rxNSduId;
    g_appRxIndicateCount++;
    g_appLastRxResult = result;

    if (result == E_OK) {
        APP_DBG("[APP] CanTp RxIndication SUCCESS (#%u)", g_appRxIndicateCount);
        if (Role_Get() == ROLE_2_BODY_TX) {
            g_lastImageRxTimeMs = g_sysTick_ms;
            /* Slots are marked APP_SLOT_READY by App_CanTpCopyRxData and drained by App_Task_1ms */
        }
    } else {
        TRACE("[APP] CanTp RxIndication FAILED (#%u) -> release reserved slot", g_appRxIndicateCount);
        /* If a slot was reserved, release it back to FREE */
        for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
            if (App_RxQueue[i].state == APP_SLOT_RESERVED) {
                App_RxQueue[i].state  = APP_SLOT_FREE;
                App_RxQueue[i].length = 0U;
                TRACE("[APP] Queue slot %u released -> FREE", (uint32)i);
                break;
            }
        }
    }
}

uint32 App_GetTxConfirmationCount(void)  { return g_appTxConfirmCount; }
uint32 App_GetRxIndicationCount(void)    { return g_appRxIndicateCount; }
Std_ReturnType App_GetLastTxResult(void) { return g_appLastTxResult; }
Std_ReturnType App_GetLastRxResult(void) { return g_appLastRxResult; }

void App_ResetCanTpCounters(void) {
    g_appTxConfirmCount  = 0U;
    g_appRxIndicateCount = 0U;
    g_appLastTxResult    = E_NOT_OK;
    g_appLastRxResult    = E_NOT_OK;
    for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
        App_RxQueue[i].state  = APP_SLOT_FREE;
        App_RxQueue[i].length = 0U;
        memset(App_RxQueue[i].data, 0, sizeof(App_RxQueue[i].data));
    }
}

void App_SetTxSourceData(const uint8 *data, PduLengthType length) {
    g_appTxSourceData = data;
    g_appTxSourceLen  = length;
}

boolean App_IsImageStreamingActive(void) {
    if (CanTp_GetTxState() != TX_IDLE || CanTp_GetRxState() != RX_IDLE ||
        CanTp_IsTxPduPending() || CanTp_IsFcTxPending()) {
        return TRUE;
    }

    RoleType role = Role_Get();
    if (role == ROLE_1_ENGINE_TX) {
        if (UartRxQueue_ImageActive() || g_imgTxState != APP_IMG_TX_IDLE) {
            return TRUE;
        }
    } else if (role == ROLE_2_BODY_TX) {
        if (g_lastImageRxTimeMs > 0U && (uint32)(g_sysTick_ms - g_lastImageRxTimeMs) < 3000U) {
            return TRUE;
        }
    }
    return FALSE;
}

/* ================================================================== */
/*  IMAGE SENDER — INTERNAL HELPERS (Role 1)                           */
/* ================================================================== */

/**
 * @brief Attempt to submit the current chunk to CanTp.
 *        Sets up the Tx source data pointer and calls CanTp_Transmit().
 * @return E_OK if CanTp accepted, E_NOT_OK if rejected (busy/error).
 */
static Std_ReturnType App_ImgTx_SubmitChunk(void) {
    /* Point the CopyTxData callback at our image chunk buffer */
    g_appTxSourceData = g_imgTxChunk;
    g_appTxSourceLen  = g_imgTxChunkLen;

    PduInfoType pdu;
    pdu.SduDataPtr  = g_imgTxChunk;
    pdu.SduLength   = g_imgTxChunkLen;
    pdu.MetaDataPtr = NULL_PTR;

    Std_ReturnType ret = CanTp_Transmit(0U, &pdu);
    if (ret == E_OK) {
        g_imgTxConfirmPending = TRUE;
        g_imgTxState = APP_IMG_TX_WAIT_CANTP;
        APP_DBG("[APP] ImageSender: submitted chunk %u bytes (attempt %u/%u)",
              (uint32)g_imgTxChunkLen, (uint32)(g_imgTxRetryCount + 1U),
              (uint32)(APP_IMG_TX_MAX_RETRIES + 1U));
    }
    return ret;
}

/* ================================================================== */
/*  TASK 3 — SLAVE STATUS MONITORING (Role 1 — Master)                */
/*  Assignment §4: Master monitors Slave 1 & Slave 2 via COM Rx       */
/*  Timeout: 2000ms -> Slave marked OFFLINE                            */
/*  Output: "[MASTER] Slave X ONLINE", "[MASTER] Online Slaves: N/2"  */
/* ================================================================== */

#define APP_SLAVE_OFFLINE_TIMEOUT_MS  2000U
#define APP_NUM_SLAVES                2U

typedef struct {
    boolean online;
    uint8   status;            /* 0 = NORMAL, 1 = MASTER_LOST */
    uint32  lastSeenTick;
    boolean wasOnline;
    uint8   lastReportedStatus;
} App_SlaveMonitorType;

static App_SlaveMonitorType g_slaves[APP_NUM_SLAVES];
static uint8 g_lastReportedOnlineCount = 0xFFU;
static uint32 g_lastOnlineSummaryLogMs = 0U;
static boolean g_masterBootReported    = FALSE;

static void App_PrintOnlineSummary(void) {
    uint8 count = 0U;
    for (uint8 i = 0U; i < APP_NUM_SLAVES; i++) {
        if (g_slaves[i].online) {
            count++;
        }
    }
    g_lastReportedOnlineCount = count;
    g_lastOnlineSummaryLogMs  = g_sysTick_ms;
    TRACE("[MASTER] Online Slaves: %u/2", (uint32)count);
}

static void App_OnSlaveMessageReceived(uint8 slaveIdx, uint8 status) {
    if (slaveIdx >= APP_NUM_SLAVES) return;

    g_slaves[slaveIdx].lastSeenTick = g_sysTick_ms;
    g_slaves[slaveIdx].status       = status;

    if (!g_slaves[slaveIdx].online) {
        g_slaves[slaveIdx].online    = TRUE;
        g_slaves[slaveIdx].wasOnline = TRUE;
        g_slaves[slaveIdx].lastReportedStatus = status;

        if (!App_IsImageStreamingActive()) {
            TRACE("[MASTER] Slave %u ONLINE", (uint32)(slaveIdx + 1U));
            App_PrintOnlineSummary();
        }
    } else if (status != g_slaves[slaveIdx].lastReportedStatus) {
        g_slaves[slaveIdx].lastReportedStatus = status;
        if (!App_IsImageStreamingActive()) {
            TRACE("[MASTER] Slave %u Status: %s",
                  (uint32)(slaveIdx + 1U),
                  (status == 0U) ? "NORMAL" : "MASTER_LOST");
        }
    }
}

void App_ComRxCallback(PduIdType RxPduId) {
    if (Role_Get() != ROLE_1_ENGINE_TX) {
        return;
    }

    if (RxPduId == 4U) {
        /* Slave 1 Status (CAN ID 0x201, RxPduId 4 in ComIPdu) -> Slave 1 */
        uint8 st = 0U;
        Com_ReceiveSignal(6, &st); /* RxSlave1Status: 0=NORMAL, 1=MASTER_LOST */
        App_OnSlaveMessageReceived(0U, st);
    } else if (RxPduId == 5U) {
        /* Slave 2 Status (CAN ID 0x202, RxPduId 5 in ComIPdu) -> Slave 2 */
        uint8 st = 0U;
        Com_ReceiveSignal(7, &st); /* RxSlave2Status: 0=NORMAL, 1=MASTER_LOST */
        App_OnSlaveMessageReceived(1U, st);
    }
}

uint8 App_GetOnlineSlavesCount(void) {
    uint8 count = 0U;
    for (uint8 i = 0U; i < APP_NUM_SLAVES; i++) {
        if (g_slaves[i].online) {
            count++;
        }
    }
    return count;
}

boolean App_IsSlaveOnline(uint8 slaveNum) {
    if (slaveNum >= 1U && slaveNum <= APP_NUM_SLAVES) {
        return g_slaves[slaveNum - 1U].online;
    }
    return FALSE;
}

uint8 App_GetSlaveStatus(uint8 slaveNum) {
    if (slaveNum >= 1U && slaveNum <= APP_NUM_SLAVES) {
        return g_slaves[slaveNum - 1U].status;
    }
    return 1U; /* default MASTER_LOST */
}

/* Network Monitor Task (§4: periodic 10ms timeout check) */
static void App_NetworkMonitor_Task(void) {
    boolean stateChanged = FALSE;

    for (uint8 i = 0U; i < APP_NUM_SLAVES; i++) {
        if (g_slaves[i].online) {
            if ((uint32)(g_sysTick_ms - g_slaves[i].lastSeenTick) > APP_SLAVE_OFFLINE_TIMEOUT_MS) {
                g_slaves[i].online    = FALSE;
                g_slaves[i].wasOnline = FALSE;
                stateChanged = TRUE;

                if (!App_IsImageStreamingActive()) {
                    TRACE("[MASTER] Slave %u OFFLINE", (uint32)(i + 1U));
                }
            }
        }
    }

    if (stateChanged) {
        if (!App_IsImageStreamingActive()) {
            App_PrintOnlineSummary();
        }
    } else {
        /* Initial boot check or periodic heartbeat when idle */
        if (!g_masterBootReported && g_sysTick_ms >= 2000U) {
            g_masterBootReported = TRUE;
            if (!App_IsImageStreamingActive()) {
                App_PrintOnlineSummary();
            }
        } else if (!App_IsImageStreamingActive() &&
                   g_masterBootReported &&
                   (uint32)(g_sysTick_ms - g_lastOnlineSummaryLogMs) >= 5000U) {
            App_PrintOnlineSummary();
        }
    }
}

/* ================================================================== */
/*  PUBLIC: App_Init()                                                  */
/* ================================================================== */

void App_Init(void) {
    RoleType role = Role_Get();
    TRACE("[APP] Init — %s", Role_GetName());

    App_ResetCanTpCounters();

    for (uint8 i = 0U; i < APP_NUM_SLAVES; i++) {
        g_slaves[i].online = FALSE;
        g_slaves[i].status = 0U;
        g_slaves[i].lastSeenTick = 0U;
        g_slaves[i].wasOnline = FALSE;
        g_slaves[i].lastReportedStatus = 0xFFU;
    }
    g_lastReportedOnlineCount = 0xFFU;
    g_lastOnlineSummaryLogMs  = 0U;
    g_masterBootReported      = FALSE;

    if (role == ROLE_1_ENGINE_TX) {
        /* Image Sender initialization */
        g_imgTxState = APP_IMG_TX_IDLE;
        g_imgTxChunkLen = 0U;
        g_imgTxRetryCount = 0U;
        g_imgTxChunksSent = 0U;
        g_imgTxChunksDropped = 0U;
        g_imgTxConfirmPending = FALSE;
        TRACE("[APP] Role1: Image Sender ready (UART->CanTp, max_chunk=%u, retry=%u)",
              (uint32)CANTP_MAX_NSDU, (uint32)APP_IMG_TX_MAX_RETRIES);
    } else if (role == ROLE_2_BODY_TX) {
        TRACE("[APP] Role2: Image Receiver ready (CanTp->UART)");
    }

    Com_SetRxCallback(App_ComRxCallback);

    Platform_LedSet(0U, 0U, 0U);

    if (Com_ValidateConfig() == E_OK) {
        TRACE("[APP] Com_ValidateConfig() -> E_OK");
    } else {
        TRACE("[APP] Com_ValidateConfig() -> E_NOT_OK *** CONFIG ERROR ***");
    }
}

/* ================================================================== */
/*  PUBLIC: App_Task_1ms()                                              */
/*  Handles KeepAlive Potentiometer sampling (Master) and              */
/*  LED blinking rate modulation / timeout detection (Slave).          */
/*  Assignment §2: KeepAlive Mechanism, §3: Slave KeepAlive Reception  */
/* ================================================================== */

void App_Task_1ms(void) {
    RoleType role = Role_Get();

    /* ============================================================== */
    /*  ROLE 1: MASTER — Potentiometer ADC -> RateLevel -> KeepAlive   */
    /* ============================================================== */
    if (role == ROLE_1_ENGINE_TX) {
        /* Rate Level to App KeepAlive Period (§2.4):
         * Level 0: 500 ms, Level 1: 200 ms, Level 2: 100 ms,
         * Level 3: 50 ms,  Level 4: 20 ms,  Level 5: 10 ms, Level 6: 5 ms
         */
        static const uint16 kAppKeepAlivePeriodMs[7] = {
            500U, 200U, 100U, 50U, 20U, 10U, 5U
        };

        /* Sample potentiometer ADC0_SE12 (PTC14) every 10ms with 4-tap smoothing */
        static uint16 s_adcVal = 0U;
        static uint32 s_lastAdcSampleMs = 0U;
        if ((uint32)(g_sysTick_ms - s_lastAdcSampleMs) >= 10U) {
            s_lastAdcSampleMs = g_sysTick_ms;
            uint16 rawSample = Platform_AdcReadPot();
            if (s_adcVal == 0U) {
                s_adcVal = rawSample;
            } else {
                s_adcVal = (uint16)(((uint32)s_adcVal * 3U + rawSample) / 4U);
            }
        }

        /* Map 12-bit ADC (0..4095) into 7 discrete Rate Levels (0..6) per §2.4 */
        uint8 rateLevel = (uint8)(s_adcVal / 586U);
        if (rateLevel > 6U) {
            rateLevel = 6U;
        }

        static uint8 s_lastMasterReportedLevel = 0xFFU;
        if (rateLevel != s_lastMasterReportedLevel) {
            s_lastMasterReportedLevel = rateLevel;
            if (!App_IsImageStreamingActive()) {
                TRACE("[MASTER] Potentiometer ADC=%u -> RateLevel=%u (App Period=%ums)",
                      (uint32)s_adcVal, (uint32)rateLevel, (uint32)kAppKeepAlivePeriodMs[rateLevel]);
            }
        }

        /* Rate Level to LED Toggle Half-Period (§3.1):
         * Level 0: 1500 ms full cycle -> 750 ms half-period
         * Level 1: 1000 ms full cycle -> 500 ms half-period
         * Level 2:  800 ms full cycle -> 400 ms half-period
         * Level 3:  600 ms full cycle -> 300 ms half-period
         * Level 4:  400 ms full cycle -> 200 ms half-period
         * Level 5:  300 ms full cycle -> 150 ms half-period
         * Level 6:  200 ms full cycle -> 100 ms half-period
         */
        static const uint16 kLedToggleHalfPeriodMs[7] = {
            750U, 500U, 400U, 300U, 200U, 150U, 100U
        };

        /* Periodic KeepAlive countdown (§2.4) */
        static uint32 s_lastKeepAliveTxMs = 0U;
        static uint8  s_aliveCounter = 0U;

        if ((uint32)(g_sysTick_ms - s_lastKeepAliveTxMs) >= kAppKeepAlivePeriodMs[rateLevel]) {
            s_lastKeepAliveTxMs = g_sysTick_ms;
            /* Mask to 7-bit (0..127) because 8-bit COM slot has 1 update bit */
            s_aliveCounter = (uint8)((s_aliveCounter + 1U) & 0x7FU);

            aliveCounter = s_aliveCounter;
            rateLevelVal = rateLevel;

            Com_SendSignal(0, &aliveCounter);
            Com_SendSignal(1, &rateLevelVal);
        }

        /* Visual indicator on Master: toggle Blue LED at human-visible Rate Level period (§3.1) */
        static uint32 s_masterLastLedToggleMs = 0U;
        if ((uint32)(g_sysTick_ms - s_masterLastLedToggleMs) >= kLedToggleHalfPeriodMs[rateLevel]) {
            s_masterLastLedToggleMs = g_sysTick_ms;
            Platform_LedToggleBlue();
        }
    }
    /* ============================================================== */
    /*  ROLE 2: SLAVE 1 — KeepAlive Reception & LED Modulation        */
    /* ============================================================== */
    else if (role == ROLE_2_BODY_TX) {
        static const uint16 kLedToggleHalfPeriodMs[7] = {
            750U, 500U, 400U, 300U, 200U, 150U, 100U
        };

        static uint8  s_slaveLastAliveCounter = 0xFFU;
        static uint32 s_slaveLastAliveTimeMs  = 0U;
        static uint32 s_slaveLastLedToggleMs  = 0U;
        static uint8  s_slaveState            = 0U; /* 0=NORMAL, 1=MASTER_LOST */
        static uint8  s_slaveLastReportedLvl  = 0xFFU;

        /* Read incoming signals from KeepAliveRx (PDU 0x0100, IPDU 3 from Master) */
        Com_ReceiveSignal(4, (void*)&rxAliveCounter);
        Com_ReceiveSignal(5, (void*)&rxKeepAliveRateLevel);

        /* Detect new KeepAlive (§3): newAliveCounter != lastAliveCounter */
        if (rxAliveCounter != s_slaveLastAliveCounter) {
            s_slaveLastAliveCounter = rxAliveCounter;
            s_slaveLastAliveTimeMs  = g_sysTick_ms;

            if (s_slaveState != 0U) {
                s_slaveState = 0U;
                Platform_LedSet(0U, 0U, 0U); /* Turn off Red error LED immediately */
                if (!App_IsImageStreamingActive()) {
                    TRACE("[SLAVE 1] KeepAlive Status: NORMAL (recovered)");
                }
            }
        }

        /* Prevent false timeout while actively receiving image stream over CanTp */
        if (App_IsImageStreamingActive()) {
            s_slaveLastAliveTimeMs = g_sysTick_ms;
        }

        /* Initial grace period on boot */
        if (s_slaveLastAliveTimeMs == 0U) {
            s_slaveLastAliveTimeMs = g_sysTick_ms;
        }

        /* Timeout Check (§3 & §4): 2000 ms without new AliveCounter -> MASTER_LOST */
        if ((uint32)(g_sysTick_ms - s_slaveLastAliveTimeMs) > 2000U) {
            if (s_slaveState != 1U) {
                s_slaveState = 1U;
                if (!App_IsImageStreamingActive()) {
                    TRACE("[SLAVE 1] KeepAlive Status: MASTER_LOST (timeout > 2000ms)");
                }
            }
            slave1Status = 1U; /* Signal 2: Slave1Status = MASTER_LOST (§4) */
            /* In MASTER_LOST state: Red LED ON solid, Green OFF, Blue OFF */
            Platform_LedSet(1U, 0U, 0U);
        } else {
            /* NORMAL State: ensure Red is OFF, modulate Green LED blinking */
            slave1Status = 0U; /* Signal 2: Slave1Status = NORMAL (§4) */

            uint8 level = rxKeepAliveRateLevel;
            if (level > 6U) {
                level = 6U;
            }

            if (level != s_slaveLastReportedLvl) {
                s_slaveLastReportedLvl = level;
                if (!App_IsImageStreamingActive()) {
                    TRACE("[SLAVE 1] KeepAlive RateLevel: %u (LED full cycle: %ums)",
                          (uint32)level, (uint32)kLedToggleHalfPeriodMs[level] * 2U);
                }
            }

            /* Toggle Green LED at the configured half-period */
            if ((uint32)(g_sysTick_ms - s_slaveLastLedToggleMs) >= kLedToggleHalfPeriodMs[level]) {
                s_slaveLastLedToggleMs = g_sysTick_ms;
                Platform_LedToggleGreen();
            }
        }
    }
    /* ============================================================== */
    /*  ROLE 0: SLAVE 2 — KeepAlive Reception & LED Modulation        */
    /* ============================================================== */
    else if (role == ROLE_0_VEHICLE_TX) {
        static const uint16 kLedToggleHalfPeriodMs[7] = {
            750U, 500U, 400U, 300U, 200U, 150U, 100U
        };

        static uint8  s_slave2LastAliveCounter = 0xFFU;
        static uint32 s_slave2LastAliveTimeMs  = 0U;
        static uint32 s_slave2LastLedToggleMs  = 0U;
        static uint8  s_slave2State            = 0U; /* 0=NORMAL, 1=MASTER_LOST */
        static uint8  s_slave2LastReportedLvl  = 0xFFU;

        /* Read incoming signals from KeepAliveRx (PDU 0x0100, IPDU 3 from Master) */
        Com_ReceiveSignal(4, (void*)&rxAliveCounter);
        Com_ReceiveSignal(5, (void*)&rxKeepAliveRateLevel);

        /* Detect new KeepAlive (§3): newAliveCounter != lastAliveCounter */
        if (rxAliveCounter != s_slave2LastAliveCounter) {
            s_slave2LastAliveCounter = rxAliveCounter;
            s_slave2LastAliveTimeMs  = g_sysTick_ms;

            if (s_slave2State != 0U) {
                s_slave2State = 0U;
                Platform_LedSet(0U, 0U, 0U); /* Turn off Red error LED immediately */
                if (!App_IsImageStreamingActive()) {
                    TRACE("[SLAVE 2] KeepAlive Status: NORMAL (recovered)");
                }
            }
        }

        /* Initial grace period on boot */
        if (s_slave2LastAliveTimeMs == 0U) {
            s_slave2LastAliveTimeMs = g_sysTick_ms;
        }

        /* Timeout Check (§3 & §4): 2000 ms without new AliveCounter -> MASTER_LOST */
        if ((uint32)(g_sysTick_ms - s_slave2LastAliveTimeMs) > 2000U) {
            if (s_slave2State != 1U) {
                s_slave2State = 1U;
                if (!App_IsImageStreamingActive()) {
                    TRACE("[SLAVE 2] KeepAlive Status: MASTER_LOST (timeout > 2000ms)");
                }
            }
            slave2Status = 1U; /* Signal 3: Slave2Status = MASTER_LOST (§4) */
            /* In MASTER_LOST state: Red LED ON solid, Green OFF, Blue OFF */
            Platform_LedSet(1U, 0U, 0U);
        } else {
            /* NORMAL State: ensure Red is OFF, modulate Green LED blinking */
            slave2Status = 0U; /* Signal 3: Slave2Status = NORMAL (§4) */

            uint8 level = rxKeepAliveRateLevel;
            if (level > 6U) {
                level = 6U;
            }

            if (level != s_slave2LastReportedLvl) {
                s_slave2LastReportedLvl = level;
                if (!App_IsImageStreamingActive()) {
                    TRACE("[SLAVE 2] KeepAlive RateLevel: %u (LED full cycle: %ums)",
                          (uint32)level, (uint32)kLedToggleHalfPeriodMs[level] * 2U);
                }
            }

            /* Toggle Green LED at the configured half-period */
            if ((uint32)(g_sysTick_ms - s_slave2LastLedToggleMs) >= kLedToggleHalfPeriodMs[level]) {
                s_slave2LastLedToggleMs = g_sysTick_ms;
                Platform_LedToggleGreen();
            }
        }
    }

    /* Drain any completed image N-SDU slots toward UART on Slave 1 */
    if (role == ROLE_2_BODY_TX) {
        for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
            if (App_RxQueue[i].state == APP_SLOT_READY) {
                if (App_RxQueue[i].length > 0U) {
                    Uart_SendRawBytes(App_RxQueue[i].data, App_RxQueue[i].length);
                }
                App_RxQueue[i].state  = APP_SLOT_FREE;
                App_RxQueue[i].length = 0U;
            }
        }
    }
}

/* ================================================================== */
/*  PUBLIC: App_Task_10ms()                                             */
/* ================================================================== */

void App_Task_10ms(void) {
    RoleType role = Role_Get();

    /* ---- TX Signals (role-specific) ---- */
    switch (role) {
    case ROLE_0_VEHICLE_TX:
        Com_SendSignal(3, &slave2Status); /* Signal 3: Slave2Status (CAN ID 0x202) */
        break;

    case ROLE_1_ENGINE_TX:
        /* KeepAlive TX is handled in App_Task_1ms() with ADC potentiometer */
        /* Task 3: Network Monitor timeout check */
        App_NetworkMonitor_Task();
        break;

    case ROLE_2_BODY_TX:
        Com_SendSignal(2, &slave1Status); /* Signal 2: Slave1Status (CAN ID 0x201) */
        break;

    default:
        break;
    }

    /* ---- RX Signals (role-specific read) ---- */
    switch (role) {
    case ROLE_0_VEHICLE_TX:
        Com_ReceiveSignal(4, (void*)&rxAliveCounter);
        Com_ReceiveSignal(5, (void*)&rxKeepAliveRateLevel);
        break;

    case ROLE_1_ENGINE_TX:
        Com_ReceiveSignal(6, (void*)&rxSlave1Status);
        Com_ReceiveSignal(7, (void*)&rxSlave2Status);
        break;

    case ROLE_2_BODY_TX:
        Com_ReceiveSignal(4, (void*)&rxAliveCounter);
        Com_ReceiveSignal(5, (void*)&rxKeepAliveRateLevel);
        break;

    default:
        break;
    }

    /* ================================================================== */
    /*  IMAGE SENDER — Role 1 (Master) State Machine                      */
    /*  Assignment §5.3: Pop ≤62 bytes from UART Rx Queue → CanTp         */
    /*  Assignment §5.4: 1 initial attempt + max 3 retries per chunk      */
    /* ================================================================== */
    if (role == ROLE_1_ENGINE_TX) {
        switch (g_imgTxState) {
        case APP_IMG_TX_IDLE:
            /* Check if UART Rx Queue has image data available */
            if (UartRxQueue_ImageActive() && UartRxQueue_Available() > 0U) {
                g_imgTxState = APP_IMG_TX_LOAD_CHUNK;
            }
            /* Also check if a completed image can be finalized */
            if (UartRxQueue_ImageComplete()) {
                TRACE("[APP] ImageSender: image transfer complete (%u chunks sent, %u dropped)",
                      g_imgTxChunksSent, g_imgTxChunksDropped);
                g_imgTxChunksSent = 0U;
                g_imgTxChunksDropped = 0U;
            }
            break;

        case APP_IMG_TX_LOAD_CHUNK:
            /* Pop next chunk of up to CANTP_MAX_NSDU (62) bytes */
            if (UartRxQueue_Available() > 0U) {
                /* Wait until CanTp is completely idle before popping next chunk */
                if (CanTp_GetTxState() != TX_IDLE || CanTp_IsTxPduPending()) {
                    break;
                }

                g_imgTxChunkLen = UartRxQueue_Pop(g_imgTxChunk, CANTP_MAX_NSDU);
                g_imgTxRetryCount = 0U;

                if (g_imgTxChunkLen > 0U) {
                    /* Attempt first Tx */
                    if (App_ImgTx_SubmitChunk() != E_OK) {
                        TRACE("[APP] ImageSender: CanTp_Transmit rejected (busy), will retry");
                        g_imgTxState = APP_IMG_TX_RETRY;
                    }
                } else {
                    /* No data available despite queue check — stay in LOAD */
                    g_imgTxState = APP_IMG_TX_IDLE;
                }
            } else {
                /* No more data in queue — go idle or check completion */
                g_imgTxState = APP_IMG_TX_IDLE;
            }
            break;

        case APP_IMG_TX_WAIT_CANTP:
            /* Waiting for App_CanTpTxConfirmation callback.
             * State transition happens in the callback handler above. */
            break;

        case APP_IMG_TX_RETRY:
            /* Retry the current chunk (same data, same length) */
            if (CanTp_GetTxState() == TX_IDLE && !CanTp_IsTxPduPending()) {
                if (g_imgTxRetryCount < APP_IMG_TX_MAX_RETRIES) {
                    g_imgTxRetryCount++;
                    TRACE("[APP] ImageSender: retrying chunk (%u/%u)",
                          (uint32)g_imgTxRetryCount, (uint32)APP_IMG_TX_MAX_RETRIES);
                    if (App_ImgTx_SubmitChunk() != E_OK) {
                        /* Still rejected — will retry again next cycle */
                        TRACE("[APP] ImageSender: retry %u rejected, will try again",
                              (uint32)g_imgTxRetryCount);
                    }
                } else {
                    /* Assignment §5.4: "Drop current chunk, process next chunk" */
                    g_imgTxChunksDropped++;
                    TRACE("[APP] ImageSender: DROPPING chunk after %u retries (total dropped=%u)",
                          (uint32)APP_IMG_TX_MAX_RETRIES, g_imgTxChunksDropped);
                    g_imgTxState = APP_IMG_TX_LOAD_CHUNK; /* Move to next chunk */
                }
            }
            break;

        default:
            g_imgTxState = APP_IMG_TX_IDLE;
            break;
        }
    }

    /* ================================================================== */
    /*  IMAGE RECEIVER — Role 2 (Slave 1)                                  */
    /*  Assignment §6: Forward received N-SDU payload → UART Tx → PC      */
    /*  "receive the N-SDU, extract image payload, push toward UART Tx"    */
    /* ================================================================== */
    if (role == ROLE_2_BODY_TX) {
        for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
            if (App_RxQueue[i].state == APP_SLOT_READY) {
                APP_DBG("[APP] ImageReceiver: forwarding slot %u (%u bytes) -> UART Tx",
                      (uint32)i, (uint32)App_RxQueue[i].length);

                /* Forward raw image payload bytes to UART for PC terminal display */
                if (App_RxQueue[i].length > 0U) {
                    Uart_SendRawBytes(App_RxQueue[i].data, App_RxQueue[i].length);
                }

                /* Consume slot */
                App_RxQueue[i].state  = APP_SLOT_FREE;
                App_RxQueue[i].length = 0U;
            }
        }
    }
}
