#include "candrv/Can_Cfg.h"
#include "device_registers.h"

/*
 * Controller configuration table.
 * CAN0: physically active on S32K144 EVB.
 * CAN1: declared for Multiple Controller model (Deliverable #12); not initialised.
 */
const Can_ControllerConfigType CanControllerConfig[CAN_NUM_CONTROLLERS] = {
    { .controllerId = 0, .baseAddr = CAN0_BASE, .clkSrcHz = 48000000U, .loopbackEnable = 0 },  /* Normal Mode for 2-board CAN */
    { .controllerId = 1, .baseAddr = CAN1_BASE, .clkSrcHz = 48000000U, .loopbackEnable = 0 }   /* model only */
};

/*
 * Hardware Object table — HOH IDs unique across the entire CanDrv instance (§33).
 *
 *   HOH 0 = HTH → CAN0, MB 0   (Tx)
 *   HOH 1 = HRH → CAN0, MB 1   (Rx, BasicCAN — accepts all CAN IDs via mask=0)
 *   HOH 2 = HTH → CAN1, MB 0   (Tx, model only)
 *   HOH 3 = HRH → CAN1, MB 1   (Rx, model only)
 */
const Can_HardwareObjectType CanHardwareObject[CAN_NUM_HOH] = {
    { .hohId = 0, .type = CAN_HOH_TRANSMIT, .controllerId = 0, .mbIndex = 0 },
    { .hohId = 1, .type = CAN_HOH_RECEIVE,  .controllerId = 0, .mbIndex = 1 },
    { .hohId = 2, .type = CAN_HOH_TRANSMIT, .controllerId = 1, .mbIndex = 0 },  /* model only */
    { .hohId = 3, .type = CAN_HOH_RECEIVE,  .controllerId = 1, .mbIndex = 1 }   /* model only */
};

const Can_ConfigType Can_Config = {
    .controllers = CanControllerConfig,
    .hohs        = CanHardwareObject
};
