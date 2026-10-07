#include "app/UartRxQueue.h"
#include "trace/Trace.h"
#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
#include "device_registers.h"
#endif
#include <string.h>

/*
 * UART Rx Queue — Circular buffer implementation for image data reception.
 * Assignment §5.2: "queue shall not overwrite unread data"
 *
 * State machine:
 *   IDLE → receive byte 0 of length header → WAIT_LEN_HI
 *   WAIT_LEN_HI → receive byte 1 of length header → RECEIVING (imageLength known)
 *   RECEIVING → push payload bytes into circular buffer until all received
 *   → when all bytes popped by App_ImageSender → back to IDLE
 */

/* Circular buffer */
static uint8  rxBuf[UART_RX_QUEUE_SIZE];
static uint16 rxHead = 0U;   /* Write pointer (producer: UartRxQueue_Poll) */
static uint16 rxTail = 0U;   /* Read pointer  (consumer: UartRxQueue_Pop)  */

/* Image framing state */
static UartImgStateType imgState = UART_IMG_IDLE;
static uint32 imgTotalLength  = 0U;   /* From header (uint16 or uint32) */
static uint32 imgBytesRxd     = 0U;   /* Payload bytes pushed into queue */
static uint32 imgBytesPopped  = 0U;   /* Payload bytes consumed by App */
static uint8  imgLenLowByte   = 0U;   /* Temp storage for LE low byte */
static uint32 imgExtLen       = 0U;   /* Temp storage for 32-bit extended length */

/* ================================================================== */
/*  INTERNAL HELPERS                                                   */
/* ================================================================== */

static uint16 RingUsed(void) {
    if (rxHead >= rxTail) {
        return rxHead - rxTail;
    }
    return UART_RX_QUEUE_SIZE - rxTail + rxHead;
}

static uint16 RingFree(void) {
    return (UART_RX_QUEUE_SIZE - 1U) - RingUsed();  /* Keep 1 byte empty */
}

static void RingPush(uint8 byte) {
    rxBuf[rxHead] = byte;
    rxHead = (rxHead + 1U) % UART_RX_QUEUE_SIZE;
}

static uint8 RingPop(void) {
    uint8 byte = rxBuf[rxTail];
    rxTail = (rxTail + 1U) % UART_RX_QUEUE_SIZE;
    return byte;
}

/* ================================================================== */
/*  PUBLIC API                                                         */
/* ================================================================== */

void UartRxQueue_Init(void) {
    rxHead = 0U;
    rxTail = 0U;
    imgState = UART_IMG_IDLE;
    imgTotalLength = 0U;
    imgBytesRxd = 0U;
    imgBytesPopped = 0U;
    imgLenLowByte = 0U;
}

extern volatile uint32 g_sysTick_ms;
static uint32 lastRxTime_ms = 0U;

void UartRxQueue_PushByte(uint8 byte) {
    /* If a previous transfer was stalled (e.g. PC aborted) and new byte arrives after >1500ms silence, reset */
    if (imgState != UART_IMG_IDLE && (uint32)(g_sysTick_ms - lastRxTime_ms) > 1500U) {
        TRACE("[UART_Q] Stale session timed out (>1500ms silence) -> reset to IDLE");
        imgState = UART_IMG_IDLE;
        imgTotalLength = 0U;
        imgBytesRxd = 0U;
        imgBytesPopped = 0U;
        rxHead = 0U;
        rxTail = 0U;
    }
    lastRxTime_ms = g_sysTick_ms;

    switch (imgState) {
    case UART_IMG_IDLE:
        /* First byte of [ImageLength:uint16_LE] header (low byte) */
        imgLenLowByte = byte;
        imgState = UART_IMG_WAIT_LEN_HI;
        break;

    case UART_IMG_WAIT_LEN_HI:
        /* Second byte of header (high byte) */
        {
            uint16 len16 = (uint16)((uint16)byte << 8U) | (uint16)imgLenLowByte;
            if (len16 == 0xFFFFU) {
                /* 0xFFFF marker indicates 4-byte uint32 LE length follows */
                imgExtLen = 0U;
                imgState = UART_IMG_WAIT_EXT_LEN_0;
            } else if (len16 > 0U) {
                imgTotalLength = (uint32)len16;
                imgBytesRxd = 0U;
                imgBytesPopped = 0U;
                imgState = UART_IMG_RECEIVING;
                TRACE("[UART_Q] Image header parsed: length=%u bytes", (uint32)imgTotalLength);
            } else {
                imgState = UART_IMG_IDLE;
            }
        }
        break;

    case UART_IMG_WAIT_EXT_LEN_0:
        imgExtLen = (uint32)byte;
        imgState = UART_IMG_WAIT_EXT_LEN_1;
        break;

    case UART_IMG_WAIT_EXT_LEN_1:
        imgExtLen |= ((uint32)byte << 8U);
        imgState = UART_IMG_WAIT_EXT_LEN_2;
        break;

    case UART_IMG_WAIT_EXT_LEN_2:
        imgExtLen |= ((uint32)byte << 16U);
        imgState = UART_IMG_WAIT_EXT_LEN_3;
        break;

    case UART_IMG_WAIT_EXT_LEN_3:
        imgExtLen |= ((uint32)byte << 24U);
        imgTotalLength = imgExtLen;
        imgBytesRxd = 0U;
        imgBytesPopped = 0U;
        if (imgTotalLength > 0U) {
            imgState = UART_IMG_RECEIVING;
            TRACE("[UART_Q] Extended image header parsed: length=%u bytes", (uint32)imgTotalLength);
        } else {
            imgState = UART_IMG_IDLE;
        }
        break;

    case UART_IMG_RECEIVING:
        if (imgBytesRxd < imgTotalLength) {
            if (RingFree() > 0U) {
                RingPush(byte);
                imgBytesRxd++;
            } else {
                /* Queue full — drop byte (Assignment §5.2: "shall not overwrite") */
                static uint32 lastDropWarnMs = 0U;
                if ((uint32)(g_sysTick_ms - lastDropWarnMs) >= 1000U) {
                    lastDropWarnMs = g_sysTick_ms;
                    TRACE("[UART_Q] WARN: queue full, byte dropped at offset %u", (uint32)imgBytesRxd);
                }
            }
        }
        break;

    default:
        imgState = UART_IMG_IDLE;
        break;
    }
}

void UartRxQueue_Poll(void) {
    /* Auto-reset ONLY if queue is completely empty (no data waiting for CanTp)
     * and no bytes received for >3000ms */
    if (imgState != UART_IMG_IDLE && RingUsed() == 0U && (uint32)(g_sysTick_ms - lastRxTime_ms) > 3000U) {
        imgState = UART_IMG_IDLE;
        imgTotalLength = 0U;
        imgBytesRxd = 0U;
        imgBytesPopped = 0U;
        rxHead = 0U;
        rxTail = 0U;
    }

#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))
    /* Clear error flags (OR=Overrun, NF=Noise, FE=Framing, PF=Parity) to unfreeze receiver */
    uint32 stat = LPUART1->STAT;
    if ((stat & (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | 
                 LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK)) != 0U) {
        LPUART1->STAT = stat; /* Write 1 to clear */
    }

    /* Poll LPUART1 Rx Data Register Full flag — non-blocking */
    while ((LPUART1->STAT & LPUART_STAT_RDRF_MASK) != 0U) {
        uint8 byte = (uint8)(LPUART1->DATA & 0xFFU);
        UartRxQueue_PushByte(byte);
    }
#endif
}

uint16 UartRxQueue_Available(void) {
    return RingUsed();
}

uint16 UartRxQueue_Pop(uint8 *dst, uint16 maxLen) {
    if (dst == NULL_PTR) {
        return 0U;
    }
    uint16 avail = RingUsed();
    uint16 toPop = (maxLen < avail) ? maxLen : avail;

    for (uint16 i = 0U; i < toPop; i++) {
        dst[i] = RingPop();
    }
    imgBytesPopped += toPop;
    return toPop;
}

boolean UartRxQueue_ImageActive(void) {
    return (imgState == UART_IMG_RECEIVING) ? TRUE : FALSE;
}

uint32 UartRxQueue_ImageLength(void) {
    return imgTotalLength;
}

uint32 UartRxQueue_ImageBytesReceived(void) {
    return imgBytesRxd;
}

uint32 UartRxQueue_ImageBytesPopped(void) {
    return imgBytesPopped;
}

boolean UartRxQueue_ImageComplete(void) {
    if (imgState == UART_IMG_RECEIVING &&
        imgBytesRxd == imgTotalLength &&
        imgBytesPopped == imgTotalLength) {
        /* All bytes received and consumed — reset for next image */
        TRACE("[UART_Q] Image complete: %u/%u bytes transferred", (uint32)imgBytesPopped, (uint32)imgTotalLength);
        imgState = UART_IMG_IDLE;
        imgTotalLength = 0U;
        imgBytesRxd = 0U;
        imgBytesPopped = 0U;
        return TRUE;
    }
    return FALSE;
}
