#include "canif/CanIf.h"
#include "pdur/PduR.h"
#include "cantp/CanTp.h"
#include "app/Role.h"
#include "trace/Trace.h"
#include <stddef.h> /* For NULL */

#define CANIF_INVALID_TX_PDU_ID 0xFFFFu

/* Stores the last accepted TxPduId per HTH to resolve TxConfirmation identity */
static PduIdType CanIf_ActiveTxPduId[CAN_NUM_HTH];
CanIf_TransmitHookType CanIf_TransmitHook = NULL;

void CanIf_Init(void) {
    for (int i = 0; i < CAN_NUM_HTH; i++) {
        CanIf_ActiveTxPduId[i] = CANIF_INVALID_TX_PDU_ID;
    }
}

Std_ReturnType CanIf_Transmit(PduIdType TxPduId, const PduInfoType* PduInfo) {
    if (CanIf_TransmitHook != NULL) {
        return CanIf_TransmitHook(TxPduId, PduInfo);
    }
    if (TxPduId >= CANIF_NUM_TX_PDU) return E_NOT_OK;
    const CanIf_TxPduConfigType* config = &CanIfTxPdu[TxPduId];

    Can_PduType canPdu;
    canPdu.swPduHandle = TxPduId;
    canPdu.length = config->dlc; /* Explicitly use 8 bytes */
    canPdu.id = config->canId;
    canPdu.sdu = PduInfo->SduDataPtr;

    Can_ReturnType ret = Can_Write(config->hthRef, &canPdu);

    if (ret == CAN_OK) {
        CanIf_ActiveTxPduId[config->hthRef] = TxPduId;
        return E_OK;
    } else {
        return E_NOT_OK;
    }
}

void CanIf_TxConfirmation(Can_HwHandleType Hth) {
    if (Hth >= CAN_NUM_HTH) return;
    PduIdType txPduId = CanIf_ActiveTxPduId[Hth];

    if (txPduId == CANIF_INVALID_TX_PDU_ID) {
        return;
    }

    /* Valid confirmation */
    CanIf_ActiveTxPduId[Hth] = CANIF_INVALID_TX_PDU_ID;

    if (txPduId == CANTP_TX_DATA_LPDU_ID || txPduId == CANTP_TX_FC_LPDU_ID) {
        CanTp_TxConfirmation(txPduId);
    }
}

void CanIf_RxIndication(Can_HwHandleType Hrh, const Can_RxPduType* RxPdu) {
    RoleType role = Role_Get();
    /* Role 1 is CanTp Sender: never process CanTp Data frames (0x730) */
    if (role == ROLE_1_ENGINE_TX && RxPdu->id == 0x730) {
        return;
    }
    /* Role 2 is CanTp Receiver: never process CanTp FC frames (0x731) */
    if (role == ROLE_2_BODY_TX && RxPdu->id == 0x731) {
        return;
    }

    /* Linear search for matching HRH and CAN ID */
    for (int i = 0; i < CANIF_NUM_RX_PDU; i++) {
        if (CanIfRxPdu[i].hrhRef == Hrh && CanIfRxPdu[i].canId == RxPdu->id) {
            PduInfoType pduInfo;
            pduInfo.SduDataPtr = (uint8*)RxPdu->sdu;
            pduInfo.SduLength = RxPdu->length;
            pduInfo.MetaDataPtr = NULL;
            /* F-03: Pass CanIf's rxPduId instead of COM's upperPduId to PduR */
            PduR_CanIfRxIndication(CanIfRxPdu[i].rxPduId, &pduInfo);
            return;
        }
    }
    /* Frame silently dropped if no match */
}
