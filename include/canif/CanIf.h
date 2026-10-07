#ifndef CANIF_H
#define CANIF_H

#include "ComStack_Types.h"
#include "candrv/Can.h"
#include "canif/CanIf_Cfg.h"

void CanIf_Init(void);
Std_ReturnType CanIf_Transmit(PduIdType TxPduId, const PduInfoType* PduInfo);
void CanIf_TxConfirmation(Can_HwHandleType Hth);
void CanIf_RxIndication(Can_HwHandleType Hrh, const Can_RxPduType* RxPdu);

typedef Std_ReturnType (*CanIf_TransmitHookType)(PduIdType TxPduId, const PduInfoType* PduInfo);
extern CanIf_TransmitHookType CanIf_TransmitHook;

#endif /* CANIF_H */
