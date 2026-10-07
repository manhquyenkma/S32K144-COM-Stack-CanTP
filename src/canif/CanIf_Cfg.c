#include "canif/CanIf_Cfg.h"

/*
 * CanIf Tx PDU configuration — Assignment v0.7 CAN IDs.
 *
 *   PDU 0: KeepAlive         (CAN ID 0x100, Master → All Slaves)
 *   PDU 1: Slave 1 Status    (CAN ID 0x201, Slave 1 → Master)
 *   PDU 2: Slave 2 Status    (CAN ID 0x202, Slave 2 → Master)
 *   PDU 3: CanTP Data Tx     (CAN ID 0x730, ISO-TP convention)
 *   PDU 4: CanTP FC Tx       (CAN ID 0x731, ISO-TP convention)
 */
const CanIf_TxPduConfigType CanIfTxPdu[CANIF_NUM_TX_PDU] = {
    { .txPduId=0, .canId=0x100, .hthRef=0, .globalPduId=0x0100, .dlc=8 },  /* KeepAlive */
    { .txPduId=1, .canId=0x201, .hthRef=0, .globalPduId=0x0201, .dlc=8 },  /* Slave 1 Status */
    { .txPduId=2, .canId=0x202, .hthRef=0, .globalPduId=0x0202, .dlc=8 },  /* Slave 2 Status */
    { .txPduId=3, .canId=0x730, .hthRef=0, .globalPduId=0x0730, .dlc=8 },  /* CanTP Data Tx */
    { .txPduId=4, .canId=0x731, .hthRef=0, .globalPduId=0x0731, .dlc=8 }   /* CanTP FC Tx */
};

/*
 * CanIf Rx PDU configuration.
 *
 *   PDU 0: KeepAlive Rx      (CAN ID 0x100)
 *   PDU 1: Slave 1 Status Rx (CAN ID 0x201)
 *   PDU 2: Slave 2 Status Rx (CAN ID 0x202)
 *   PDU 3: CanTP Data Rx     (CAN ID 0x730)
 *   PDU 4: CanTP FC Rx       (CAN ID 0x731)
 */
const CanIf_RxPduConfigType CanIfRxPdu[CANIF_NUM_RX_PDU] = {
    { .rxPduId=0, .canId=0x100, .hrhRef=1, .globalPduId=0x0100, .upperPduId=3 },  /* KeepAlive */
    { .rxPduId=1, .canId=0x201, .hrhRef=1, .globalPduId=0x0201, .upperPduId=4 },  /* Slave 1 Status */
    { .rxPduId=2, .canId=0x202, .hrhRef=1, .globalPduId=0x0202, .upperPduId=5 },  /* Slave 2 Status */
    { .rxPduId=3, .canId=0x730, .hrhRef=1, .globalPduId=0x0730, .upperPduId=3 },  /* CanTP Data Rx */
    { .rxPduId=4, .canId=0x731, .hrhRef=1, .globalPduId=0x0731, .upperPduId=4 }   /* CanTP FC Rx */
};
