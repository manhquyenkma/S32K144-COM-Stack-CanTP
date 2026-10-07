#ifndef ROLE_H
#define ROLE_H

#include "Std_Types.h"

/*
 * Hardware Role Switching (Project Requirement Part I §1)
 *
 * One firmware image contains all 3 roles.
 * Role is selected at startup by reading 2 physical buttons on the board:
 *
 *   role = (SW3_state << 1) | SW2_state
 *
 *   SW2 = PTC12 (NXP S32K144 EVB, active LOW)
 *   SW3 = PTC13 (NXP S32K144 EVB, active LOW)
 *
 *   Button state: 0 = pressed (pulled to GND), 1 = released (pull-up)
 *
 *   Role 0 (SW2=0, SW3=0): VehicleStatus TX  | EngineStatus RX  | no CanTP
 *   Role 1 (SW2=1, SW3=0): EngineStatus TX   | BodyStatus RX    | CanTP TX (DTC sender)
 *   Role 2 (SW2=0, SW3=1): BodyStatus TX     | VehicleStatus RX | CanTP RX (DTC receiver)
 *   Role 3 (SW2=1, SW3=1): reserved (treated as Role 0)
 *
 * No firmware reflash is needed — power cycle + button state selects the role.
 */

typedef enum {
    ROLE_0_VEHICLE_TX = 0,   /* Slave 2: TX SlaveStatus 0x202 | RX KeepAlive 0x100 */
    ROLE_1_ENGINE_TX  = 1,   /* Master:  TX KeepAlive 0x100   | RX Status 0x201/0x202 | CanTP Tx */
    ROLE_2_BODY_TX    = 2,   /* Slave 1: TX SlaveStatus 0x201 | RX KeepAlive 0x100   | CanTP Rx */
    ROLE_RESERVED     = 3    /* Both buttons pressed → default to Role 0 */
} RoleType;

/* GPIO pin definitions (S32K144 EVB, NXP defaults)
 * PORTC = PORT mux control (PORT_Type*), PTC = GPIO data (GPIO_Type*) */
#define ROLE_SW2_PIN         12U        /* PTC12 / PORTC[12] */
#define ROLE_SW3_PIN         13U        /* PTC13 / PORTC[13] */

/* Read button GPIO and determine role. Call once at startup before BSW init. */
void     Role_Init(void);

/* Return the active role selected at last Role_Init(). */
RoleType Role_Get(void);

/* Manually set active role (useful after unit test simulations) */
void     Role_Set(RoleType role);

/* Human-readable role name for trace/debug. */
const char* Role_GetName(void);

#endif /* ROLE_H */
