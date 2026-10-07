#ifndef PDUR_H
#define PDUR_H

#include "ComStack_Types.h"
#include "pdur/PduR_Cfg.h"

void           PduR_Init(void);
Std_ReturnType PduR_ComTransmit(PduIdType TxPduId, const PduInfoType* PduInfo);
Std_ReturnType PduR_CanTpTransmit(PduIdType TxPduId, const PduInfoType* PduInfo);
void           PduR_CanIfRxIndication(PduIdType RxPduId, const PduInfoType* PduInfo);

/* Transport Protocol Buffer & Confirmation Interfaces */
BufReq_ReturnType PduR_CanTpCopyTxData(PduIdType txNSduId, uint8 *dst, PduLengthType length);
void              PduR_CanTpTxConfirmation(PduIdType txNSduId, Std_ReturnType result);
BufReq_ReturnType PduR_CanTpStartOfReception(PduIdType rxNSduId, PduLengthType totalLength);
BufReq_ReturnType PduR_CanTpCopyRxData(PduIdType rxNSduId, const uint8 *completeData, PduLengthType length);
void              PduR_CanTpRxIndication(PduIdType rxNSduId, Std_ReturnType result);

#endif /* PDUR_H */
