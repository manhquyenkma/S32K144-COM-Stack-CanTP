#ifndef COMSTACK_TYPES_H
#define COMSTACK_TYPES_H

#include "Std_Types.h"

/* Global PDU Identifier type */
typedef uint16 PduIdType;

/* Length type for PDUs */
typedef uint16 PduLengthType;

/* PDU Information structure passed across layers */
typedef struct {
    uint8* SduDataPtr;
    uint8* MetaDataPtr;
    PduLengthType SduLength;
} PduInfoType;

/* Buffer Request Return type for PduR and Transport Protocol */
typedef enum {
    BUFREQ_OK = 0,
    BUFREQ_E_NOT_OK,
    BUFREQ_E_BUSY,
    BUFREQ_E_OVFLW
} BufReq_ReturnType;

#endif /* COMSTACK_TYPES_H */
