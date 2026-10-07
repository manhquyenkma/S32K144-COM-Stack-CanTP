#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "Std_Types.h"
#include "cantp/CanTp.h"
#include "canif/CanIf.h"
#include "pdur/PduR.h"
#include "app/App.h"
#include "candrv/Can.h"
#include "app/Role.h"

/* Monotonic tick simulation */
volatile uint32 g_sysTick_ms = 0;
volatile uint32 Trace_DroppedCount = 0;

void Trace_Init(void) {}
void Trace_Flush(void) { fflush(stdout); }
void Trace_FlushBlocking(void) { fflush(stdout); }
char Trace_GetChar(void) { return '\0'; }

static int g_verbose = 0;

void Trace_Enqueue(const char* fmt, ...) {
    if (!g_verbose) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

/* Stubs for low-level dependencies */
Can_ReturnType Can_Write(Can_HwHandleType Hth, const Can_PduType* PduInfo) {
    (void)Hth; (void)PduInfo; return CAN_OK;
}
void Can_Init(const Can_ConfigType* Config) { (void)Config; }
void Can_MainFunction_Write(void) {}
void Can_MainFunction_Read(void) {}

void Com_RxIndication(PduIdType RxPduId, const PduInfoType* PduInfoPtr) { (void)RxPduId; (void)PduInfoPtr; }
void Com_TxConfirmation(PduIdType TxPduId) { (void)TxPduId; }
typedef void (*Com_RxCallbackType)(void);
Std_ReturnType Com_SetRxCallback(uint8 SignalId, Com_RxCallbackType Callback) { (void)SignalId; (void)Callback; return E_OK; }
Std_ReturnType Com_ValidateConfig(void) { return E_OK; }
uint8 Com_SendSignal(uint8 SignalId, const void* SignalDataPtr) { (void)SignalId; (void)SignalDataPtr; return 0; }
uint8 Com_ReceiveSignal(uint8 SignalId, void* SignalDataPtr) { (void)SignalId; (void)SignalDataPtr; return 0; }

RoleType Role_Get(void) { return ROLE_0_VEHICLE_TX; }
const char* Role_GetName(void) { return "MASTER_ECU"; }

/* CAN Statistics */
static uint32 g_sfCount = 0;
static uint32 g_ffCount = 0;
static uint32 g_cfCount = 0;
static uint32 g_fcCount = 0;
static uint32 g_totalCanFrames = 0;

/* Receiver Buffer */
static uint8*  g_rxStreamBuffer = NULL;
static uint32  g_rxStreamOffset = 0;
static uint32  g_rxStreamCapacity = 0;

static Std_ReturnType Stream_CanIf_TransmitHook(PduIdType TxPduId, const PduInfoType* PduInfo) {
    g_totalCanFrames++;
    uint8 pci = PduInfo->SduDataPtr[0];
    uint8 type = pci & 0xF0U;

    if (type == 0x00U) {
        g_sfCount++;
    } else if (type == 0x10U) {
        g_ffCount++;
    } else if (type == 0x20U) {
        g_cfCount++;
    } else if (type == 0x30U) {
        g_fcCount++;
    }

    /* Auto-confirm Tx to CanTp */
    CanTp_TxConfirmation(TxPduId);

    /* Loopback to Rx side:
     * If TxPduId == CANTP_TX_DATA_LPDU_ID (3), deliver to Rx Data (3)
     * If TxPduId == CANTP_TX_FC_LPDU_ID (4), deliver to Rx FC (4)
     */
    PduInfoType rxPdu;
    rxPdu.SduDataPtr  = PduInfo->SduDataPtr;
    rxPdu.SduLength   = 8U;
    rxPdu.MetaDataPtr = NULL_PTR;

    if (TxPduId == CANTP_TX_DATA_LPDU_ID) {
        CanTp_RxIndication(CANTP_RX_DATA_NPDU_ID, &rxPdu);
    } else if (TxPduId == CANTP_TX_FC_LPDU_ID) {
        CanTp_RxIndication(CANTP_RX_FC_NPDU_ID, &rxPdu);
    }

    return E_OK;
}

static void Stream_ResetStats(void) {
    g_sfCount = 0;
    g_ffCount = 0;
    g_cfCount = 0;
    g_fcCount = 0;
    g_totalCanFrames = 0;
    g_sysTick_ms = 0;
}

static int Stream_File(const char* filepath, const char* label, int printAscii) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        printf("[-] Error: Cannot open file '%s'\n", filepath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        printf("[-] Error: File '%s' is empty.\n", filepath);
        fclose(f);
        return -1;
    }

    uint8* fileBuffer = (uint8*)malloc(fileSize);
    if (!fileBuffer) {
        printf("[-] Error: Memory allocation failed for file buffer.\n");
        fclose(f);
        return -1;
    }
    if (fread(fileBuffer, 1, fileSize, f) != (size_t)fileSize) {
        printf("[-] Error: Failed to read file data.\n");
        free(fileBuffer);
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Prepare Rx Buffer */
    g_rxStreamCapacity = fileSize;
    g_rxStreamBuffer   = (uint8*)calloc(1, fileSize + 1);
    g_rxStreamOffset   = 0;

    /* Initialize Stack */
    CanIf_TransmitHook = Stream_CanIf_TransmitHook;
    Stream_ResetStats();
    CanTp_Init();
    CanIf_Init();
    PduR_Init();
    App_ResetCanTpCounters();

    printf("\n======================================================================\n");
    printf(" [CanTp STREAMING DEMO] Transferring: %s (%ld Bytes)\n", label, fileSize);
    printf(" File Path: %s\n", filepath);
    printf("======================================================================\n");

    uint32 offset = 0;
    uint32 chunkCount = 0;
    uint32 startTimeMs = g_sysTick_ms;

    while (offset < (uint32)fileSize) {
        uint32 remaining = fileSize - offset;
        uint16 chunkLen  = (remaining < CANTP_MAX_NSDU) ? (uint16)remaining : CANTP_MAX_NSDU;

        /* Set App source data for CopyTxData callback */
        App_SetTxSourceData(&fileBuffer[offset], chunkLen);

        PduInfoType pdu;
        pdu.SduDataPtr  = &fileBuffer[offset];
        pdu.SduLength   = chunkLen;
        pdu.MetaDataPtr = NULL_PTR;

        Std_ReturnType ret = CanTp_Transmit(0U, &pdu);
        if (ret != E_OK) {
            printf("[-] CanTp_Transmit failed at offset %u!\n", offset);
            free(fileBuffer);
            free(g_rxStreamBuffer);
            return -1;
        }

        chunkCount++;

        /* Drive CanTp main loop until current chunk transmission completes */
        uint32 chunkStartMs = g_sysTick_ms;
        while (CanTp_GetTxState() != TX_IDLE || CanTp_GetRxState() != RX_IDLE) {
            CanTp_MainFunction();
            g_sysTick_ms += 1;

            /* Guard timeout per chunk */
            if (g_sysTick_ms - chunkStartMs > 5000) {
                printf("[-] Timeout transmitting chunk %u!\n", chunkCount);
                break;
            }
        }

        /* Collect received chunk from App Rx Queue */
        for (uint8 i = 0; i < APP_RX_QUEUE_SLOTS; i++) {
            if (App_RxQueue[i].state == APP_SLOT_READY) {
                if (g_rxStreamOffset + App_RxQueue[i].length <= g_rxStreamCapacity) {
                    memcpy(&g_rxStreamBuffer[g_rxStreamOffset], App_RxQueue[i].data, App_RxQueue[i].length);
                    g_rxStreamOffset += App_RxQueue[i].length;
                }
                App_RxQueue[i].state  = APP_SLOT_FREE;
                App_RxQueue[i].length = 0U;
            }
        }

        offset += chunkLen;
    }

    uint32 totalDurationMs = g_sysTick_ms - startTimeMs;
    if (totalDurationMs == 0) totalDurationMs = 1;

    /* Verify Data Integrity */
    int match = (g_rxStreamOffset == (uint32)fileSize) && (memcmp(fileBuffer, g_rxStreamBuffer, fileSize) == 0);

    /* Print Metrics */
    printf("\n--- TRANSFER SUMMARY & METRICS ---\n");
    printf(" Status             : %s\n", match ? "[SUCCESS] 100% DATA INTEGRITY MATCH!" : "[FAIL] DATA CORRUPTED");
    printf(" Original File Size : %ld bytes\n", fileSize);
    printf(" Received Payload   : %u bytes\n", g_rxStreamOffset);
    printf(" N-SDU Chunks Sent  : %u chunks (Max 62B payload per chunk)\n", chunkCount);
    printf(" CAN Frames Breakdown:\n");
    printf("   - Single Frames (SF)       : %u\n", g_sfCount);
    printf("   - First Frames (FF)        : %u\n", g_ffCount);
    printf("   - Consecutive Frames (CF)  : %u\n", g_cfCount);
    printf("   - Flow Control (FC)        : %u\n", g_fcCount);
    printf("   - Total CAN Frames Transmitted : %u frames\n", g_totalCanFrames);
    printf(" Total CAN Bus Wire Payload   : %u bytes (DLC=8 fixed)\n", g_totalCanFrames * 8);
    printf(" CanTp Overhead Ratio        : %.2f%%\n", 
           ((double)(g_totalCanFrames * 8 - fileSize) / (double)(g_totalCanFrames * 8)) * 100.0);
    printf(" Simulated Transfer Time     : %u ms\n", totalDurationMs);
    printf(" Effective Throughput        : %.2f KB/s (Simulated CAN Bus)\n", 
           ((double)fileSize / 1024.0) / ((double)totalDurationMs / 1000.0));

    if (printAscii && match) {
        printf("\n======================================================================\n");
        printf(" [STREAMED ASCII ARTWORK PREVIEW AT SLAVE RECEIVER]\n");
        printf("======================================================================\n");
        fwrite(g_rxStreamBuffer, 1, g_rxStreamOffset, stdout);
        printf("\n======================================================================\n");
    }

    free(fileBuffer);
    free(g_rxStreamBuffer);
    g_rxStreamBuffer = NULL;
    return match ? 0 : 1;
}

int main(int argc, char* argv[]) {
    printf("======================================================================\n");
    printf("   AUTOSAR Mock COM Stack - CanTp ASCII File Streaming Demo v0.7      \n");
    printf("======================================================================\n");

    int choice = 0;
    int printArt = 1;
    const char* customFile = NULL;

    const char* files[5] = {
        "ascii_cat_512B_showcase.txt",
        "ascii_owl_2KB.txt",
        "ascii_monalisa_refstyle_8KB.txt",
        "ascii_monalisa_refstyle_16KB.txt",
        "ascii-monalisa-130KB.txt"
    };

    const char* labels[5] = {
        "Small Showcase: ASCII Cat (512 Bytes)",
        "Medium Showcase: ASCII Owl (2 KB)",
        "Large Showcase: ASCII Mona Lisa RefStyle (8 KB)",
        "XL Stress Showcase: ASCII Mona Lisa RefStyle (16 KB)",
        "XXL Ultra Stress Showcase: ASCII Mona Lisa (130 KB)"
    };

    if (argc > 1) {
        if (strcmp(argv[1], "1") == 0) choice = 1;
        else if (strcmp(argv[1], "2") == 0) choice = 2;
        else if (strcmp(argv[1], "3") == 0) choice = 3;
        else if (strcmp(argv[1], "4") == 0) choice = 4;
        else if (strcmp(argv[1], "5") == 0) choice = 5;
        else if (strcmp(argv[1], "all") == 0) choice = 6;
        else {
            customFile = argv[1];
        }
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-art") == 0) {
            printArt = 0;
        }
    }

    if (choice == 0 && customFile == NULL) {
        choice = 6; /* Default to running all files sequentially */
    }

    int failCount = 0;

    if (customFile != NULL) {
        int res = Stream_File(customFile, customFile, printArt);
        if (res != 0) failCount++;
    } else if (choice >= 1 && choice <= 5) {
        int res = Stream_File(files[choice - 1], labels[choice - 1], printArt);
        if (res != 0) failCount++;
    } else {
        /* Run all 5 */
        for (int i = 0; i < 5; i++) {
            /* For XXL 130KB, skip printing huge text unless user explicitly ran single file */
            int showThisArt = (i == 4) ? 0 : printArt;
            int res = Stream_File(files[i], labels[i], showThisArt);
            if (res != 0) failCount++;
        }
    }

    printf("\n======================================================================\n");
    if (failCount == 0) {
        printf(" >>> [DEMO COMPLETE] ALL STREAM TRANSFERS SUCCEEDED! <<<\n");
    } else {
        printf(" >>> [DEMO COMPLETED WITH ERRORS] %d TRANSFERS FAILED! <<<\n", failCount);
    }
    printf("======================================================================\n");

    return failCount;
}
