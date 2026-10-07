#ifndef APP_H
#define APP_H

#include "Std_Types.h"
#include "ComStack_Types.h"
#include "cantp/CanTp_Cfg.h"

/*
 * Application layer — role-aware signal TX/RX and CanTP stream integration.
 * Role is determined by Role_Get() which reads button GPIO at startup.
 *
 * Role 1 (ROLE_1_ENGINE_TX): Image Sender — pops UART Rx Queue → CanTp chunks
 * Role 2 (ROLE_2_BODY_TX):   Image Receiver — forwards CanTp N-SDU → UART Tx
 */

#define APP_RX_QUEUE_SLOTS 2U

typedef enum {
    APP_SLOT_FREE = 0,
    APP_SLOT_RESERVED,
    APP_SLOT_READY
} App_QueueSlotStateType;

typedef struct {
    App_QueueSlotStateType state;
    uint8 data[CANTP_CHUNK_CAPACITY];
    uint16 length;
} App_RxQueueSlotType;

extern App_RxQueueSlotType App_RxQueue[APP_RX_QUEUE_SLOTS];

/* --- Image Sender State Machine (Role 1 — Master) --- */
/* Assignment §5.3 + §5.4: chunk-by-chunk with retry policy */

#define APP_IMG_TX_MAX_RETRIES  3U  /* Max retries per chunk (Assignment §5.4) */

typedef enum {
    APP_IMG_TX_IDLE = 0,        /* No image transfer in progress */
    APP_IMG_TX_LOAD_CHUNK,      /* Pop next chunk from UART Rx Queue */
    APP_IMG_TX_WAIT_CANTP,      /* CanTp_Transmit submitted, waiting for TxConfirmation */
    APP_IMG_TX_RETRY            /* Retry current chunk after failure */
} App_ImgTxStateType;

void App_Init(void);
void App_Task_1ms(void);
void App_Task_10ms(void);

/* PduR / CanTp App Callbacks */
BufReq_ReturnType App_CanTpCopyTxData(PduIdType txNSduId, uint8 *dst, PduLengthType length);
void              App_CanTpTxConfirmation(PduIdType txNSduId, Std_ReturnType result);
BufReq_ReturnType App_CanTpStartOfReception(PduIdType rxNSduId, PduLengthType totalLength);
BufReq_ReturnType App_CanTpCopyRxData(PduIdType rxNSduId, const uint8 *completeData, PduLengthType length);
void              App_CanTpRxIndication(PduIdType rxNSduId, Std_ReturnType result);

/* Diagnostics / Verification */
uint32 App_GetTxConfirmationCount(void);
uint32 App_GetRxIndicationCount(void);
Std_ReturnType App_GetLastTxResult(void);
Std_ReturnType App_GetLastRxResult(void);
void   App_ResetCanTpCounters(void);
void   App_SetTxSourceData(const uint8 *data, PduLengthType length);
boolean App_IsImageStreamingActive(void);

/* Task 3 — Slave Status Monitoring (Assignment §4 & §8) */
void    App_ComRxCallback(PduIdType RxPduId);
uint8   App_GetOnlineSlavesCount(void);
boolean App_IsSlaveOnline(uint8 slaveNum);  /* slaveNum: 1 or 2 */
uint8   App_GetSlaveStatus(uint8 slaveNum); /* slaveNum: 1 or 2 (0=NORMAL, 1=MASTER_LOST) */

#endif /* APP_H */
