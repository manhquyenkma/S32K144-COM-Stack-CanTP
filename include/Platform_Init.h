#ifndef PLATFORM_INIT_H
#define PLATFORM_INIT_H

#include "Std_Types.h"

extern volatile uint32 g_sysTick_ms;

/* SBC UJA1169 Diagnostics (read back via LPSPI1) */
extern volatile uint16 g_sbc_tx_norm;
extern volatile uint16 g_sbc_tx_can;
extern volatile uint16 g_sbc_rx_status;
extern volatile uint16 g_sbc_rx_can_status;
extern volatile uint16 g_sbc_id;

void Platform_Init(void);

/* Potentiometer ADC reader (PTC14 / ADC0_SE12) */
uint16 Platform_AdcReadPot(void);

/* RGB LED control functions (PTD15=Red, PTD16=Green, PTD0=Blue - Active LOW) */
void Platform_LedSet(uint8 red, uint8 green, uint8 blue);
void Platform_LedToggleGreen(void);
void Platform_LedToggleBlue(void);
void Platform_LedToggleRed(void);

#endif /* PLATFORM_INIT_H */
