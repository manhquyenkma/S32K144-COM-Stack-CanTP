#ifndef CAN_CFG_H
#define CAN_CFG_H

#include "Std_Types.h"

/*
 * Multiple Controller Model (Assignment Deliverable #12 / §20)
 *
 * This training configuration declares 2 CAN controllers to demonstrate the
 * Multiple Controller model required by the assignment. On this hardware target
 * (S32K144 evaluation board) only CAN0 is physically wired, so Can_Init()
 * initialises CAN0 only. The configuration model is architecturally complete.
 *
 * HOH namespace (unique within one CanDrv instance, §33):
 *
 *   CAN0  ──  HTH 0  (Tx)
 *         ──  HRH 1  (Rx)
 *
 *   CAN1  ──  HTH 2  (Tx)   ← config-only, not initialised at runtime
 *         ──  HRH 3  (Rx)   ← config-only, not initialised at runtime
 *
 * Resolution: HOH → HardwareObject → Controller (unique, no ControllerId needed)
 */

#define CAN_NUM_CONTROLLERS 2   /* CAN0 (active) + CAN1 (config model) */
#define CAN_NUM_HOH         4   /* HTH0, HRH1, HTH2, HRH3              */
#define CAN_NUM_HTH         2   /* HOH IDs 0 and 2                     */
#define CAN_NUM_HRH         2   /* HOH IDs 1 and 3                     */

/* Active HOH handles (used at runtime on CAN0) */
#define CAN_HTH_CAN0        0u
#define CAN_HRH_CAN0        1u

/* Model-only HOH handles (CAN1, not initialised) */
#define CAN_HTH_CAN1        2u
#define CAN_HRH_CAN1        3u

typedef enum {
    CAN_HOH_TRANSMIT,
    CAN_HOH_RECEIVE
} Can_HohType;

typedef struct {
    uint8 controllerId;
    uint32 baseAddr;
    uint32 clkSrcHz;
    uint8 loopbackEnable;   /* 0 = Normal Mode (2-board CAN bus), 1 = Loopback (self-test) */
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
