#include "app/Role.h"
#include "device_registers.h"
#include "trace/Trace.h"

/*
 * Role Selection via Hardware Button GPIO
 *
 * S32K144 EVB button wiring (NXP default):
 *   SW2 → PTC12, configured with internal pull-up → active LOW (0 when pressed)
 *   SW3 → PTC13, configured with internal pull-up → active LOW (0 when pressed)
 *
 * Role encoding:
 *   role = (SW3_pressed << 1) | SW2_pressed
 *
 *   Both released  (SW2=1, SW3=1) → role = (0<<1)|0 = 0   ROLE_0
 *   SW2 pressed    (SW2=0, SW3=1) → role = (0<<1)|1 = 1   ROLE_1
 *   SW3 pressed    (SW2=1, SW3=0) → role = (1<<1)|0 = 2   ROLE_2
 *   Both pressed   (SW2=0, SW3=0) → role = (1<<1)|1 = 3   ROLE_RESERVED → ROLE_0
 *
 * Platform precondition (satisfied by Platform_Init):
 *   - PORTC clock enabled via PCC_PORTC_INDEX
 *   - PTC12/PTC13 PCR must be configured: MUX=GPIO, pull-up enabled, no ISF
 */

static RoleType g_activeRole = ROLE_0_VEHICLE_TX;

/* GPIO register access (S32K144 has separate PORT mux and GPIO data registers):
 *   PORTC->PCR[n] — pin control (mux, pull)
 *   PTC->PDDR     — data direction register (0=input)
 *   PTC->PDIR     — pin data input register
 */

void Role_Init(void) {
    /* Configure PTC12 and PTC13 as GPIO input with pull-up enabled.
     * MUX=1 (GPIO), PE=1 (pull enable), PS=1 (pull select = pull-up), ISF=0 */
    PORTC->PCR[ROLE_SW2_PIN] = PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;
    PORTC->PCR[ROLE_SW3_PIN] = PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;

    /* Set as input (clear PDDR bits) */
    PTC->PDDR &= ~(1U << ROLE_SW2_PIN);
    PTC->PDDR &= ~(1U << ROLE_SW3_PIN);

    /* Small settling delay — let pull-ups stabilise before reading */
    for (volatile uint32 d = 0; d < 10000U; d++) { (void)d; }

    /* Read button states.
     * PDIR bit = 0 → pin pulled LOW → button PRESSED
     * PDIR bit = 1 → pin HIGH (released) */
    uint8 sw2_pressed = ((PTC->PDIR & (1U << ROLE_SW2_PIN)) == 0U) ? 1U : 0U;
    uint8 sw3_pressed = ((PTC->PDIR & (1U << ROLE_SW3_PIN)) == 0U) ? 1U : 0U;

    uint8 rawRole = (uint8)((sw3_pressed << 1U) | sw2_pressed);

    if (rawRole == (uint8)ROLE_RESERVED) {
        /* Both buttons pressed simultaneously — treat as Role 0 */
        g_activeRole = ROLE_0_VEHICLE_TX;
        TRACE("[Role] Both buttons pressed: defaulting to Role 0");
    } else {
        g_activeRole = (RoleType)rawRole;
    }

    TRACE("[Role] SW2=%u SW3=%u raw=%u -> %s",
          (uint32)sw2_pressed,
          (uint32)sw3_pressed,
          (uint32)rawRole,
          Role_GetName());
}

RoleType Role_Get(void) {
    return g_activeRole;
}

void Role_Set(RoleType role) {
    g_activeRole = role;
}

const char* Role_GetName(void) {
    switch (g_activeRole) {
        case ROLE_0_VEHICLE_TX: return "Role0 (Slave 2: Status TX 0x202 | KeepAlive RX)";
        case ROLE_1_ENGINE_TX:  return "Role1 (Master: KeepAlive TX 0x100 | Status RX | CanTP TX)";
        case ROLE_2_BODY_TX:    return "Role2 (Slave 1: Status TX 0x201 | KeepAlive RX | CanTP RX)";
        default:                return "Role? (unknown)";
    }
}
