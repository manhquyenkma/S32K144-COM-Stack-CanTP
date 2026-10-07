#include "com/Com_Cfg.h"

/*
 * COM Signal Configuration — Assignment v0.7 Layout
 *
 * KeepAlive I-PDU (CAN 0x100, Master → All Slaves):
 *   Byte 0: AliveCounter      (uint8, LE)
 *   Byte 1: KeepAliveRateLevel (uint8, LE)
 *
 * Slave1 Status I-PDU (CAN 0x201, Slave 1 → Master):
 *   Byte 0: Slave1Status      (uint8, LE, 0=NORMAL, 1=MASTER_LOST)
 *
 * Slave2 Status I-PDU (CAN 0x202, Slave 2 → Master):
 *   Byte 0: Slave2Status      (uint8, LE, 0=NORMAL, 1=MASTER_LOST)
 */
const Com_SignalConfigType ComSignal[COM_NUM_SIGNALS] = {
    /* TX Signals — KeepAlive (LE) */
    { .signalId = 0, .name = "AliveCounter",       .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 0, .slotLength = 8, .signalGroupId = 0 },
    { .signalId = 1, .name = "KeepAliveRateLevel",  .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 8, .slotLength = 8, .signalGroupId = 0 },

    /* TX Signals — Slave Status */
    { .signalId = 2, .name = "Slave1Status",        .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 0, .slotLength = 8, .signalGroupId = 1 },
    { .signalId = 3, .name = "Slave2Status",        .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 0, .slotLength = 8, .signalGroupId = 2 },

    /* RX Signals — KeepAlive Rx (LE) */
    { .signalId = 4, .name = "RxAliveCounter",      .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 0, .slotLength = 8, .signalGroupId = 3 },
    { .signalId = 5, .name = "RxKeepAliveRateLevel", .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 8, .slotLength = 8, .signalGroupId = 3 },

    /* RX Signals — Slave Status Rx */
    { .signalId = 6, .name = "RxSlave1Status",      .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 0, .slotLength = 8, .signalGroupId = 4 },
    { .signalId = 7, .name = "RxSlave2Status",      .dataType = COM_UINT8, .endianness = COM_LITTLE_ENDIAN, .slotStartBit = 0, .slotLength = 8, .signalGroupId = 5 },
};

const Com_SignalGroupConfigType ComSignalGroup[COM_NUM_SIGNAL_GROUPS] = {
    { .groupId = 0, .name = "KeepAliveTx",    .signalStartIdx = 0, .signalCount = 2, .ipduId = 0 },
    { .groupId = 1, .name = "Slave1StatusTx",  .signalStartIdx = 2, .signalCount = 1, .ipduId = 1 },
    { .groupId = 2, .name = "Slave2StatusTx",  .signalStartIdx = 3, .signalCount = 1, .ipduId = 2 },

    { .groupId = 3, .name = "KeepAliveRx",     .signalStartIdx = 4, .signalCount = 2, .ipduId = 3 },
    { .groupId = 4, .name = "Slave1StatusRx",   .signalStartIdx = 6, .signalCount = 1, .ipduId = 4 },
    { .groupId = 5, .name = "Slave2StatusRx",   .signalStartIdx = 7, .signalCount = 1, .ipduId = 5 },
};

const Com_IPduConfigType ComIPdu[COM_NUM_TX_IPDU + COM_NUM_RX_IPDU] = {
    /* TX PDUs — Assignment v0.7 §2.3 & §4 */
    { .ipduId = 0, .globalPduId = 0x0100, .direction = COM_TX, .length = 2, .signalGroupId = 0, .periodTicks = 10,  .initialOffsetTicks = 1, .maxRetries = 3 },  /* KeepAlive: CAN 0x100, 10ms */
    { .ipduId = 1, .globalPduId = 0x0201, .direction = COM_TX, .length = 1, .signalGroupId = 1, .periodTicks = 500, .initialOffsetTicks = 3, .maxRetries = 3 },  /* Slave1 Status: CAN 0x201, 500ms */
    { .ipduId = 2, .globalPduId = 0x0202, .direction = COM_TX, .length = 1, .signalGroupId = 2, .periodTicks = 500, .initialOffsetTicks = 5, .maxRetries = 3 },  /* Slave2 Status: CAN 0x202, 500ms */

    /* RX PDUs */
    { .ipduId = 3, .globalPduId = 0x0100, .direction = COM_RX, .length = 2, .signalGroupId = 3, .periodTicks = 0, .initialOffsetTicks = 0, .maxRetries = 0 },
    { .ipduId = 4, .globalPduId = 0x0201, .direction = COM_RX, .length = 1, .signalGroupId = 4, .periodTicks = 0, .initialOffsetTicks = 0, .maxRetries = 0 },
    { .ipduId = 5, .globalPduId = 0x0202, .direction = COM_RX, .length = 1, .signalGroupId = 5, .periodTicks = 0, .initialOffsetTicks = 0, .maxRetries = 0 },
};
