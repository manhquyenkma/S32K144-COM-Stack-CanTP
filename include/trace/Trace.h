#ifndef TRACE_H
#define TRACE_H

#include "Std_Types.h"

#define TRACE_FLUSH_MAX_BYTES_PER_CALL 64

void Trace_Init(void);
void Trace_Enqueue(const char* fmt, ...);
void Trace_Flush(void);
void Trace_FlushBlocking(void);
char Trace_GetChar(void);
void Trace_SetEnabled(boolean enabled);
boolean Trace_IsEnabled(void);

extern volatile uint32 Trace_DroppedCount;

#define TRACE(fmt, ...) Trace_Enqueue(fmt "\r\n", ##__VA_ARGS__)

#endif /* TRACE_H */
