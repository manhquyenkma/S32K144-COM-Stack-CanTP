#ifndef PDUR_CFG_H
#define PDUR_CFG_H

#include "ComStack_Types.h"

#define PDUR_NUM_ROUTES 10   /* 6 COM signal routes + 2 CanTP Tx + 2 CanTP Rx */

typedef enum {
    PDUR_COM,
    PDUR_CANIF,
    PDUR_CANTP      /* CanTP transport protocol module */
} PduR_ModuleIdType;

typedef struct {
    PduIdType globalPduId;
    PduR_ModuleIdType srcModule;
    PduIdType srcPduId;
    PduR_ModuleIdType dstModule;
    PduIdType dstPduId;
} PduR_RouteConfigType;

extern const PduR_RouteConfigType PduRRoute[PDUR_NUM_ROUTES];

#endif /* PDUR_CFG_H */
