#include "Platform_Init.h"
#include "device_registers.h"

/* Global SysTick counter */
volatile uint32 g_sysTick_ms = 0;

void SysTick_Handler(void) {
    g_sysTick_ms++;
}

#if 0
static void SBC_Init(void);
#endif

void Platform_Init(void) {
    /* 1. WDOG Disable - Already disabled in SystemInit() at startup */

    /* 2. Clocks (SCG) - Enable FIRC Dividers (FIRCDIV1=1, FIRCDIV2=1)
     * FIRCDIV2 is the async clock source for peripherals (LPUART, etc.)
     * when PCC PCS=3. Default after reset is 0 (disabled) — MUST enable. */
    if ((SCG->FIRCCSR & SCG_FIRCCSR_LK_MASK) == 0U) {
        SCG->FIRCDIV = SCG_FIRCDIV_FIRCDIV1(1) | SCG_FIRCDIV_FIRCDIV2(1);
    }

    /* Ensure FIRC (48MHz) is active system clock with safe bus/slow dividers */
    if (((SCG->CSR & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT) != 3U) {
        SCG->RCCR = SCG_RCCR_SCS(3) | SCG_RCCR_DIVCORE(0) | SCG_RCCR_DIVBUS(0) | SCG_RCCR_DIVSLOW(1);
        while (((SCG->CSR & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT) != 3U) {} /* Wait for clock switch */
    }

    /* Configure SOSC (8 MHz external quartz crystal on S32K144 EVB) per Can_Ex */
    SCG->SOSCCSR &= ~SCG_SOSCCSR_SOSCEN_MASK;
    SCG->SOSCCFG = SCG_SOSCCFG_EREFS_MASK | SCG_SOSCCFG_HGO_MASK | SCG_SOSCCFG_RANGE(3); /* Range 3 = 8-40MHz, High Gain */
    SCG->SOSCDIV = SCG_SOSCDIV_SOSCDIV1(1) | SCG_SOSCDIV_SOSCDIV2(1); /* 8 MHz on SOSCDIV1/2 */
    SCG->SOSCCSR |= SCG_SOSCCSR_SOSCEN_MASK;
    uint32 sosc_timeout = 100000U;
    while (((SCG->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK) == 0U) && (sosc_timeout > 0U)) { sosc_timeout--; }

    /* 3. PCC (Peripheral Clock Controller) */
    /* Enable clocks for PORTC, PORTE, PORTD */
    PCC->PCCn[PCC_PORTC_INDEX] |= PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTD_INDEX] |= PCC_PCCn_CGC_MASK;

    /* Enable clock for FlexCAN0 */
    PCC->PCCn[PCC_FlexCAN0_INDEX] |= PCC_PCCn_CGC_MASK;

    /* Enable clock for LPUART1 from FIRC (PCS=3):
     * CGC MUST be cleared before modifying PCS field to avoid PCC BusFault */
    PCC->PCCn[PCC_LPUART1_INDEX] &= ~PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_LPUART1_INDEX] = PCC_PCCn_PCS(3) | PCC_PCCn_CGC_MASK;

    /* Enable clock for ADC0 from FIRC (PCS=3) */
    PCC->PCCn[PCC_ADC0_INDEX] &= ~PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_ADC0_INDEX] = PCC_PCCn_PCS(3) | PCC_PCCn_CGC_MASK;

    /* 4. PORT/Pin Muxing */
    /* RGB LEDs on PORTD: PTD0 (Blue), PTD15 (Red), PTD16 (Green) - MUX ALT1 (GPIO) */
    PORTD->PCR[0]  = PORT_PCR_MUX(1);
    PORTD->PCR[15] = PORT_PCR_MUX(1);
    PORTD->PCR[16] = PORT_PCR_MUX(1);
    PTD->PDDR |= (1U << 0) | (1U << 15) | (1U << 16);
    /* Active LOW: Set HIGH initially to turn all LEDs OFF */
    PTD->PSOR = (1U << 0) | (1U << 15) | (1U << 16);

    /* Potentiometer on PTC14: MUX ALT0 (ADC0_SE12 analog input) */
    PORTC->PCR[14] = PORT_PCR_MUX(0);

    /* CAN0_RX: PORTE4, MUX ALT5; CAN0_TX: PORTE5, MUX ALT5 */
    PORTE->PCR[4] = (PORTE->PCR[4] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(5);
    PORTE->PCR[5] = (PORTE->PCR[5] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(5);

    /* LPUART1_RX: PORTC6, MUX ALT2; LPUART1_TX: PORTC7, MUX ALT2 */
    PORTC->PCR[6] = (PORTC->PCR[6] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(2);
    PORTC->PCR[7] = (PORTC->PCR[7] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(2);

    /* 5. Initialize ADC0 (12-bit, div 4 = 12MHz, software trigger) */
    ADC0->CFG1 = ADC_CFG1_ADICLK(0) | ADC_CFG1_MODE(1) | ADC_CFG1_ADIV(2);
    ADC0->CFG2 = ADC_CFG2_SMPLTS(12);
    ADC0->SC2  = 0U;
    ADC0->SC3  = 0U;

    /* 6. SysTick Init (1ms tick at 48MHz) */
    S32_SysTick->RVR = 48000U - 1U;
    S32_SysTick->CVR = 0U;
    S32_SysTick->CSR = S32_SysTick_CSR_ENABLE_MASK | S32_SysTick_CSR_TICKINT_MASK | S32_SysTick_CSR_CLKSOURCE_MASK;

    /* 7. Do NOT touch SBC UJA1169 via SPI (matches Can_Ex).
     * S32K144 EVB boots in hardware Forced Normal Mode with watchdog DISABLED.
     * Sending SPI commands triggers UJA1169 software watchdog, which times out
     * after 512ms and shuts down the CAN transceiver physical layer! */
    /* SBC_Init(); */
}

/* Diagnostic variables for SBC UJA1169 */
volatile uint16 g_sbc_tx_norm = 0U;
volatile uint16 g_sbc_tx_can = 0U;
volatile uint16 g_sbc_rx_status = 0U;
volatile uint16 g_sbc_rx_can_status = 0U;
volatile uint16 g_sbc_id = 0U;

#if 0
static uint16 SBC_Transfer16(uint16 data) {
    /* Clear RX FIFO by reading any existing data */
    while ((LPSPI1->FSR & LPSPI_FSR_RXCOUNT_MASK) != 0U) {
        (void)LPSPI1->RDR;
    }
    
    /* Wait for TX FIFO not full (TDF flag) */
    uint32 timeout = 100000U;
    while (((LPSPI1->SR & LPSPI_SR_TDF_MASK) == 0U) && (timeout > 0U)) { timeout--; }
    
    /* Set TCR for 16-bit transfer on PCS3 (CPHA=1, CPOL=0, PRESCALE=2) per AN5413 */
    LPSPI1->TCR = 0x5300000FU;
    
    LPSPI1->SR |= LPSPI_SR_TCF_MASK; /* Clear flag */
    LPSPI1->TDR = data;
    
    /* Wait for transfer complete */
    timeout = 100000U;
    while (((LPSPI1->SR & LPSPI_SR_TCF_MASK) == 0U) && (timeout > 0U)) { timeout--; }
    LPSPI1->SR |= LPSPI_SR_TCF_MASK;

    /* Wait for RX data available (RDF flag) */
    timeout = 100000U;
    while (((LPSPI1->SR & LPSPI_SR_RDF_MASK) == 0U) && (timeout > 0U)) { timeout--; }

    uint16 rx = (uint16)LPSPI1->RDR;
    LPSPI1->SR |= LPSPI_SR_RDF_MASK;
    return rx;
}

/* Initialize UJA1169 CAN Transceiver via LPSPI1 */
static void SBC_Init(void) {
    /* 1. Enable PORTB clock */
    PCC->PCCn[PCC_PORTB_INDEX] |= PCC_PCCn_CGC_MASK;

    /* 2. Configure PTB14(SCK), PTB15(SIN), PTB16(SOUT), PTB17(PCS3) to ALT3 for LPSPI1 */
    PORTB->PCR[14] = (PORTB->PCR[14] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(3);
    PORTB->PCR[15] = (PORTB->PCR[15] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(3);
    PORTB->PCR[16] = (PORTB->PCR[16] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(3);
    PORTB->PCR[17] = (PORTB->PCR[17] & ~PORT_PCR_MUX_MASK) | PORT_PCR_MUX(3);

    /* 3. Configure LPSPI1 */
    PCC->PCCn[PCC_LPSPI1_INDEX] &= ~PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_LPSPI1_INDEX] = PCC_PCCn_PCS(3) | PCC_PCCn_CGC_MASK; /* FIRC 48MHz */

    LPSPI1->CR = 0; /* Disable for config */
    LPSPI1->IER = 0;
    LPSPI1->DER = 0;
    LPSPI1->CFGR1 = LPSPI_CFGR1_MASTER_MASK;

    /* AN5413 standard timing: SCKPCS=4, PCSSCK=9, DBT=8, SCKDIV=8 */
    LPSPI1->CCR = 0x04090808U;
    LPSPI1->FCR = 0;

    /* Enable module BEFORE writing to TCR */
    LPSPI1->CR |= LPSPI_CR_MEN_MASK;

    /* Small delay for PHY to stabilize */
    for (volatile uint32 i = 0; i < 100000; i++) {}

    /* 4. Read Identification Register (0x7E) -> Header 0xFD00 per AN5413 */
    g_sbc_id = SBC_Transfer16(0xFD00U);

    for (volatile uint32 i = 0; i < 20000; i++) {}

    /* 5. Write to UJA1169 Main Control Register (0x01) -> Mode = Normal (0x07)
     * Value: (0x01 << 9) | 0x07 = 0x0207
     */
    g_sbc_tx_norm = SBC_Transfer16(0x0207U);

    for (volatile uint32 i = 0; i < 20000; i++) {}

    /* 6. Write to UJA1169 CAN Control Register (0x20) -> CAN Active TX/RX (0x02)
     * Value: (0x20 << 9) | 0x02 = 0x4002
     */
    g_sbc_tx_can = SBC_Transfer16(0x4002U);

    for (volatile uint32 i = 0; i < 20000; i++) {}

    /* 7. Read back Main Status Register (0x03)
     * Header: (0x03 << 9) | RO(1) = 0x0700
     */
    g_sbc_rx_status = SBC_Transfer16(0x0700U);

    for (volatile uint32 i = 0; i < 20000; i++) {}

    /* 8. Read back CAN Control Register (0x20)
     * Header: (0x20 << 9) | RO(1) = 0x4100
     */
    g_sbc_rx_can_status = SBC_Transfer16(0x4100U);
}
#endif

/* ================================================================== */
/*  HARDWARE ACCESS: ADC & LED DRIVERS                                */
/* ================================================================== */

uint16 Platform_AdcReadPot(void) {
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    /* Trigger ADC conversion on channel 12 (ADC0_SE12 / PTC14) */
    ADC0->SC1[0] = ADC_SC1_ADCH(12);

    /* Poll conversion complete (COCO) flag with timeout */
    uint32 timeout = 10000U;
    while (((ADC0->SC1[0] & ADC_SC1_COCO_MASK) == 0U) && (timeout > 0U)) {
        timeout--;
    }
    return (uint16)(ADC0->R[0] & ADC_R_D_MASK);
#else
    return 0U;
#endif
}

void Platform_LedSet(uint8 red, uint8 green, uint8 blue) {
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    /* Active LOW: 1 = ON (PCOR pulls low), 0 = OFF (PSOR sets high) */
    if (red != 0U)   { PTD->PCOR = (1U << 15); } else { PTD->PSOR = (1U << 15); }
    if (green != 0U) { PTD->PCOR = (1U << 16); } else { PTD->PSOR = (1U << 16); }
    if (blue != 0U)  { PTD->PCOR = (1U << 0); }  else { PTD->PSOR = (1U << 0); }
#else
    (void)red; (void)green; (void)blue;
#endif
}

void Platform_LedToggleGreen(void) {
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    PTD->PTOR = (1U << 16);
#endif
}

void Platform_LedToggleBlue(void) {
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    PTD->PTOR = (1U << 0);
#endif
}

void Platform_LedToggleRed(void) {
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    PTD->PTOR = (1U << 15);
#endif
}
