#include "com/Com.h"
#include "pdur/PduR.h"
#include "cantp/CanTp.h"
#include "trace/Trace.h"
#include "app/Role.h"
#include "app/App.h"
#include <string.h>

extern volatile uint32 g_sysTick_ms;

#define MAX_IPDUS (COM_NUM_TX_IPDU + COM_NUM_RX_IPDU)

uint8  Com_IpduBuffer[MAX_IPDUS][8];
uint16 Com_TimerTicks[MAX_IPDUS];
boolean Com_IsPending[MAX_IPDUS];
uint8  Com_RetryCount[MAX_IPDUS];

/*
 * ISR/Task Separation (architecture_notes §14 / assignment §14 COM context split):
 *
 * ISR  context → Com_RxIndication():    copy raw bytes + set Com_RxFlag[]
 * Task context → Com_MainFunction_Rx(): check flag, unpack signals, check
 *                                        Update Bit (bit 0), notify App.
 *
 * volatile ensures the compiler does not cache the flag value across the
 * ISR/task boundary on a Cortex-M4 with no cache coherency issue in SRAM.
 */
static volatile boolean Com_RxFlag[MAX_IPDUS];

/* Optional App-level Rx notification callback (registered via Com_SetRxCallback) */
static Com_RxCallbackType Com_RxCallback = NULL_PTR;

void Com_Init(void) {
    for (int i = 0; i < MAX_IPDUS; i++) {
        memset(Com_IpduBuffer[i], 0, 8);
        Com_IsPending[i] = FALSE;
        Com_RetryCount[i] = 0;
        Com_RxFlag[i]     = FALSE;
        if (ComIPdu[i].direction == COM_TX) {
            Com_TimerTicks[i] = ComIPdu[i].initialOffsetTicks;
        } else {
            Com_TimerTicks[i] = 0;
        }
    }
}

void Com_SetRxCallback(Com_RxCallbackType callback) {
    Com_RxCallback = callback;
}

/* ---------- Generic byte-aligned signal packing (F-01/A-04 fix) ---------- */

static void pack_signal(uint8* buffer, const Com_SignalConfigType* sigCfg, const void* data) {
    uint32 val = 0;
    if (sigCfg->dataType == COM_UINT8)       val = *(const uint8*)data;
    else if (sigCfg->dataType == COM_UINT16) val = *(const uint16*)data;
    else if (sigCfg->dataType == COM_UINT32) val = *(const uint32*)data;

    /* REQ-07: EncodedSignal = (Payload << 1) | 1 */
    val = (val << 1) | 1U;

    uint8 startByte = sigCfg->slotStartBit / 8U;
    uint8 numBytes  = sigCfg->slotLength  / 8U;

    if (sigCfg->endianness == COM_LITTLE_ENDIAN) {
        /* LE: LSByte at lowest address */
        for (uint8 i = 0; i < numBytes; i++) {
            buffer[startByte + i] = (uint8)((val >> (i * 8U)) & 0xFFU);
        }
    } else {
        /* BE: MSByte at lowest address */
        for (uint8 i = 0; i < numBytes; i++) {
            buffer[startByte + i] = (uint8)((val >> ((numBytes - 1U - i) * 8U)) & 0xFFU);
        }
    }
}

static void unpack_signal(const uint8* buffer, const Com_SignalConfigType* sigCfg, void* data) {
    uint32 val = 0;
    uint8 startByte = sigCfg->slotStartBit / 8U;
    uint8 numBytes  = sigCfg->slotLength  / 8U;

    if (sigCfg->endianness == COM_LITTLE_ENDIAN) {
        for (uint8 i = 0; i < numBytes; i++) {
            val |= ((uint32)buffer[startByte + i]) << (i * 8U);
        }
    } else {
        for (uint8 i = 0; i < numBytes; i++) {
            val |= ((uint32)buffer[startByte + i]) << ((numBytes - 1U - i) * 8U);
        }
    }

    /* REQ-07: Decode payload = EncodedSignal >> 1 */
    val = val >> 1;

    if (sigCfg->dataType == COM_UINT8)       *(uint8*)data  = (uint8)val;
    else if (sigCfg->dataType == COM_UINT16) *(uint16*)data = (uint16)val;
    else if (sigCfg->dataType == COM_UINT32) *(uint32*)data = (uint32)val;
}

/* ---------- UpdateBit clearing (F-02 fix) ---------- */

static void clear_update_bit(uint8* buffer, const Com_SignalConfigType* sigCfg) {
    uint8 startByte = sigCfg->slotStartBit / 8U;
    uint8 numBytes  = sigCfg->slotLength  / 8U;

    if (sigCfg->endianness == COM_LITTLE_ENDIAN) {
        /* LE: bit 0 of encoded value is at startByte, bit 0 */
        buffer[startByte] &= (uint8)~0x01U;
    } else {
        /* BE: bit 0 of encoded value is at last byte of slot, bit 0 */
        buffer[startByte + numBytes - 1U] &= (uint8)~0x01U;
    }
}

/* ---------- Public API ---------- */

Std_ReturnType Com_SendSignal(uint16 SignalId, const void* SignalDataPtr) {
    if (SignalId >= COM_NUM_SIGNALS) return E_NOT_OK;
    const Com_SignalConfigType* sigCfg = &ComSignal[SignalId];
    const Com_SignalGroupConfigType* groupCfg = &ComSignalGroup[sigCfg->signalGroupId];

    /* Value range check (§15): payload must fit in (slotLength - 1) bits.
     * EncodedSignal = (Payload << 1) | 1.  Maximum raw payload value is
     * (2^(slotLength-1)) - 1.  Reject values that would overflow the slot. */
    uint8 payloadBits = (uint8)(sigCfg->slotLength - 1U);
    uint32 maxVal = (payloadBits >= 32U) ? 0xFFFFFFFFUL : ((1UL << payloadBits) - 1UL);
    uint32 rawVal = 0U;
    if      (sigCfg->dataType == COM_UINT8)  rawVal = (uint32)*(const uint8*)SignalDataPtr;
    else if (sigCfg->dataType == COM_UINT16) rawVal = (uint32)*(const uint16*)SignalDataPtr;
    else if (sigCfg->dataType == COM_UINT32) rawVal = *(const uint32*)SignalDataPtr;

    if (rawVal > maxVal) {
        TRACE("[COM] SendSignal id=%u val=%u exceeds %u-bit payload (max=%u)",
              SignalId, rawVal, payloadBits, maxVal);
        return E_NOT_OK;
    }

    pack_signal(Com_IpduBuffer[groupCfg->ipduId], sigCfg, SignalDataPtr);
    return E_OK;
}

Std_ReturnType Com_ReceiveSignal(uint16 SignalId, void* SignalDataPtr) {
    if (SignalId >= COM_NUM_SIGNALS) return E_NOT_OK;
    const Com_SignalConfigType* sigCfg = &ComSignal[SignalId];
    const Com_SignalGroupConfigType* groupCfg = &ComSignalGroup[sigCfg->signalGroupId];
    
    unpack_signal(Com_IpduBuffer[groupCfg->ipduId], sigCfg, SignalDataPtr);
    return E_OK;
}

/* ---------- COM Tx Scheduler (F-04 / F-06 fix) ---------- */

void Com_MainFunction_Tx(void) {
    /* If image streaming or CanTp session is active, completely yield CAN bus and mute COM Tx */
    if (App_IsImageStreamingActive()) {
        return;
    }

    RoleType role = Role_Get();

    for (int i = 0; i < COM_NUM_TX_IPDU; i++) {
        /* Only transmit the IPDU owned by current ECU role (§Role.h):
         * Role 0: IPDU 0 (VehicleStatus, gpdu=0x0010)
         * Role 1: IPDU 1 (EngineStatus, gpdu=0x0011)
         * Role 2: IPDU 2 (BodyStatus, gpdu=0x0012)
         */
        if ((role == ROLE_1_ENGINE_TX  && i != 0) ||
            (role == ROLE_2_BODY_TX    && i != 1) ||
            (role == ROLE_0_VEHICLE_TX && i != 2)) {
            continue;
        }

        const Com_IPduConfigType* ipduCfg = &ComIPdu[i];
        
        /* Countdown */
        if (Com_TimerTicks[i] > 0) {
            Com_TimerTicks[i]--;
        }
        
        /* Deadline check — assignment reference state machine */
        if (Com_TimerTicks[i] == 0) {
            if (Com_IsPending[i] == FALSE) {
                Com_IsPending[i] = TRUE;
                Com_RetryCount[i] = 0;
            }
            /* Always reload counter — no drift */
            Com_TimerTicks[i] = ipduCfg->periodTicks;
        }

        /* Transmit attempt if pending */
        if (Com_IsPending[i]) {
            PduInfoType pduInfo;
            pduInfo.SduDataPtr = Com_IpduBuffer[i];
            pduInfo.SduLength = ipduCfg->length;
            pduInfo.MetaDataPtr = NULL;
            
            Std_ReturnType ret = PduR_ComTransmit(ipduCfg->ipduId, &pduInfo);
            if (ret == E_OK) {
                Com_IsPending[i] = FALSE;
                Com_RetryCount[i] = 0;
                
                /* Periodic signal display (every ~500ms) */
                static uint32 lastTxLogMs[MAX_IPDUS] = {0};
                if (!App_IsImageStreamingActive() && ((uint32)(g_sysTick_ms - lastTxLogMs[i]) >= 500U)) {
                    lastTxLogMs[i] = g_sysTick_ms;
                    if (ipduCfg->ipduId == 0) {
                        uint8 alv = 0, lvl = 0;
                        unpack_signal(Com_IpduBuffer[i], &ComSignal[0], &alv);
                        unpack_signal(Com_IpduBuffer[i], &ComSignal[1], &lvl);
                        TRACE("[COM] TX [KeepAlive 0x100]: Alive=%u | RateLevel=%u", (uint32)alv, (uint32)lvl);
                    } else if (ipduCfg->ipduId == 1) {
                        uint8 st = 0;
                        unpack_signal(Com_IpduBuffer[i], &ComSignal[2], &st);
                        TRACE("[COM] TX [Slave1Status 0x201]: %u (%s)", (uint32)st, st ? "MASTER_LOST" : "NORMAL");
                    } else if (ipduCfg->ipduId == 2) {
                        uint8 st = 0;
                        unpack_signal(Com_IpduBuffer[i], &ComSignal[3], &st);
                        TRACE("[COM] TX [Slave2Status 0x202]: %u (%s)", (uint32)st, st ? "MASTER_LOST" : "NORMAL");
                    }
                }

                /* REQ-08: Clear UpdateBits for all signals in this I-PDU */
                const Com_SignalGroupConfigType* grp = &ComSignalGroup[ipduCfg->signalGroupId];
                for (uint8 s = 0; s < grp->signalCount; s++) {
                    clear_update_bit(Com_IpduBuffer[i], &ComSignal[grp->signalStartIdx + s]);
                }
            } else {
                /* F-06: retry_count < max_retries → keep pending and increment */
                /*        retry_count >= max_retries → drop occurrence */
                if (Com_RetryCount[i] < ipduCfg->maxRetries) {
                    Com_RetryCount[i]++;
                } else {
                    Com_IsPending[i] = FALSE;
                    Com_RetryCount[i] = 0;
                    /* UpdateBits preserved on drop — do NOT clear buffer */
                }
            }
        }
    }
}

/*
 * Com_MainFunction_Rx — Task context (called every 1 ms by the scheduler).
 *
 * Architecture notes §14 / assignment §14:
 *   This function runs in cooperative task context, NOT in ISR context.
 *   It polls Com_RxFlag[] set by Com_RxIndication() (ISR-safe), then
 *   unpacks signals and checks Update Bits before notifying the App.
 *
 * Rx signal unpack flow per assignment §31:
 *   1. Check Com_RxFlag[id] — skip if not set.
 *   2. Clear flag atomically (read-clear).
 *   3. Walk the Signal Group: for each Signal, read the Signal Slot.
 *      If UpdateBit (bit 0) == 1  → new data present, decode payload.
 *      If UpdateBit (bit 0) == 0  → stale / no update, skip.
 *   4. Invoke App callback if registered.
 */
void Com_MainFunction_Rx(void) {
    for (PduIdType id = 0; id < MAX_IPDUS; id++) {
        if (ComIPdu[id].direction != COM_RX) continue;

        if (Com_RxFlag[id] == FALSE) continue;

        /* Clear flag — must happen before processing to avoid missing a concurrent
         * ISR update. On Cortex-M4 without RTOS, boolean write is atomic. */
        Com_RxFlag[id] = FALSE;

        const Com_IPduConfigType*       ipduCfg = &ComIPdu[id];
        const Com_SignalGroupConfigType* grp     = &ComSignalGroup[ipduCfg->signalGroupId];

        boolean anyUpdated = FALSE;
        for (uint8 s = 0; s < grp->signalCount; s++) {
            const Com_SignalConfigType* sig = &ComSignal[grp->signalStartIdx + s];
            uint8 startByte = (uint8)(sig->slotStartBit / 8U);

            /* Read the Update Bit (bit 0 of the LSByte of the encoded slot). */
            uint8 updateBit;
            if (sig->endianness == COM_LITTLE_ENDIAN) {
                /* LE: bit 0 of encoded value is at startByte, bit 0 */
                updateBit = Com_IpduBuffer[id][startByte] & 0x01U;
            } else {
                /* BE: bit 0 of encoded value is at last byte of slot, bit 0 */
                uint8 lastByte = (uint8)(startByte + (sig->slotLength / 8U) - 1U);
                updateBit = Com_IpduBuffer[id][lastByte] & 0x01U;
            }

            if (updateBit != 0U) {
                anyUpdated = TRUE;
            }
        }

        if (anyUpdated) {
            /* Only log received COM signals when NOT streaming an image */
            if (!App_IsImageStreamingActive()) {
                static uint32 lastRxLogMs[MAX_IPDUS] = {0};
                if ((uint32)(g_sysTick_ms - lastRxLogMs[id]) >= 500U) {
                    lastRxLogMs[id] = g_sysTick_ms;
                    if (id == 3) {
                        uint8 alv = 0, lvl = 0;
                        unpack_signal(Com_IpduBuffer[id], &ComSignal[4], &alv);
                        unpack_signal(Com_IpduBuffer[id], &ComSignal[5], &lvl);
                        TRACE("[COM] RX [KeepAlive 0x100]: Alive=%u | RateLevel=%u", (uint32)alv, (uint32)lvl);
                    } else if (id == 4) {
                        uint8 st = 0;
                        unpack_signal(Com_IpduBuffer[id], &ComSignal[6], &st);
                        TRACE("[COM] RX [Slave1Status 0x201]: %u (%s)", (uint32)st, st ? "MASTER_LOST" : "NORMAL");
                    } else if (id == 5) {
                        uint8 st = 0;
                        unpack_signal(Com_IpduBuffer[id], &ComSignal[7], &st);
                        TRACE("[COM] RX [Slave2Status 0x202]: %u (%s)", (uint32)st, st ? "MASTER_LOST" : "NORMAL");
                    }
                }
            }

            if (Com_RxCallback != NULL_PTR) {
                Com_RxCallback(id);
            }
        }
    }
}

/*
 * Com_RxIndication — ISR context (called from CAN driver interrupt / polling).
 *
 * Architecture notes §14:
 *   SHALL only: (1) copy raw bytes into the PDU buffer, (2) set RxFlag.
 *   SHALL NOT: unpack signals, check Update Bits, or call App callbacks.
 *   This keeps ISR latency minimal and deterministic.
 */
void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfo) {
    if (RxPduId >= MAX_IPDUS) return;
    if (ComIPdu[RxPduId].direction != COM_RX) return;

    /* Fast byte copy — ISR safe (no dynamic allocation, no callbacks) */
    uint8 copyLen = (PduInfo->SduLength < 8U) ? (uint8)PduInfo->SduLength : 8U;
    for (uint8 i = 0; i < copyLen; i++) {
        Com_IpduBuffer[RxPduId][i] = PduInfo->SduDataPtr[i];
    }

    /* Signal Task context to process this PDU */
    Com_RxFlag[RxPduId] = TRUE;
}

/* ---------- Configuration Validation (R-33 fix) ---------- */

Std_ReturnType Com_ValidateConfig(void) {
    /* --- Signal slot validation --- */
    for (int s = 0; s < COM_NUM_SIGNALS; s++) {
        const Com_SignalConfigType* sig = &ComSignal[s];
        
        /* SlotStartBit must be byte-aligned */
        if (sig->slotStartBit % 8U != 0) return E_NOT_OK;
        
        /* SlotLength must be multiple of 8 and >= 8 */
        if (sig->slotLength < 8U)        return E_NOT_OK;
        if (sig->slotLength % 8U != 0)   return E_NOT_OK;
        
        /* Slot must fit within I-PDU buffer (8 bytes = 64 bits) */
        if ((uint16)(sig->slotStartBit + sig->slotLength) > 64U) return E_NOT_OK;
        
        /* Data type width must fit in slotLength bits */
        uint8 dataTypeBits = 0;
        if (sig->dataType == COM_UINT8)       dataTypeBits = 8;
        else if (sig->dataType == COM_UINT16) dataTypeBits = 16;
        else if (sig->dataType == COM_UINT32) dataTypeBits = 32;
        if (dataTypeBits > sig->slotLength) return E_NOT_OK;
    }
    
    /* --- Slot overlap check within each group --- */
    for (int g = 0; g < COM_NUM_SIGNAL_GROUPS; g++) {
        const Com_SignalGroupConfigType* grp = &ComSignalGroup[g];
        if (grp->signalCount == 0) return E_NOT_OK; /* Group must be non-empty */
        
        for (uint8 a = 0; a < grp->signalCount; a++) {
            for (uint8 b = a + 1U; b < grp->signalCount; b++) {
                const Com_SignalConfigType* sa = &ComSignal[grp->signalStartIdx + a];
                const Com_SignalConfigType* sb = &ComSignal[grp->signalStartIdx + b];
                uint16 aEnd = sa->slotStartBit + sa->slotLength;
                uint16 bEnd = sb->slotStartBit + sb->slotLength;
                /* Overlap if ranges intersect */
                if (sa->slotStartBit < bEnd && sb->slotStartBit < aEnd) return E_NOT_OK;
            }
        }
    }
    
    /* --- I-PDU validation --- */
    /* Tx PDU validation: period must be > 0 */
    for (int i = 0; i < COM_NUM_TX_IPDU; i++) {
        const Com_IPduConfigType* pdu = &ComIPdu[i];
        if (pdu->periodTicks == 0) return E_NOT_OK;
    }

    /* GlobalPduId uniqueness among same-direction PDUs */
    for (int i = 0; i < (COM_NUM_TX_IPDU + COM_NUM_RX_IPDU); i++) {
        for (int j = i + 1; j < (COM_NUM_TX_IPDU + COM_NUM_RX_IPDU); j++) {
            /* Only compare PDUs with the same direction */
            if (ComIPdu[i].direction == ComIPdu[j].direction &&
                ComIPdu[i].globalPduId == ComIPdu[j].globalPduId) {
                return E_NOT_OK;
            }
        }
    }
    
    return E_OK;
}
