#include "pdur/PduR.h"
#include "canif/CanIf.h"
#include "com/Com.h"
#include "cantp/CanTp.h"
#include "app/App.h"
#include "trace/Trace.h"

void PduR_Init(void) {
    /* Nothing to initialize for PduR in this mock */
}

Std_ReturnType PduR_ComTransmit(PduIdType TxPduId, const PduInfoType* PduInfo) {
    /* Route from COM to CanIf — pass-by-pointer, no payload modification */
    for (int i = 0; i < PDUR_NUM_ROUTES; i++) {
        if (PduRRoute[i].srcModule == PDUR_COM && PduRRoute[i].srcPduId == TxPduId) {
            /* Routine COM TX trace suppressed to keep UART bandwidth for CanTp */
            return CanIf_Transmit(PduRRoute[i].dstPduId, PduInfo);
        }
    }
    return E_NOT_OK;
}

Std_ReturnType PduR_CanTpTransmit(PduIdType TxPduId, const PduInfoType* PduInfo) {
    /* Route from CanTP to CanIf — pass-by-pointer, no payload modification */
    for (int i = 0; i < PDUR_NUM_ROUTES; i++) {
        if (PduRRoute[i].srcModule == PDUR_CANTP && PduRRoute[i].srcPduId == TxPduId) {
            return CanIf_Transmit(PduRRoute[i].dstPduId, PduInfo);
        }
    }
    return E_NOT_OK;
}

void PduR_CanIfRxIndication(PduIdType RxPduId, const PduInfoType* PduInfo) {
    /* Route incoming CAN frame to COM or CanTP based on routing table */
    for (int i = 0; i < PDUR_NUM_ROUTES; i++) {
        if (PduRRoute[i].srcModule == PDUR_CANIF && PduRRoute[i].srcPduId == RxPduId) {
            if (PduRRoute[i].dstModule == PDUR_COM) {
                Com_RxIndication(PduRRoute[i].dstPduId, PduInfo);
            } else if (PduRRoute[i].dstModule == PDUR_CANTP) {
                CanTp_RxIndication(PduRRoute[i].dstPduId, PduInfo);
            }
            return;
        }
    }
    /* No route found: frame silently dropped */
    TRACE("[PduR] RX no route for canif_pdu=%u", RxPduId);
}

BufReq_ReturnType PduR_CanTpCopyTxData(PduIdType txNSduId, uint8 *dst, PduLengthType length) {
    return App_CanTpCopyTxData(txNSduId, dst, length);
}

void PduR_CanTpTxConfirmation(PduIdType txNSduId, Std_ReturnType result) {
    App_CanTpTxConfirmation(txNSduId, result);
}

BufReq_ReturnType PduR_CanTpStartOfReception(PduIdType rxNSduId, PduLengthType totalLength) {
    return App_CanTpStartOfReception(rxNSduId, totalLength);
}

BufReq_ReturnType PduR_CanTpCopyRxData(PduIdType rxNSduId, const uint8 *completeData, PduLengthType length) {
    return App_CanTpCopyRxData(rxNSduId, completeData, length);
}

void PduR_CanTpRxIndication(PduIdType rxNSduId, Std_ReturnType result) {
    App_CanTpRxIndication(rxNSduId, result);
}
