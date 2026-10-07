#ifndef STD_TYPES_H
#define STD_TYPES_H

#include <stdint.h>

/* Standard AUTOSAR types */
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int8_t   sint8;
typedef int16_t  sint16;
typedef int32_t  sint32;
typedef uint8_t  boolean;

#ifndef TRUE
#define TRUE  1u
#endif

#ifndef FALSE
#define FALSE 0u
#endif

/* Return types */
typedef uint8 Std_ReturnType;
#define E_OK      0u
#define E_NOT_OK  1u

#ifndef NULL_PTR
#include <stddef.h>
#define NULL_PTR NULL
#endif

#endif /* STD_TYPES_H */
