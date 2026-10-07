#include "cantp/CanTp.h"
#include "pdur/PduR.h"
#include "trace/Trace.h"
#include "Platform_Init.h"
#include <string.h>

#ifndef CANTP_VERBOSE_DEBUG
#define CANTP_VERBOSE_DEBUG 0
#endif

#if CANTP_VERBOSE_DEBUG
#define CANTP_DBG(...) TRACE(__VA_ARGS__)
#else
#define CANTP_DBG(...) ((void)0)
#endif

/* ================================================================== */
/*  STATIC STATE DEFINITIONS                                          */
/* ================================================================== */

typedef struct {
    CanTp_TxStateType      state;
    uint8                  txChunkBuffer[CANTP_CHUNK_CAPACITY]; /* 64-byte snapshot */
    uint8                  txDataFrame[CANTP_FRAME_LENGTH];     /* 8-byte frame buffer */
    PduLengthType          totalLength;
    PduLengthType          txOffset;            /* Confirmed transmitted bytes */
    uint8                  nextSN;              /* 4-bit CF SN (starts 1, wraps 0..15) */
    uint8                  blockCount;          /* CFs confirmed in current block */
    uint8                  retryCount;          /* Attempts rejected by CanIf (max 3 retries) */
    boolean                txPduPending;        /* CanIf accepted Data, awaiting confirmation */
    boolean                resultReported;      /* Guard against duplicate final callback */
    boolean                fcPermissionGranted; /* Valid CTS received */
    boolean                priorCfExists;       /* At least one CF confirmed */
    uint32                 lastCfConfirmedMs;   /* Timestamp of last confirmed CF for STmin */
    uint32                 dataAttemptDueMs;    /* Tick when next retry attempt is due */
    uint32                 txTimerStartMs;      /* Start time for N_As or N_Bs */
    uint8                  preparedPayloadBytes;/* Committed only upon confirmation */
    CanTp_FrameKindType    preparedFrameType;   /* SF, FF, or CF */
    PduIdType              txNSduId;
} CanTp_TxRuntimeType;

typedef struct {
    CanTp_RxStateType      state;
    uint8                  rxChunkBuffer[CANTP_CHUNK_CAPACITY]; /* 64-byte reassembly buffer */
    uint8                  txFcFrame[CANTP_FRAME_LENGTH];       /* 8-byte FC frame */
    PduLengthType          totalLength;
    PduLengthType          receivedLength;
    uint8                  expectedSN;          /* Expected SN for next CF */
    uint8                  blockCount;          /* CFs received in current block */
    uint8                  fcRetryCount;        /* FC retries */
    boolean                queueSlotReserved;   /* True if App queue slot is reserved */
    boolean                fcRequestActive;     /* True while FC is queued/transmitting */
    boolean                fcTxPending;         /* CanIf accepted FC, awaiting confirmation */
    boolean                resultReported;      /* Guard against duplicate final callback */
    boolean                isStandaloneOvflw;   /* True if sending standalone FC(OVFLW) */
    uint32                 fcAttemptDueMs;      /* Tick when next FC retry is due */
    uint32                 fcTimerStartMs;      /* Start time for N_Ar */
    uint32                 rxCrTimerStartMs;    /* Start time for N_Cr */
    PduIdType              rxNSduId;
} CanTp_RxRuntimeType;

static CanTp_TxRuntimeType CanTp_Tx;
static CanTp_RxRuntimeType CanTp_Rx;

/* Monotonic time helper */
extern volatile uint32 g_sysTick_ms;
static inline uint32 CanTp_GetTimeMs(void) {
    return g_sysTick_ms;
}

/* ================================================================== */
/*  HELPER / RESET FUNCTIONS                                          */
/* ================================================================== */

static void CanTp_TxReset(void) {
    CanTp_Tx.state               = TX_IDLE;
    CanTp_Tx.totalLength         = 0U;
    CanTp_Tx.txOffset            = 0U;
    CanTp_Tx.nextSN              = 1U;
    CanTp_Tx.blockCount          = 0U;
    CanTp_Tx.retryCount          = 0U;
    /* NOTE: txPduPending is NOT cleared if outstanding in hardware */
    CanTp_Tx.resultReported      = FALSE;
    CanTp_Tx.fcPermissionGranted = FALSE;
    CanTp_Tx.priorCfExists       = FALSE;
    CanTp_Tx.lastCfConfirmedMs   = 0U;
    CanTp_Tx.dataAttemptDueMs    = 0U;
    CanTp_Tx.txTimerStartMs      = 0U;
    CanTp_Tx.preparedPayloadBytes = 0U;
    memset(CanTp_Tx.txChunkBuffer, 0, sizeof(CanTp_Tx.txChunkBuffer));
    memset(CanTp_Tx.txDataFrame, 0, sizeof(CanTp_Tx.txDataFrame));
}

static void CanTp_RxReset(void) {
    CanTp_Rx.state             = RX_IDLE;
    CanTp_Rx.totalLength       = 0U;
    CanTp_Rx.receivedLength    = 0U;
    CanTp_Rx.expectedSN        = 1U;
    CanTp_Rx.blockCount        = 0U;
    CanTp_Rx.fcRetryCount      = 0U;
    CanTp_Rx.queueSlotReserved = FALSE;
    /* NOTE: fcTxPending is NOT cleared if outstanding in hardware */
    CanTp_Rx.resultReported    = FALSE;
    CanTp_Rx.isStandaloneOvflw = FALSE;
    CanTp_Rx.fcAttemptDueMs    = 0U;
    CanTp_Rx.fcTimerStartMs    = 0U;
    CanTp_Rx.rxCrTimerStartMs  = 0U;
    memset(CanTp_Rx.rxChunkBuffer, 0, sizeof(CanTp_Rx.rxChunkBuffer));
}

/* Abort helper for Tx */
static void CanTp_AbortTx(CanTp_TxAbortReasonType reason) {
    TRACE("[CanTp] AbortTx reason=%u state=%u", (uint32)reason, (uint32)CanTp_Tx.state);
    if (CanTp_Tx.state == TX_IDLE) {
        return;
    }
    if (!CanTp_Tx.resultReported) {
        CanTp_Tx.resultReported = TRUE;
        PduR_CanTpTxConfirmation(CanTp_Tx.txNSduId, E_NOT_OK);
    }
    CanTp_Tx.state = TX_IDLE;
}

/* Complete helper for Tx */
static void CanTp_CompleteTx(void) {
    CANTP_DBG("[CanTp] CompleteTx totalLen=%u", CanTp_Tx.totalLength);
    if (!CanTp_Tx.resultReported) {
        CanTp_Tx.resultReported = TRUE;
        PduR_CanTpTxConfirmation(CanTp_Tx.txNSduId, E_OK);
    }
    CanTp_Tx.state = TX_IDLE;
}

/* Abort helper for Rx */
static void CanTp_AbortRx(CanTp_RxAbortReasonType reason) {
    TRACE("[CanTp] AbortRx reason=%u state=%u", (uint32)reason, (uint32)CanTp_Rx.state);
    if (CanTp_Rx.state == RX_IDLE && !CanTp_Rx.queueSlotReserved) {
        return;
    }
    if (!CanTp_Rx.resultReported && CanTp_Rx.queueSlotReserved) {
        CanTp_Rx.resultReported = TRUE;
        PduR_CanTpRxIndication(CanTp_Rx.rxNSduId, E_NOT_OK);
    }
    CanTp_Rx.state = RX_IDLE;
    CanTp_Rx.queueSlotReserved = FALSE;
}

/* ================================================================== */
/*  FRAME BUILDERS (Mentor Patch Wire Format)                         */
/* ================================================================== */

static void CanTp_PrepareDataFrame(void) {
    memset(CanTp_Tx.txDataFrame, CANTP_PADDING_BYTE, CANTP_FRAME_LENGTH);
    PduLengthType left = CanTp_Tx.totalLength - CanTp_Tx.txOffset;

    if (CanTp_Tx.totalLength <= CANTP_PAYLOAD_SF_MAX) {
        /* Single Frame (1..7 bytes) — Student Guide v2.0 wire format:
         * byte 0: 0x0L  (L = data length in low nibble)
         * byte 1..L: data payload
         * byte L+1..7: padding 0x00 */
        CanTp_Tx.txDataFrame[0] = (uint8)(CANTP_FRAME_TYPE_SF | (CanTp_Tx.totalLength & 0x0FU));
        memcpy(&CanTp_Tx.txDataFrame[1], CanTp_Tx.txChunkBuffer, CanTp_Tx.totalLength);
        CanTp_Tx.preparedPayloadBytes = (uint8)CanTp_Tx.totalLength;
        CanTp_Tx.preparedFrameType    = CANTP_FRAME_SF;
    } else if (CanTp_Tx.txOffset == 0U) {
        /* First Frame (8..62 bytes) — Student Guide v2.0 wire format:
         * byte 0: 0x10 | ((FF_DL >> 8) & 0x0F)  (12-bit FF_DL)
         * byte 1: FF_DL & 0xFF
         * byte 2..7: first 6 bytes of data */
        CanTp_Tx.txDataFrame[0] = (uint8)(CANTP_FRAME_TYPE_FF | ((CanTp_Tx.totalLength >> 8U) & 0x0FU));
        CanTp_Tx.txDataFrame[1] = (uint8)(CanTp_Tx.totalLength & 0xFFU);
        memcpy(&CanTp_Tx.txDataFrame[2], CanTp_Tx.txChunkBuffer, CANTP_PAYLOAD_FF_MAX);
        CanTp_Tx.preparedPayloadBytes = CANTP_PAYLOAD_FF_MAX;
        CanTp_Tx.preparedFrameType    = CANTP_FRAME_FF;
    } else {
        /* Consecutive Frame:
         * byte 0: 0x20 | (nextSN & 0x0F)
         * byte 1..7: up to 7 bytes of data, padded with 0x00 */
        uint8 payloadLen = (uint8)((left < CANTP_PAYLOAD_CF_MAX) ? left : CANTP_PAYLOAD_CF_MAX);
        CanTp_Tx.txDataFrame[0] = (uint8)(CANTP_FRAME_TYPE_CF | (CanTp_Tx.nextSN & 0x0FU));
        memcpy(&CanTp_Tx.txDataFrame[1], &CanTp_Tx.txChunkBuffer[CanTp_Tx.txOffset], payloadLen);
        CanTp_Tx.preparedPayloadBytes = payloadLen;
        CanTp_Tx.preparedFrameType    = CANTP_FRAME_CF;
    }

    CanTp_Tx.retryCount       = 0U;
    CanTp_Tx.dataAttemptDueMs = CanTp_GetTimeMs();
    CanTp_Tx.state            = TX_REQUEST_TX;
}

static void CanTp_PrepareFCFrame(uint8 fs) {
    memset(CanTp_Rx.txFcFrame, CANTP_PADDING_BYTE, CANTP_FRAME_LENGTH);
    CanTp_Rx.txFcFrame[0] = (uint8)(CANTP_FRAME_TYPE_FC | (fs & 0x0FU));
    if (fs == CANTP_FC_FS_CTS) {
        CanTp_Rx.txFcFrame[1] = CANTP_BS;        /* BS = 4 */
        CanTp_Rx.txFcFrame[2] = CANTP_STMIN_MS;  /* STmin = 5 ms */
    } else {
        CanTp_Rx.txFcFrame[1] = 0x00U;
        CanTp_Rx.txFcFrame[2] = 0x00U;
    }
    CanTp_Rx.fcRetryCount    = 0U;
    CanTp_Rx.fcRequestActive = TRUE;
    CanTp_Rx.fcAttemptDueMs  = CanTp_GetTimeMs();
}

/* ================================================================== */
/*  PUBLIC API: INIT & TRANSMIT                                       */
/* ================================================================== */

void CanTp_Init(void) {
    CanTp_TxReset();
    CanTp_Tx.txPduPending = FALSE;
    CanTp_RxReset();
    CanTp_Rx.fcTxPending = FALSE;
    CanTp_Rx.fcRequestActive = FALSE;
    TRACE("[CanTp] Init complete");
}

Std_ReturnType CanTp_Transmit(PduIdType txNSduId, const PduInfoType *request) {
    if (request == NULL_PTR || request->SduLength == 0U || request->SduLength > CANTP_MAX_NSDU) {
        TRACE("[CanTp] Transmit invalid param len=%u", (request != NULL_PTR) ? request->SduLength : 0U);
        return E_NOT_OK;
    }

    /* Reject if Tx session active or Data PDU locked */
    if (CanTp_Tx.state != TX_IDLE || CanTp_Tx.txPduPending) {
        TRACE("[CanTp] Transmit busy: state=%u pending=%u", (uint32)CanTp_Tx.state, (uint32)CanTp_Tx.txPduPending);
        return E_NOT_OK;
    }

    CanTp_Tx.txNSduId            = txNSduId;
    CanTp_Tx.totalLength         = request->SduLength;
    CanTp_Tx.txOffset            = 0U;
    CanTp_Tx.nextSN              = 1U;
    CanTp_Tx.blockCount          = 0U;
    CanTp_Tx.resultReported      = FALSE;
    CanTp_Tx.fcPermissionGranted = (request->SduLength <= CANTP_PAYLOAD_SF_MAX);  /* SF needs no FC */
    CanTp_Tx.priorCfExists       = FALSE;
    CanTp_Tx.lastCfConfirmedMs   = 0U;
    CanTp_Tx.state               = TX_PREPARE;

    /* Snapshot source data once from request or via PduR */
    if (request->SduDataPtr != NULL_PTR) {
        memcpy(CanTp_Tx.txChunkBuffer, request->SduDataPtr, CanTp_Tx.totalLength);
        CANTP_DBG("[CanTp] Transmit snapshotted %u bytes from request", (uint32)CanTp_Tx.totalLength);
    } else if (PduR_CanTpCopyTxData(txNSduId, CanTp_Tx.txChunkBuffer, CanTp_Tx.totalLength) != BUFREQ_OK) {
        CanTp_AbortTx(CANTP_TX_ABORT_COPY_FAILED);
        return E_OK; /* Request accepted, delivered via callback */
    }

    CanTp_PrepareDataFrame();
    return E_OK;
}

/* ================================================================== */
/*  PUBLIC API: SCHEDULER TICK (1 ms)                                 */
/* ================================================================== */

void CanTp_MainFunction(void) {
    uint32 now = CanTp_GetTimeMs();

    /* Auto-unlock txPduPending if state is TX_IDLE and no late confirmation arrived after grace period */
    if (CanTp_Tx.txPduPending && CanTp_Tx.state == TX_IDLE) {
        if ((uint32)(now - CanTp_Tx.txTimerStartMs) >= (CANTP_N_AS_MS + 300U)) {
            CanTp_Tx.txPduPending = FALSE;
            TRACE("[CanTp] Auto-cleared stale txPduPending lock");
        }
    }

    /* Auto-unlock fcTxPending if state is RX_IDLE and no late confirmation arrived after grace period */
    if (CanTp_Rx.fcTxPending && CanTp_Rx.state == RX_IDLE) {
        if ((uint32)(now - CanTp_Rx.fcTimerStartMs) >= (CANTP_N_AR_MS + 300U)) {
            CanTp_Rx.fcTxPending = FALSE;
            TRACE("[CanTp] Auto-cleared stale fcTxPending lock");
        }
    }

    /* -------------------------------------------------------------- */
    /* 1. Tx STmin Gate & Timers (N_As, N_Bs)                          */
    /* -------------------------------------------------------------- */
    if (CanTp_Tx.state == TX_WAIT_STMIN) {
        if (CanTp_Tx.fcPermissionGranted &&
            ((uint32)(now - CanTp_Tx.lastCfConfirmedMs) >= CANTP_STMIN_MS)) {
            CANTP_DBG("[CanTp] STmin satisfied (%ums >= %ums), prepare next CF",
                  (uint32)(now - CanTp_Tx.lastCfConfirmedMs), CANTP_STMIN_MS);
            CanTp_PrepareDataFrame();
        }
    } else if (CanTp_Tx.state == TX_WAIT_CONFIRM) {
        if ((uint32)(now - CanTp_Tx.txTimerStartMs) >= CANTP_N_AS_MS) {
            TRACE("[CanTp] N_As timeout -> Abort");
            CanTp_AbortTx(CANTP_TX_ABORT_N_AS_TIMEOUT);
            /* NOTE: txPduPending remains TRUE until matching confirmation */
        }
    } else if (CanTp_Tx.state == TX_WAIT_FC) {
        if ((uint32)(now - CanTp_Tx.txTimerStartMs) >= CANTP_N_BS_MS) {
            TRACE("[CanTp] N_Bs timeout -> Abort");
            CanTp_AbortTx(CANTP_TX_ABORT_N_BS_TIMEOUT);
        }
    }

    /* -------------------------------------------------------------- */
    /* 2. Tx Data Request & Retry                                      */
    /* -------------------------------------------------------------- */
    if (CanTp_Tx.state == TX_REQUEST_TX) {
        if (now >= CanTp_Tx.dataAttemptDueMs) {
            PduInfoType pdu;
            pdu.SduDataPtr  = CanTp_Tx.txDataFrame;
            pdu.SduLength   = CANTP_FRAME_LENGTH;
            pdu.MetaDataPtr = NULL_PTR;

            CanTp_Tx.txPduPending   = TRUE;
            CanTp_Tx.txTimerStartMs = now; /* Start N_As */
            CanTp_Tx.state          = TX_WAIT_CONFIRM;

            Std_ReturnType ret = PduR_CanTpTransmit(CANTP_TX_DATA_LPDU_ID, &pdu);
            if (ret == E_OK) {
                CANTP_DBG("[CanTp] Data TX accepted type=%u len=%u",
                      (uint32)CanTp_Tx.preparedFrameType, (uint32)CanTp_Tx.preparedPayloadBytes);
            } else {
                CanTp_Tx.txPduPending = FALSE;
                CanTp_Tx.state        = TX_REQUEST_TX;
                if (CanTp_Tx.retryCount < CANTP_MAX_RETRIES) {
                    CanTp_Tx.retryCount++;
                    CanTp_Tx.dataAttemptDueMs = now + 1U;
                    CANTP_DBG("[CanTp] Data TX retry %u/3", (uint32)CanTp_Tx.retryCount);
                } else {
                    TRACE("[CanTp] Data TX retries exhausted -> Abort");
                    CanTp_AbortTx(CANTP_TX_ABORT_RETRY_EXHAUSTED);
                }
            }
        }
    }

    /* -------------------------------------------------------------- */
    /* 3. Rx Flow Control Request & Retry                              */
    /* -------------------------------------------------------------- */
    if (CanTp_Rx.fcRequestActive && !CanTp_Rx.fcTxPending) {
        if (now >= CanTp_Rx.fcAttemptDueMs) {
            PduInfoType pdu;
            pdu.SduDataPtr  = CanTp_Rx.txFcFrame;
            pdu.SduLength   = CANTP_FRAME_LENGTH;
            pdu.MetaDataPtr = NULL_PTR;

            CanTp_Rx.fcTxPending    = TRUE;
            CanTp_Rx.fcTimerStartMs = now; /* Start N_Ar */

            Std_ReturnType ret = PduR_CanTpTransmit(CANTP_TX_FC_LPDU_ID, &pdu);
            if (ret == E_OK) {
                CANTP_DBG("[CanTp] FC TX accepted FS=0x%02X", CanTp_Rx.txFcFrame[0]);
            } else {
                CanTp_Rx.fcTxPending = FALSE;
                if (CanTp_Rx.fcRetryCount < CANTP_MAX_RETRIES) {
                    CanTp_Rx.fcRetryCount++;
                    CanTp_Rx.fcAttemptDueMs = now + 1U;
                    CANTP_DBG("[CanTp] FC TX retry %u/3", (uint32)CanTp_Rx.fcRetryCount);
                } else {
                    TRACE("[CanTp] FC TX retries exhausted");
                    CanTp_Rx.fcRequestActive = FALSE;
                    if (CanTp_Rx.state != RX_IDLE) {
                        CanTp_AbortRx(CANTP_RX_ABORT_FC_RETRY_EXHAUSTED);
                    }
                }
            }
        }
    }

    /* -------------------------------------------------------------- */
    /* 4. Rx Timers (N_Ar, N_Cr)                                       */
    /* -------------------------------------------------------------- */
    if (CanTp_Rx.fcTxPending) {
        if ((uint32)(now - CanTp_Rx.fcTimerStartMs) >= CANTP_N_AR_MS) {
            TRACE("[CanTp] N_Ar timeout");
            if (CanTp_Rx.state != RX_IDLE) {
                CanTp_AbortRx(CANTP_RX_ABORT_N_AR_TIMEOUT);
            }
            CanTp_Rx.fcTimerStartMs = now + 10000000U; /* Avoid repeated timeout triggers */
            /* NOTE: fcTxPending remains locked until matching confirmation */
        }
    }

    if (CanTp_Rx.state == RX_WAIT_CF) {
        if ((uint32)(now - CanTp_Rx.rxCrTimerStartMs) >= CANTP_N_CR_MS) {
            TRACE("[CanTp] N_Cr timeout -> Abort");
            CanTp_AbortRx(CANTP_RX_ABORT_N_CR_TIMEOUT);
        }
    }
}

/* ================================================================== */
/*  PUBLIC API: TX CONFIRMATION                                        */
/* ================================================================== */

void CanTp_TxConfirmation(PduIdType txNPduId) {
    uint32 now = CanTp_GetTimeMs();

    if (txNPduId == CANTP_TX_DATA_LPDU_ID) {
        /* Data frame confirmation */
        if (!CanTp_Tx.txPduPending) {
            TRACE("[CanTp] Unexpected Data TxConf (not pending)");
            return;
        }
        CanTp_Tx.txPduPending = FALSE;

        if (CanTp_Tx.state != TX_WAIT_CONFIRM) {
            TRACE("[CanTp] Late Data TxConf after state change (state=%u)", (uint32)CanTp_Tx.state);
            return;
        }

        /* Commit transmitted bytes */
        CanTp_Tx.txOffset += CanTp_Tx.preparedPayloadBytes;
        CANTP_DBG("[CanTp] Data Confirmed: committed %u bytes, offset=%u/%u",
              (uint32)CanTp_Tx.preparedPayloadBytes, (uint32)CanTp_Tx.txOffset, (uint32)CanTp_Tx.totalLength);

        if (CanTp_Tx.preparedFrameType == CANTP_FRAME_CF) {
            CanTp_Tx.nextSN            = (uint8)((CanTp_Tx.nextSN + 1U) & 0x0FU);
            CanTp_Tx.blockCount++;
            CanTp_Tx.priorCfExists     = TRUE;
            CanTp_Tx.lastCfConfirmedMs = now; /* STmin gate starts here */
        }

        if (CanTp_Tx.txOffset == CanTp_Tx.totalLength) {
            CanTp_CompleteTx();
        } else if (CanTp_Tx.preparedFrameType == CANTP_FRAME_FF ||
                   CanTp_Tx.blockCount >= CANTP_BS) {
            CanTp_Tx.state               = TX_WAIT_FC;
            CanTp_Tx.txTimerStartMs      = now; /* Start N_Bs */
            CanTp_Tx.fcPermissionGranted = FALSE;
            CANTP_DBG("[CanTp] Wait for FC (BS=%u), N_Bs started at %ums", CANTP_BS, now);
        } else {
            CanTp_Tx.state = TX_WAIT_STMIN;
        }
    } else if (txNPduId == CANTP_TX_FC_LPDU_ID) {
        /* Flow Control frame confirmation */
        if (!CanTp_Rx.fcTxPending) {
            TRACE("[CanTp] Unexpected FC TxConf (not pending)");
            return;
        }
        CanTp_Rx.fcTxPending     = FALSE;
        CanTp_Rx.fcRequestActive = FALSE;

        if (CanTp_Rx.isStandaloneOvflw) {
            CANTP_DBG("[CanTp] Standalone FC(OVFLW) confirmed");
            CanTp_Rx.isStandaloneOvflw = FALSE;
            CanTp_Rx.state             = RX_IDLE;
            return;
        }

        if (CanTp_Rx.state == RX_FC_PENDING) {
            CanTp_Rx.state            = RX_WAIT_CF;
            CanTp_Rx.rxCrTimerStartMs = now; /* Start N_Cr */
            CANTP_DBG("[CanTp] FC(CTS) Confirmed -> RX_WAIT_CF, N_Cr started at %ums", now);
        } else {
            TRACE("[CanTp] Late FC TxConf in state=%u", (uint32)CanTp_Rx.state);
        }
    }
}

/* ================================================================== */
/*  PUBLIC API: RX INDICATION                                          */
/* ================================================================== */

void CanTp_RxIndication(PduIdType rxNPduId, const PduInfoType *pduInfo) {
    if (pduInfo == NULL_PTR || pduInfo->SduDataPtr == NULL_PTR) {
        return;
    }
    if (pduInfo->SduLength != CANTP_FRAME_LENGTH) {
        TRACE("[CanTp] RxIndication rejected: DLC=%u != 8", (uint32)pduInfo->SduLength);
        return;
    }

    const uint8 *frame = pduInfo->SduDataPtr;
    uint32 now = CanTp_GetTimeMs();

    /* -------------------------------------------------------------- */
    /* 1. Rx FC N-PDU (CAN ID 0x731) — Processed by SENDER            */
    /* -------------------------------------------------------------- */
    if (rxNPduId == CANTP_RX_FC_NPDU_ID) {
        if ((frame[0] & CANTP_FRAME_TYPE_MASK) != CANTP_FRAME_TYPE_FC) {
            TRACE("[CanTp] FC N-PDU invalid frame type 0x%02X", frame[0]);
            return;
        }
        if (CanTp_Tx.state != TX_WAIT_FC) {
            TRACE("[CanTp] FC received while Tx not in TX_WAIT_FC (state=%u)", (uint32)CanTp_Tx.state);
            return;
        }

        uint8 fs    = frame[0] & 0x0FU;
        uint8 bs    = frame[1];
        uint8 stmin = frame[2];
        CANTP_DBG("[CanTp] FC Rx: FS=%u BS=%u STmin=%u", (uint32)fs, (uint32)bs, (uint32)stmin);

        if (fs == CANTP_FC_FS_CTS) {
            /* Student Guide §10 Item P3.6: Sender accepts CTS only if BS and STmin match static config.
             * Mismatch -> Abort Tx E_NOT_OK. */
            if (bs != CANTP_BS || stmin != CANTP_STMIN_MS) {
                TRACE("[CanTp] FC param mismatch: BS=%u STmin=%u (expected %u/%u) -> Abort Tx",
                      (uint32)bs, (uint32)stmin, CANTP_BS, CANTP_STMIN_MS);
                CanTp_AbortTx(CANTP_TX_ABORT_FC_INVALID);
                return;
            }

            CanTp_Tx.blockCount          = 0U;
            CanTp_Tx.fcPermissionGranted = TRUE;
            CANTP_DBG("[CanTp] CTS accepted (BS=%u STmin=%ums)", CANTP_BS, CANTP_STMIN_MS);

            /* Check STmin eligibility */
            if (!CanTp_Tx.priorCfExists ||
                ((uint32)(now - CanTp_Tx.lastCfConfirmedMs) >= CANTP_STMIN_MS)) {
                CANTP_DBG("[CanTp] CTS valid & STmin ready -> prepare next CF");
                CanTp_PrepareDataFrame();
            } else {
                CANTP_DBG("[CanTp] CTS valid, waiting for STmin delta");
                CanTp_Tx.state = TX_WAIT_STMIN;
            }
        } else if (fs == CANTP_FC_FS_OVFLW) {
            TRACE("[CanTp] FC(OVFLW) received -> Abort Tx");
            CanTp_AbortTx(CANTP_TX_ABORT_FC_OVFLW);
        } else {
            /* Student Guide §10 Item P3.6: FS unsupported (including WAIT) -> Abort Tx */
            TRACE("[CanTp] Unsupported FC FS=%u -> Abort Tx", (uint32)fs);
            CanTp_AbortTx(CANTP_TX_ABORT_FC_INVALID);
        }
        return;
    }

    /* -------------------------------------------------------------- */
    /* 2. Rx Data N-PDU (CAN ID 0x730) — Processed by RECEIVER        */
    /* -------------------------------------------------------------- */
    if (rxNPduId == CANTP_RX_DATA_NPDU_ID) {
        uint8 frameType = frame[0] & CANTP_FRAME_TYPE_MASK;

        /* Single Frame (0x00) — Student Guide v2.0:
         * byte 0 low nibble = SF_DL (1..7), data at byte 1..SF_DL */
        if (frameType == CANTP_FRAME_TYPE_SF) {
            uint8 sfLen = frame[0] & 0x0FU;
            if (sfLen == 0U || sfLen > CANTP_PAYLOAD_SF_MAX) {
                TRACE("[CanTp] SF discarded: invalid SF_DL=%u", (uint32)sfLen);
                return;
            }

            /* Session replacement check */
            if (CanTp_Rx.state == RX_WAIT_CF && !CanTp_Rx.fcTxPending) {
                CANTP_DBG("[CanTp] New valid SF replaces active Rx session");
                CanTp_AbortRx(CANTP_RX_ABORT_REPLACED);
            }

            if (CanTp_Rx.state == RX_IDLE) {
                if (PduR_CanTpStartOfReception(0U, sfLen) == BUFREQ_OK) {
                    memcpy(CanTp_Rx.rxChunkBuffer, &frame[1], sfLen);
                    PduR_CanTpCopyRxData(0U, CanTp_Rx.rxChunkBuffer, sfLen);
                    PduR_CanTpRxIndication(0U, E_OK);
                    CANTP_DBG("[CanTp] SF received & delivered %u bytes", (uint32)sfLen);
                } else {
                    TRACE("[CanTp] SF queue full: discarded without FC");
                }
            }
            return;
        }

        /* First Frame (0x10) — Student Guide v2.0:
         * 12-bit FF_DL: byte 0 low nibble << 8 | byte 1 */
        if (frameType == CANTP_FRAME_TYPE_FF) {
            uint16 totalLen = (uint16)(((uint16)(frame[0] & 0x0FU) << 8U) | (uint16)frame[1]);

            /* Malformed FF check: FF_DL < 8 is invalid per Student Guide */
            if (totalLen < 8U) {
                TRACE("[CanTp] FF discarded: FF_DL=%u < 8", (uint32)totalLen);
                return;
            }

            /* Oversized FF check: length > 62 bytes */
            if (totalLen > CANTP_MAX_NSDU) {
                TRACE("[CanTp] FF oversized (%u > 62) -> send FC(OVFLW)", (uint32)totalLen);
                if (CanTp_Rx.state == RX_IDLE && !CanTp_Rx.fcTxPending) {
                    CanTp_Rx.isStandaloneOvflw = TRUE;
                    CanTp_PrepareFCFrame(CANTP_FC_FS_OVFLW);
                }
                return;
            }

            /* Session replacement check */
            if (CanTp_Rx.state == RX_WAIT_CF && !CanTp_Rx.fcTxPending) {
                CANTP_DBG("[CanTp] New valid FF replaces active Rx session");
                CanTp_AbortRx(CANTP_RX_ABORT_REPLACED);
            }

            if (CanTp_Rx.state == RX_IDLE) {
                if (PduR_CanTpStartOfReception(0U, totalLen) != BUFREQ_OK) {
                    TRACE("[CanTp] FF queue full -> send FC(OVFLW)");
                    if (!CanTp_Rx.fcTxPending) {
                        CanTp_Rx.isStandaloneOvflw = TRUE;
                        CanTp_PrepareFCFrame(CANTP_FC_FS_OVFLW);
                    }
                    return;
                }

                /* Slot reserved successfully */
                CanTp_Rx.queueSlotReserved = TRUE;
                CanTp_Rx.totalLength       = totalLen;
                memcpy(CanTp_Rx.rxChunkBuffer, &frame[2], CANTP_PAYLOAD_FF_MAX);
                CanTp_Rx.receivedLength    = CANTP_PAYLOAD_FF_MAX;
                CanTp_Rx.expectedSN        = 1U;
                CanTp_Rx.blockCount        = 0U;
                CanTp_Rx.resultReported    = FALSE;
                CanTp_Rx.isStandaloneOvflw = FALSE;
                CanTp_Rx.state             = RX_FC_PENDING;

                CANTP_DBG("[CanTp] FF accepted len=%u rx=%u -> request FC(CTS)",
                      (uint32)totalLen, (uint32)CanTp_Rx.receivedLength);
                CanTp_PrepareFCFrame(CANTP_FC_FS_CTS);
            }
            return;
        }

        /* Consecutive Frame (0x20) */
        if (frameType == CANTP_FRAME_TYPE_CF) {
            if (CanTp_Rx.state != RX_WAIT_CF) {
                TRACE("[CanTp] CF unexpected (state=%u) -> ignored", (uint32)CanTp_Rx.state);
                return;
            }

            uint8 sn = frame[0] & 0x0FU;
            if (sn != CanTp_Rx.expectedSN) {
                TRACE("[CanTp] Wrong SN: got=%u expected=%u -> Abort Rx before append",
                      (uint32)sn, (uint32)CanTp_Rx.expectedSN);
                CanTp_AbortRx(CANTP_RX_ABORT_WRONG_SN);
                return;
            }

            /* Valid CF: append data */
            PduLengthType remain = CanTp_Rx.totalLength - CanTp_Rx.receivedLength;
            uint8 realBytes = (uint8)((remain < CANTP_PAYLOAD_CF_MAX) ? remain : CANTP_PAYLOAD_CF_MAX);
            memcpy(&CanTp_Rx.rxChunkBuffer[CanTp_Rx.receivedLength], &frame[1], realBytes);
            CanTp_Rx.receivedLength += realBytes;
            CanTp_Rx.expectedSN = (uint8)((CanTp_Rx.expectedSN + 1U) & 0x0FU);
            CanTp_Rx.blockCount++;

            CANTP_DBG("[CanTp] CF rx: sn=%u bytes=%u totalRx=%u/%u blockCount=%u",
                  (uint32)sn, (uint32)realBytes, (uint32)CanTp_Rx.receivedLength,
                  (uint32)CanTp_Rx.totalLength, (uint32)CanTp_Rx.blockCount);

            if (CanTp_Rx.receivedLength == CanTp_Rx.totalLength) {
                /* Complete N-SDU reassembled */
                CANTP_DBG("[CanTp] N-SDU reassembly complete (%u bytes)", (uint32)CanTp_Rx.totalLength);
                if (PduR_CanTpCopyRxData(0U, CanTp_Rx.rxChunkBuffer, CanTp_Rx.totalLength) == BUFREQ_OK) {
                    if (!CanTp_Rx.resultReported) {
                        CanTp_Rx.resultReported = TRUE;
                        PduR_CanTpRxIndication(0U, E_OK);
                    }
                } else {
                    CanTp_AbortRx(CANTP_RX_ABORT_COPY_FAILED);
                }
                CanTp_RxReset();
            } else if (CanTp_Rx.blockCount == CANTP_BS) {
                /* Block exhausted -> send next FC(CTS) */
                CanTp_Rx.state      = RX_FC_PENDING;
                CanTp_Rx.blockCount = 0U;
                CANTP_DBG("[CanTp] Block complete (%u CFs) -> request next FC(CTS)", CANTP_BS);
                CanTp_PrepareFCFrame(CANTP_FC_FS_CTS);
            } else {
                /* Restart N_Cr timer for next CF */
                CanTp_Rx.rxCrTimerStartMs = now;
            }
            return;
        }
    }
}

/* ================================================================== */
/*  DIAGNOSTIC / INTROSPECTION APIS                                   */
/* ================================================================== */

CanTp_TxStateType CanTp_GetTxState(void)           { return CanTp_Tx.state; }
CanTp_RxStateType CanTp_GetRxState(void)           { return CanTp_Rx.state; }
boolean           CanTp_IsTxPduPending(void)       { return CanTp_Tx.txPduPending; }
boolean           CanTp_IsFcTxPending(void)        { return CanTp_Rx.fcTxPending; }
PduLengthType     CanTp_GetTxOffset(void)          { return CanTp_Tx.txOffset; }
uint8             CanTp_GetTxNextSN(void)          { return CanTp_Tx.nextSN; }
uint8             CanTp_GetRxExpectedSN(void)      { return CanTp_Rx.expectedSN; }
PduLengthType     CanTp_GetRxReceivedLength(void)  { return CanTp_Rx.receivedLength; }
