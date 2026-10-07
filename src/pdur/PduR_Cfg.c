#include "pdur/PduR_Cfg.h"

/*
 * PduR Routing Table — Assignment v0.7.
 *
 * Routes 0-2: COM signal Tx (COM → CanIf)
 * Routes 3-5: COM signal Rx (CanIf → COM)
 * Routes 6-7: CanTP Tx (CanTP → CanIf)
 * Routes 8-9: CanTP Rx (CanIf → CanTP)
 *
 * srcPduId and dstPduId are module-local handles, not GlobalPduIds.
 */
const PduR_RouteConfigType PduRRoute[PDUR_NUM_ROUTES] = {
    /* TX Routes: COM -> CanIf */
    { .globalPduId=0x0100, .srcModule=PDUR_COM,   .srcPduId=0, .dstModule=PDUR_CANIF, .dstPduId=0 },  /* KeepAlive */
    { .globalPduId=0x0201, .srcModule=PDUR_COM,   .srcPduId=1, .dstModule=PDUR_CANIF, .dstPduId=1 },  /* Slave 1 Status */
    { .globalPduId=0x0202, .srcModule=PDUR_COM,   .srcPduId=2, .dstModule=PDUR_CANIF, .dstPduId=2 },  /* Slave 2 Status */

    /* RX Routes: CanIf -> COM */
    { .globalPduId=0x0100, .srcModule=PDUR_CANIF, .srcPduId=0, .dstModule=PDUR_COM,   .dstPduId=3 },  /* KeepAlive Rx */
    { .globalPduId=0x0201, .srcModule=PDUR_CANIF, .srcPduId=1, .dstModule=PDUR_COM,   .dstPduId=4 },  /* Slave 1 Status Rx */
    { .globalPduId=0x0202, .srcModule=PDUR_CANIF, .srcPduId=2, .dstModule=PDUR_COM,   .dstPduId=5 },  /* Slave 2 Status Rx */

    /* CanTP TX routes: CanTp -> CanIf (unchanged) */
    { .globalPduId=0x0730, .srcModule=PDUR_CANTP, .srcPduId=3, .dstModule=PDUR_CANIF, .dstPduId=3 },  /* Data Tx (0x730) */
    { .globalPduId=0x0731, .srcModule=PDUR_CANTP, .srcPduId=4, .dstModule=PDUR_CANIF, .dstPduId=4 },  /* FC Tx   (0x731) */

    /* CanTP RX routes: CanIf -> CanTp (unchanged) */
    { .globalPduId=0x0730, .srcModule=PDUR_CANIF, .srcPduId=3, .dstModule=PDUR_CANTP, .dstPduId=3 },  /* Data Rx (0x730) */
    { .globalPduId=0x0731, .srcModule=PDUR_CANIF, .srcPduId=4, .dstModule=PDUR_CANTP, .dstPduId=4 }   /* FC Rx   (0x731) */
};
