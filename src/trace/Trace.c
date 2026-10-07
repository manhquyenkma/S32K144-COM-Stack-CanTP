#include "trace/Trace.h"
#include "device_registers.h"
#include <stdarg.h>

#define TRACE_RING_BUFFER_SIZE 1024
#define TRACE_TEMP_BUFFER_SIZE 128

static uint8 ring_buffer[TRACE_RING_BUFFER_SIZE];
static uint16 head = 0;
static uint16 tail = 0;
volatile uint32 Trace_DroppedCount = 0;
static boolean g_traceEnabled = TRUE;

void Trace_SetEnabled(boolean enabled) {
    g_traceEnabled = enabled;
}

boolean Trace_IsEnabled(void) {
    return g_traceEnabled;
}

void Trace_Init(void) {
    /* Init LPUART1 for 115200 8N1 at 48MHz clock */
    LPUART1->CTRL = 0; /* Disable everything during setup */
    LPUART1->BAUD = LPUART_BAUD_SBR(26) | LPUART_BAUD_OSR(15);
    LPUART1->CTRL = LPUART_CTRL_TE_MASK | LPUART_CTRL_RE_MASK;

    /* Reset terminal VT100 state: exit line-drawing mode (SI=0x0F, ESC(B), reset attributes */
    const char reset_seq[] = "\r\n\x1B[0m\x1B(B\x0F\r\n";
    for (int i = 0; reset_seq[i] != '\0'; i++) {
        while ((LPUART1->STAT & LPUART_STAT_TDRE_MASK) == 0U) {}
        LPUART1->DATA = (uint8)reset_seq[i];
    }
}

/* ----------------------------------------------------------------
 * Lightweight printf-style formatter — NO heap / NO malloc.
 *
 * Supported specifiers: %s  %u  %d  %x  %%
 * Width, padding, precision are intentionally omitted to keep
 * code size minimal for a bare-metal trace channel.
 * ---------------------------------------------------------------- */
static int trace_uint_to_str(char* buf, int pos, int max, uint32 val, int base) {
    char digits[10]; /* 32-bit max = 4294967295 = 10 digits */
    int  n = 0;
    if (val == 0U) {
        if (pos < max) buf[pos] = '0';
        return pos + 1;
    }
    while (val > 0U && n < 10) {
        uint32 d = val % (uint32)base;
        digits[n++] = (d < 10U) ? (char)('0' + d) : (char)('a' + d - 10U);
        val /= (uint32)base;
    }
    for (int i = n - 1; i >= 0; i--) {
        if (pos < max) buf[pos] = digits[i];
        pos++;
    }
    return pos;
}

void Trace_Enqueue(const char* fmt, ...) {
    if (!g_traceEnabled) {
        return;
    }
    char temp_buf[TRACE_TEMP_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);

    int pos = 0;
    int max = TRACE_TEMP_BUFFER_SIZE - 1;

    while (*fmt != '\0' && pos < max) {
        if (*fmt != '%') {
            temp_buf[pos++] = *fmt++;
            continue;
        }
        fmt++; /* skip '%' */
        switch (*fmt) {
        case 's': {
            const char* s = va_arg(args, const char*);
            if (s == (void*)0) s = "(null)";
            while (*s != '\0' && pos < max) { temp_buf[pos++] = *s++; }
            break;
        }
        case 'u': {
            uint32 v = va_arg(args, uint32);
            pos = trace_uint_to_str(temp_buf, pos, max, v, 10);
            break;
        }
        case 'd': {
            sint32 v = va_arg(args, sint32);
            if (v < 0) { if (pos < max) temp_buf[pos++] = '-'; v = -v; }
            pos = trace_uint_to_str(temp_buf, pos, max, (uint32)v, 10);
            break;
        }
        case 'X':
        case 'x': {
            uint32 v = va_arg(args, uint32);
            pos = trace_uint_to_str(temp_buf, pos, max, v, 16);
            break;
        }
        case '0': {
            if (*(fmt+1) == '4' && (*(fmt+2) == 'X' || *(fmt+2) == 'x')) {
                uint32 v = va_arg(args, uint32);
                for (int s = 12; s >= 0; s -= 4) {
                    uint32 nibble = (v >> s) & 0xFU;
                    if (pos < max) temp_buf[pos++] = (char)((nibble < 10U) ? ('0' + nibble) : ('A' + nibble - 10U));
                }
                fmt += 2; /* Skip the '04', the outer loop will skip 'X' */
            } else {
                temp_buf[pos++] = '%';
                if (pos < max) temp_buf[pos++] = *fmt;
            }
            break;
        }
        case '%':
            temp_buf[pos++] = '%';
            break;
        default:
            /* Unknown specifier — emit as-is */
            temp_buf[pos++] = '%';
            if (pos < max) temp_buf[pos++] = *fmt;
            break;
        }
        fmt++;
    }
    va_end(args);

    int len = (pos > max) ? max : pos;
    if (len <= 0) return;

    /* Check free space */
    uint16 used;
    if (head >= tail) {
        used = head - tail;
    } else {
        used = TRACE_RING_BUFFER_SIZE - tail + head;
    }
    uint16 free_space = TRACE_RING_BUFFER_SIZE - 1 - used; /* Keep 1 byte empty */

    if (len > free_space) {
        Trace_DroppedCount++;
        return; /* Drop entire message if ring buffer full */
    }

    /* Copy to ring buffer */
    for (int i = 0; i < len; i++) {
        ring_buffer[head] = temp_buf[i];
        head = (head + 1) % TRACE_RING_BUFFER_SIZE;
    }

    /* Immediately attempt to flush to UART hardware */
    Trace_Flush();
}

void Trace_Flush(void) {
    uint8 count = 0;
    while ((head != tail) && (count < TRACE_FLUSH_MAX_BYTES_PER_CALL)) {
        /* Non-blocking UART check: transmit only if hardware TX buffer is ready */
        if ((LPUART1->STAT & LPUART_STAT_TDRE_MASK) == 0U) {
            break; /* Hardware TX buffer busy, exit immediately without blocking execution */
        }
        LPUART1->DATA = ring_buffer[tail];
        tail = (tail + 1U) % TRACE_RING_BUFFER_SIZE;
        count++;
    }
}

void Trace_FlushBlocking(void) {
    while (head != tail) {
        /* Wait until hardware TX buffer is ready */
        while ((LPUART1->STAT & LPUART_STAT_TDRE_MASK) == 0U) {}
        LPUART1->DATA = ring_buffer[tail];
        tail = (tail + 1U) % TRACE_RING_BUFFER_SIZE;
    }
    /* Wait for transmission complete */
    while ((LPUART1->STAT & LPUART_STAT_TC_MASK) == 0U) {}
}

char Trace_GetChar(void) {
    uint32 stat = LPUART1->STAT;
    if ((stat & (LPUART_STAT_OR_MASK | LPUART_STAT_NF_MASK | 
                 LPUART_STAT_FE_MASK | LPUART_STAT_PF_MASK)) != 0U) {
        LPUART1->STAT = stat; /* Write 1 to clear */
    }
    if ((LPUART1->STAT & LPUART_STAT_RDRF_MASK) != 0U) {
        return (char)LPUART1->DATA;
    }
    return '\0';
}
