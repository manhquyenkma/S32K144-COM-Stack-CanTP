#ifndef CAN_H
#define CAN_H

#include "ComStack_Types.h"
#include "candrv/Can_Cfg.h"

typedef uint16 Can_HwHandleType;
typedef uint32 Can_IdType;

typedef struct {
    PduIdType swPduHandle;
    uint8 length;
    Can_IdType id;
    uint8* sdu;
} Can_PduType;

typedef struct {
    Can_IdType id;
    uint8 length;
    uint8 sdu[8];
} Can_RxPduType;

typedef enum {
    CAN_OK = 0,
    CAN_NOT_OK,
    CAN_BUSY
} Can_ReturnType;

void Can_Init(const Can_ConfigType* Config);
Can_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType* PduInfo);
void Can_MainFunction_Write(void);
void Can_MainFunction_Read(void);

#endif /* CAN_H */
