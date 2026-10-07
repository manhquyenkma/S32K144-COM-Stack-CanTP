#ifndef CANTP_CFG_H
#define CANTP_CFG_H

#include "ComStack_Types.h"

/*
 * MOCK CanTP Configuration (Student Implementation Guide v2.0)
 * Updated per ISO 15765-2 recommended timeout values.
 *
 * Wire format (CAN Classic, fixed DLC = 8, padding = 0x00):
 *
 *   SF (Single Frame, N-SDU 1..7 bytes):
 *     byte 0: 0x0L  (L = SF_DL, low nibble = data length 1..7)
 *     byte 1..L: data payload
 *     byte L+1..7: padding 0x00
 *
 *   FF (First Frame, N-SDU 8..62 bytes):
 *     byte 0: 0x10 | ((FF_DL >> 8) & 0x0F)
 *     byte 1: FF_DL & 0xFF       (12-bit total length)
 *     byte 2..7: first 6 bytes of data
 *
 *   CF (Consecutive Frame):
 *     byte 0: 0x20 | (SN & 0x0F)   (SN 4-bit modulo 16)
 *     byte 1..7: up to 7 bytes of data, padded with 0x00
 *
 *   FC (Flow Control):
 *     byte 0: 0x30 | (FS & 0x0F)   (FS: 0=CTS, 1=WAIT, 2=OVFLW)
 *     byte 1: BS (BlockSize)
 *     byte 2: STmin (Separation Time)
 *     byte 3..7: padding 0x00
 */

/* --- Protocol Limits & Capacities --- */
#define CANTP_MAX_NSDU              62U     /* Max supported N-SDU size (bytes) */
#define CANTP_CHUNK_CAPACITY        64U     /* Snapshot chunk buffer size (bytes) */
#define CANTP_FRAME_LENGTH          8U      /* Fixed CAN DLC = 8 */

#define CANTP_PAYLOAD_SF_MAX        7U      /* Max data bytes in Single Frame (1 byte PCI) */
#define CANTP_PAYLOAD_FF_MAX        6U      /* Max data bytes in First Frame (2 bytes PCI) */
#define CANTP_PAYLOAD_CF_MAX        7U      /* Max data bytes in Consecutive Frame */

/* --- Flow Control Parameters (Local Rx advertises these in FC frames) --- */
#define CANTP_BS                    4U      /* Block Size = 4 CFs per FC block */
#define CANTP_STMIN_MS              5U      /* Separation Time = 5 ms (our Rx advertised value) */
#define CANTP_MAX_RETRIES           3U      /* Max 3 retries (total 4 attempts) */

/* --- Timeout Timers per ISO 15765-2 ---
 *
 * N_As / N_Ar (hardware-level): Time for CanIf to confirm frame sent to bus.
 *   Recommended: 25ms–100ms. Depends on bus load and CAN ID priority.
 *
 * N_Bs (session-level): Tx waits for FC from Rx after sending FF or block of CFs.
 *   ISO 15765-2 MANDATORY: 1000ms (1 second).
 *
 * N_Cr (session-level): Rx waits for next CF from Tx.
 *   ISO 15765-2 MANDATORY: 1000ms (1 second).
 */
#define CANTP_N_AS_MS               100U    /* Sender: Data frame Tx confirmation timeout */
#define CANTP_N_AR_MS               100U    /* Receiver: FC frame Tx confirmation timeout */
#define CANTP_N_BS_MS               100U    /* Sender: Wait for Flow Control timeout (Student Guide §1.2: 100ms) */
#define CANTP_N_CR_MS               100U    /* Receiver: Wait for Consecutive Frame timeout (Student Guide §1.2: 100ms) */

/* --- Flow Control Constants --- */
#define CANTP_FC_FS_CTS             0x00U   /* Continue To Send */
#define CANTP_FC_FS_WAIT            0x01U   /* Wait (unsupported in baseline mock -> abort Tx) */
#define CANTP_FC_FS_OVFLW           0x02U   /* Overflow (Rx buffer too small) */

/* --- Frame Type Masks and Constants --- */
#define CANTP_FRAME_TYPE_SF         0x00U   /* Single Frame (high nibble) */
#define CANTP_FRAME_TYPE_FF         0x10U   /* First Frame */
#define CANTP_FRAME_TYPE_CF         0x20U   /* Consecutive Frame */
#define CANTP_FRAME_TYPE_FC         0x30U   /* Flow Control */
#define CANTP_FRAME_TYPE_MASK       0xF0U
#define CANTP_PADDING_BYTE          0x00U   /* Pad with 0x00 */

/* --- CanIf L-PDU / N-PDU IDs (Dedicated Connection) --- */
#define CANTP_TX_DATA_LPDU_ID       3U      /* CanIf Tx L-PDU for Data (0x730) */
#define CANTP_TX_FC_LPDU_ID         4U      /* CanIf Tx L-PDU for FC (0x731) */
#define CANTP_RX_DATA_NPDU_ID       3U      /* CanIf Rx N-PDU for Data (0x730) */
#define CANTP_RX_FC_NPDU_ID         4U      /* CanIf Rx N-PDU for FC (0x731) */

#endif /* CANTP_CFG_H */
