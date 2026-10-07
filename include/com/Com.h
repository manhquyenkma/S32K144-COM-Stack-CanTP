#ifndef COM_H
#define COM_H

#include "ComStack_Types.h"
#include "com/Com_Cfg.h"

/* Rx notification callback — called from Com_MainFunction_Rx() (Task context)
 * when at least one signal in the Rx I-PDU has its Update Bit set.
 * Parameter: the local COM Rx IPdu ID (RxPduId). */
typedef void (*Com_RxCallbackType)(PduIdType RxPduId);

void Com_Init(void);
void Com_SetRxCallback(Com_RxCallbackType callback);
void Com_MainFunction_Tx(void);
void Com_MainFunction_Rx(void);
Std_ReturnType Com_SendSignal(uint16 SignalId, const void* SignalDataPtr);
Std_ReturnType Com_ReceiveSignal(uint16 SignalId, void* SignalDataPtr);
void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfo);
Std_ReturnType Com_ValidateConfig(void);

#endif /* COM_H */
