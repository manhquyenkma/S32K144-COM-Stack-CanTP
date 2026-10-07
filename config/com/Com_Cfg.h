#ifndef COM_CFG_H
#define COM_CFG_H

#include "ComStack_Types.h"

#define COM_NUM_TX_IPDU 3
#define COM_NUM_RX_IPDU 3
#define COM_NUM_SIGNALS 16 /* 8 TX + 8 RX */
#define COM_NUM_SIGNAL_GROUPS 6 /* 3 TX + 3 RX */

typedef enum {
    COM_TX,
    COM_RX
} Com_DirectionType;

typedef enum {
    COM_UINT8,
    COM_UINT16,
    COM_UINT32
} Com_DataType;

typedef enum {
    COM_LITTLE_ENDIAN,
    COM_BIG_ENDIAN
} Com_EndiannessType;

typedef struct {
    uint16 signalId;
    const char* name;
    Com_DataType dataType;
    Com_EndiannessType endianness;
    uint16 slotStartBit;
    uint8 slotLength;
    uint16 signalGroupId;
} Com_SignalConfigType;

typedef struct {
    uint16 groupId;
    const char* name;
    uint16 signalStartIdx;
    uint8 signalCount;
    PduIdType ipduId;
} Com_SignalGroupConfigType;

typedef struct {
    PduIdType ipduId;
    PduIdType globalPduId;
    Com_DirectionType direction;
    uint8 length; /* Logical length */
    uint16 signalGroupId;
    uint16 periodTicks;
    uint16 initialOffsetTicks;
    uint8 maxRetries;
} Com_IPduConfigType;

extern const Com_SignalConfigType ComSignal[COM_NUM_SIGNALS];
extern const Com_SignalGroupConfigType ComSignalGroup[COM_NUM_SIGNAL_GROUPS];
extern const Com_IPduConfigType ComIPdu[COM_NUM_TX_IPDU + COM_NUM_RX_IPDU];

#endif /* COM_CFG_H */
