#ifndef CAN_CFG_H
#define CAN_CFG_H

#include "Std_Types.h"

#define CAN_NUM_CONTROLLERS 1
#define CAN_NUM_HOH 2
#define CAN_NUM_HTH 1
#define CAN_NUM_HRH 1

typedef enum {
    CAN_HOH_TRANSMIT,
    CAN_HOH_RECEIVE
} Can_HohType;

typedef struct {
    uint8 controllerId;
    uint32 baseAddr;
    uint32 clkSrcHz;
} Can_ControllerConfigType;

typedef struct {
    uint16 hohId;
    Can_HohType type;
    uint8 controllerId;
    uint8 mbIndex;
} Can_HardwareObjectType;

typedef struct {
    const Can_ControllerConfigType* controllers;
    const Can_HardwareObjectType* hohs;
} Can_ConfigType;

extern const Can_ControllerConfigType CanControllerConfig[CAN_NUM_CONTROLLERS];
extern const Can_HardwareObjectType CanHardwareObject[CAN_NUM_HOH];
extern const Can_ConfigType Can_Config;

#endif /* CAN_CFG_H */
