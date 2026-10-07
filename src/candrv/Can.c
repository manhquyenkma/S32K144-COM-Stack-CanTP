#include "candrv/Can.h"
#include "canif/CanIf.h"
#include "device_registers.h"
#include "trace/Trace.h"
#include "Platform_Init.h"
#include <stddef.h>

static const Can_ConfigType* Can_GlobalConfig = NULL;

void Can_Init(const Can_ConfigType* Config) {
    Can_GlobalConfig = Config;
    
    /* 1. Enter Module Disable Mode (MCR[MDIS]=1).
     * S32K144 Reference Manual §53.4.1.3:
     * "The CLKSRC bit can be written only in Disable mode (MCR[MDIS]=1). In Freeze mode, attempts to write to this bit are ignored."
     */
    CAN0->MCR |= CAN_MCR_MDIS_MASK;
    
    /* 2. Select Clock Source: SOSCDIV2_CLK (8 MHz external crystal) per Can_Ex */
    CAN0->CTRL1 &= ~CAN_CTRL1_CLKSRC_MASK;
    
    /* 3. Re-enable module (clear MDIS) */
    CAN0->MCR &= ~CAN_MCR_MDIS_MASK;
    
    /* Wait for Low Power Mode Exit (LPMACK bit) with timeout */
    uint32 timeout = 100000U;
    while (((CAN0->MCR & CAN_MCR_LPMACK_MASK) != 0U) && (timeout > 0U)) { timeout--; }
    
    if (timeout == 0U) {
        TRACE("[CanDrv] ERR: Timeout waiting for LPMACK! SOSC clock missing?");
        Trace_Flush();
        for(;;){}
    }
    
    /* 4. Soft Reset to guarantee clean controller state */
    CAN0->MCR |= CAN_MCR_SOFTRST_MASK;
    timeout = 100000U;
    while (((CAN0->MCR & CAN_MCR_SOFTRST_MASK) != 0U) && (timeout > 0U)) { timeout--; }
    
    /* 5. Enter Freeze Mode */
    CAN0->MCR |= (CAN_MCR_HALT_MASK | CAN_MCR_FRZ_MASK);
    timeout = 100000U;
    while (((CAN0->MCR & CAN_MCR_FRZACK_MASK) == 0U) && (timeout > 0U)) { timeout--; }
    
    if (timeout == 0U) {
        TRACE("[CanDrv] ERR: Timeout waiting for FRZACK! CAN clock issue?");
        Trace_Flush();
        for(;;){}
    }
    
    /* Configure bit timing for 500kbit/s with SOSC 8 MHz:
     *   CLKSRC = 0 (SOSCDIV2_CLK = 8 MHz crystal oscillator — high accuracy)
     *   PRESDIV = 0 (divide by 1) -> 8 MHz Tq clock
     *   PROPSEG = 6 (7 Tq)
     *   PSEG1   = 5 (6 Tq)
     *   PSEG2   = 1 (2 Tq)
     *   RJW     = 0 (1 Tq)
     *   Total   = 1 + 7 + 6 + 2 = 16 Tq -> 8 MHz / 16 = 500.000 kHz
     */
    uint32 ctrl1Val = CAN_CTRL1_PRESDIV(0)  |    /* Prescaler: divide by 1 -> 8 MHz Tq */
                      CAN_CTRL1_PROPSEG(6)  |
                      CAN_CTRL1_PSEG1(5)    |
                      CAN_CTRL1_PSEG2(1)    |
                      CAN_CTRL1_RJW(0);

    if (Can_GlobalConfig->controllers[0].loopbackEnable != 0U) {
        ctrl1Val |= CAN_CTRL1_LPB_MASK;
        TRACE("[CanDrv] CAN0 Loopback Mode ENABLED (single-board self-test)");
    } else {
        TRACE("[CanDrv] CAN0 Normal Mode (2-board CAN bus, SOSC 8MHz -> 500kbps)");
    }
    CAN0->CTRL1 = ctrl1Val;

    /* Set MAXMB = 15 (activate up to MB15) and enable individual RX masking (IRMQ) per Can_Ex */
    CAN0->MCR |= CAN_MCR_MAXMB(15) | CAN_MCR_IRMQ_MASK;
                  
    /* Clear all Message Buffer RAM (128 words = 32 MBs) */
    for (int i = 0; i < 128; i++) {
        CAN0->RAMn[i] = 0U;
    }
    
    /* Rx MB1..MB8 config: accept all standard frames (BasicCAN) with 8-deep hardware buffer */
    CAN0->RXMGMASK = 0;             /* Global mask (legacy) */
    for (uint8 mb = 1U; mb <= 8U; mb++) {
        CAN0->RXIMR[mb] = 0x00000000UL;  /* Individual mask: accept all (0=don't care) */
        CAN0->RAMn[mb*4] = 0x04000000U;  /* CODE=0x4 (EMPTY), ready to receive */
    }

    /* Tx MB0 config: inactive, DLC=8 */
    CAN0->RAMn[0*4] = 0x08080000U; /* CODE=0x8 (INACTIVE), DLC=8 */
    
    /* Exit Freeze Mode */
    CAN0->MCR &= ~(CAN_MCR_HALT_MASK | CAN_MCR_FRZ_MASK);
    timeout = 100000U;
    while (((CAN0->MCR & CAN_MCR_FRZACK_MASK) != 0U) && (timeout > 0U)) { timeout--; }
    
    /* Wait for module ready with bounded timeout (prevents hang if bus is unpowered/idle) */
    uint32 notrdy_timeout = 100000U;
    while (((CAN0->MCR & CAN_MCR_NOTRDY_MASK) != 0U) && (notrdy_timeout > 0U)) {
        notrdy_timeout--;
    }

    uint8 clkSrcVal = (CAN0->CTRL1 & CAN_CTRL1_CLKSRC_MASK) ? 1 : 0;
    uint8 synchVal  = (CAN0->ESR1 & CAN_ESR1_SYNCH_MASK) ? 1 : 0;
    TRACE("[CanDrv] CAN0 Init Done: CLKSRC=%u (0=SOSC8M), ESR1=0x%08lX (SYNCH=%u)",
          clkSrcVal, (unsigned long)CAN0->ESR1, synchVal);
}

Can_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType* PduInfo) {
    if (Hth >= CAN_NUM_HOH) return CAN_NOT_OK;
    const Can_HardwareObjectType* hoh = &Can_GlobalConfig->hohs[Hth];
    if (hoh->type != CAN_HOH_TRANSMIT) return CAN_NOT_OK;

    /* HOHs on CAN1 are model-only — hardware not initialised on this target */
    if (hoh->controllerId != 0U) {
        TRACE("[CanDrv] WARN: Write to model-only HTH %u (CAN1) — NOT_OK", Hth);
        return CAN_NOT_OK;
    }

    uint8 mbIdx = hoh->mbIndex;
    uint32 cs = CAN0->RAMn[mbIdx*4];
    
    /* Verify MB CODE is available: INACTIVE (0x8), UNUSED (0x0), or ABORT (0x9) */
    uint32 code = cs & 0x0F000000U;
    if (code != 0x08000000U && code != 0x00000000U && code != 0x09000000U) {
        /* Mailbox is busy transmitting.
         * If this is a CanTp stream frame (ID >= 0x700) and MB0 holds a routine COM frame (ID < 0x700),
         * preempt the COM frame so CanTp streaming is never blocked. */
        uint32 curId = (CAN0->RAMn[mbIdx*4 + 1] >> 18) & 0x7FFU;
        if (PduInfo->id >= 0x700U && curId < 0x700U) {
            CAN0->RAMn[mbIdx*4] = 0x08000000U;
            CAN0->IFLAG1 = (1U << mbIdx);
            TRACE("[CanDrv] Preempted COM 0x%X for CanTp 0x%X", curId, PduInfo->id);
        } else {
            return CAN_BUSY;
        }
    }
    
    /* Copy CAN ID into MB ID field (Standard ID is bits 28-18) */
    CAN0->RAMn[mbIdx*4 + 1] = (PduInfo->id & 0x7FF) << 18;
    
    /* Copy exactly all 8 DLC payload bytes from PduInfo->sdu into physical MB data words */
    uint32 word0 = (PduInfo->sdu[0] << 24) | (PduInfo->sdu[1] << 16) | (PduInfo->sdu[2] << 8) | PduInfo->sdu[3];
    uint32 word1 = (PduInfo->sdu[4] << 24) | (PduInfo->sdu[5] << 16) | (PduInfo->sdu[6] << 8) | PduInfo->sdu[7];
    CAN0->RAMn[mbIdx*4 + 2] = word0;
    CAN0->RAMn[mbIdx*4 + 3] = word1;
    
    /* Only after the MB contents are completely prepared, set the MB CODE to TX DATA (0xC) */
    CAN0->RAMn[mbIdx*4] = 0x0C000000 | (8 << 16); /* Force DLC to 8 */
    
    return CAN_OK;
}

extern volatile uint32 g_sysTick_ms;
static uint32 mb0_write_start_ms = 0U;

void Can_MainFunction_Write(void) {
    /* 1. Poll IFLAG1 for Tx MB completion (MB0) */
    if (CAN0->IFLAG1 & (1U << 0)) {
        CAN0->IFLAG1 = (1U << 0); /* Clear flag by writing 1 */
        mb0_write_start_ms = 0U;
        CanIf_TxConfirmation(0);
        return;
    }

    /* 2. Check if MB0 is stuck in TX DATA without completing for > 50ms (no ACK) */
    uint32 cs0 = CAN0->RAMn[0*4];
    if ((cs0 & 0x0F000000U) == 0x0C000000U) {
        if (mb0_write_start_ms == 0U) {
            mb0_write_start_ms = g_sysTick_ms;
        } else if ((uint32)(g_sysTick_ms - mb0_write_start_ms) > 50U) {
            uint32 stuckId = (CAN0->RAMn[0*4 + 1] >> 18) & 0x7FFU;
            uint32 esr1 = CAN0->ESR1;
            uint32 tec = (CAN0->ECR & CAN_ECR_TXERRCNT_MASK) >> CAN_ECR_TXERRCNT_SHIFT;
            uint32 rec = (CAN0->ECR & CAN_ECR_RXERRCNT_MASK) >> CAN_ECR_RXERRCNT_SHIFT;
            uint32 flt = (esr1 & CAN_ESR1_FLTCONF_MASK) >> CAN_ESR1_FLTCONF_SHIFT;

            /* Force abort to INACTIVE so mailbox is never permanently bricked */
            CAN0->RAMn[0*4] = 0x08080000U;
            CAN0->IFLAG1 = (1U << 0);
            mb0_write_start_ms = 0U;
            TRACE("[CanDrv] Stuck MB0: ID=0x%X (TEC=%u REC=%u FLT=%u ESR1=0x%08lX SBC_ID=0x%04X SBC_STA=0x%04X)",
                  stuckId, tec, rec, flt, (unsigned long)esr1, g_sbc_id, g_sbc_rx_status);

            /* CRITICAL: Notify CanIf that the frame failed so CanTp can release txPduPending.
             * Without this, CanTp stays locked in TX_WAIT_CONFIRM for N_As+300ms = 400ms! */
            CanIf_TxConfirmation(0);
        }
    } else {
        mb0_write_start_ms = 0U;
    }

    /* 3. Auto-recover from Bus-Off: enable automatic recovery in MCR.
     * FlexCAN will automatically re-enter Normal mode after 128*11 recessive bits
     * per CAN standard (ISO 11898-1) when BOFFREC is clear (default). */
    uint32 esr1_now = CAN0->ESR1;
    if (esr1_now & CAN_ESR1_BOFFINT_MASK) {
        CAN0->ESR1 = CAN_ESR1_BOFFINT_MASK;  /* Clear Bus-Off interrupt flag (w1c) */
        CAN0->RAMn[0*4] = 0x08080000U;       /* Release MB0 to INACTIVE */
        for (uint8 mb = 1U; mb <= 8U; mb++) {
            CAN0->RAMn[mb*4] = 0x04000000U;   /* Re-arm MB1..MB8 for Rx */
        }
        mb0_write_start_ms = 0U;
        TRACE("[CanDrv] Bus-Off detected & cleared, FlexCAN auto-recovering");
    }
}

static volatile boolean s_canReadInProgress = FALSE;

void Can_MainFunction_Read(void) {
    if (s_canReadInProgress) {
        return; /* Prevent reentrant calls during nested UART transmission */
    }
    s_canReadInProgress = TRUE;

    /* Poll IFLAG1 for Rx MB completion across all configured Rx MBs (MB1..MB8) */
    uint32 iflag = CAN0->IFLAG1;
    for (uint8 mb = 1U; mb <= 8U; mb++) {
        if (iflag & (1U << mb)) {
            uint32 cs = CAN0->RAMn[mb*4];
            uint32 id_reg = CAN0->RAMn[mb*4 + 1];
            uint32 word0 = CAN0->RAMn[mb*4 + 2];
            uint32 word1 = CAN0->RAMn[mb*4 + 3];
            
            /* Read the timer to unlock the MB (FlexCAN requirement) */
            (void)CAN0->TIMER;
            
            CAN0->IFLAG1 = (1U << mb); /* Clear flag by writing 1 */
            
            Can_RxPduType rxPdu;
            rxPdu.id = (id_reg >> 18) & 0x7FFU;
            rxPdu.length = (cs >> 16) & 0xFU;
            rxPdu.sdu[0] = (uint8)((word0 >> 24) & 0xFFU);
            rxPdu.sdu[1] = (uint8)((word0 >> 16) & 0xFFU);
            rxPdu.sdu[2] = (uint8)((word0 >> 8)  & 0xFFU);
            rxPdu.sdu[3] = (uint8)(word0 & 0xFFU);
            rxPdu.sdu[4] = (uint8)((word1 >> 24) & 0xFFU);
            rxPdu.sdu[5] = (uint8)((word1 >> 16) & 0xFFU);
            rxPdu.sdu[6] = (uint8)((word1 >> 8)  & 0xFFU);
            rxPdu.sdu[7] = (uint8)(word1 & 0xFFU);
            
            /* Re-arm Rx MB to EMPTY so it can receive subsequent frames */
            CAN0->RAMn[mb*4] = 0x04000000U; /* CODE = EMPTY (0x4) */
            
            CanIf_RxIndication(1, &rxPdu);
        }
    }

    s_canReadInProgress = FALSE;
}
