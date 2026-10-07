#ifndef CANIF_CFG_H
#define CANIF_CFG_H

#include "ComStack_Types.h"
#include "candrv/Can.h"

#define CANIF_NUM_TX_PDU 5   /* 3 COM signal PDUs + 1 CanTP Data Tx + 1 CanTP FC Tx */
#define CANIF_NUM_RX_PDU 5   /* 3 COM signal PDUs + 1 CanTP Data Rx + 1 CanTP FC Rx */

typedef struct {
    PduIdType txPduId;
    Can_IdType canId;
    Can_HwHandleType hthRef;
    PduIdType globalPduId;
    uint8 dlc;
} CanIf_TxPduConfigType;

typedef struct {
    PduIdType rxPduId;
    Can_IdType canId;
    Can_HwHandleType hrhRef;
    PduIdType globalPduId;
    PduIdType upperPduId;
} CanIf_RxPduConfigType;

extern const CanIf_TxPduConfigType CanIfTxPdu[CANIF_NUM_TX_PDU];
extern const CanIf_RxPduConfigType CanIfRxPdu[CANIF_NUM_RX_PDU];

#endif /* CANIF_CFG_H */
