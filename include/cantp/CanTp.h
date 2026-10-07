#ifndef CANTP_H
#define CANTP_H

#include "ComStack_Types.h"
#include "cantp/CanTp_Cfg.h"

/*
 * MOCK CanTP — Transport Protocol Module (Phase 1–3)
 * Following Student Implementation Guide v2.0 & Mentor Wire Format Patch
 * Enhanced with ISO 15765-2 FC(WAIT) support and dynamic STmin/BS.
 */

/* ------------------------------------------------------------------ */
/* FSM State Types                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    TX_IDLE = 0,
    TX_PREPARE,
    TX_REQUEST_TX,
    TX_WAIT_CONFIRM,
    TX_WAIT_FC,
    TX_WAIT_STMIN
} CanTp_TxStateType;

typedef enum {
    RX_IDLE = 0,
    RX_FC_PENDING,
    RX_WAIT_CF
} CanTp_RxStateType;

/* ------------------------------------------------------------------ */
/* Frame Types & Abort Reasons                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    CANTP_FRAME_SF = 0,
    CANTP_FRAME_FF,
    CANTP_FRAME_CF,
    CANTP_FRAME_FC
} CanTp_FrameKindType;

typedef enum {
    CANTP_TX_ABORT_REASON_NONE = 0,
    CANTP_TX_ABORT_COPY_FAILED,
    CANTP_TX_ABORT_RETRY_EXHAUSTED,
    CANTP_TX_ABORT_N_AS_TIMEOUT,
    CANTP_TX_ABORT_N_BS_TIMEOUT,
    CANTP_TX_ABORT_FC_OVFLW,
    CANTP_TX_ABORT_FC_INVALID,
    CANTP_TX_ABORT_WFT_EXCEEDED        /* FC(WAIT) count exceeded WFTmax */
} CanTp_TxAbortReasonType;

typedef enum {
    CANTP_RX_ABORT_REASON_NONE = 0,
    CANTP_RX_ABORT_COPY_FAILED,
    CANTP_RX_ABORT_WRONG_SN,
    CANTP_RX_ABORT_N_CR_TIMEOUT,
    CANTP_RX_ABORT_N_AR_TIMEOUT,
    CANTP_RX_ABORT_FC_RETRY_EXHAUSTED,
    CANTP_RX_ABORT_REPLACED
} CanTp_RxAbortReasonType;

/* ------------------------------------------------------------------ */
/* Public API Functions                                               */
/* ------------------------------------------------------------------ */

void           CanTp_Init(void);
Std_ReturnType CanTp_Transmit(PduIdType txNSduId, const PduInfoType *request);
void           CanTp_MainFunction(void);
void           CanTp_RxIndication(PduIdType rxNPduId, const PduInfoType *pduInfo);
void           CanTp_TxConfirmation(PduIdType txNPduId);

/* Diagnostic / Test introspection queries */
CanTp_TxStateType CanTp_GetTxState(void);
CanTp_RxStateType CanTp_GetRxState(void);
boolean           CanTp_IsTxPduPending(void);
boolean           CanTp_IsFcTxPending(void);
PduLengthType     CanTp_GetTxOffset(void);
uint8             CanTp_GetTxNextSN(void);
uint8             CanTp_GetRxExpectedSN(void);
PduLengthType     CanTp_GetRxReceivedLength(void);

#endif /* CANTP_H */
