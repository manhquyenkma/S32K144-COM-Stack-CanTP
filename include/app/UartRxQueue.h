#ifndef UART_RX_QUEUE_H
#define UART_RX_QUEUE_H

#include "Std_Types.h"

/*
 * UART Rx Queue — Circular buffer for receiving ASCII image data from PC.
 * Assignment §5.2: Master UART Rx Queue
 *
 * Protocol from PC (image_sender.py):
 *   [2 bytes: ImageLength, uint16 Little-Endian]
 *   [N bytes: Raw ASCII image payload]
 *
 * The queue stores raw image payload bytes (after header is parsed).
 * App_ImageSender pops ≤62 bytes at a time for CanTp transmission.
 */

#define UART_RX_QUEUE_SIZE  8192U  /* Circular buffer capacity (bytes) */

typedef enum {
    UART_IMG_IDLE = 0,          /* Waiting for first byte of length header */
    UART_IMG_WAIT_LEN_HI,       /* Received low byte, waiting for high byte */
    UART_IMG_WAIT_EXT_LEN_0,    /* Marker 0xFFFF seen: waiting for ext byte 0 */
    UART_IMG_WAIT_EXT_LEN_1,    /* waiting for ext byte 1 */
    UART_IMG_WAIT_EXT_LEN_2,    /* waiting for ext byte 2 */
    UART_IMG_WAIT_EXT_LEN_3,    /* waiting for ext byte 3 */
    UART_IMG_RECEIVING          /* Receiving image payload bytes */
} UartImgStateType;

/* Initialize the UART Rx Queue. Call once at startup after Trace_Init(). */
void    UartRxQueue_Init(void);

/* Poll LPUART1 RDRF flag and push received bytes into queue.
 * Call this every 1ms from the main scheduler loop. */
void    UartRxQueue_Poll(void);

/* Directly push one byte into the queue state machine (used by Poll or host tests). */
void    UartRxQueue_PushByte(uint8 byte);

/* Number of image payload bytes available to pop from queue. */
uint16  UartRxQueue_Available(void);

/* Pop up to maxLen bytes from the queue into dst buffer.
 * Returns the number of bytes actually popped. */
uint16  UartRxQueue_Pop(uint8 *dst, uint16 maxLen);

/* True if an image header has been parsed and reception is in progress or complete. */
boolean UartRxQueue_ImageActive(void);

/* Total image length as declared in the header. */
uint32  UartRxQueue_ImageLength(void);

/* Total image payload bytes received so far (pushed into queue). */
uint32  UartRxQueue_ImageBytesReceived(void);

/* Total image payload bytes already popped by App_ImageSender. */
uint32  UartRxQueue_ImageBytesPopped(void);

/* True when all image bytes have been received AND popped (transfer complete).
 * After this returns TRUE, the state resets to IDLE for the next image. */
boolean UartRxQueue_ImageComplete(void);

#endif /* UART_RX_QUEUE_H */
