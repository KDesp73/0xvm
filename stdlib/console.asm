; Console device conveniences. Include ports.asm for the port names; every
; macro expands to the same DEI/DEO sequence as the former built-in pseudo-op.
; This file is idempotent.

INCLUDE "ports.asm"

IFNDEF STD_CONSOLE
EQU STD_CONSOLE 1

; [..., byte] -> write one character to stdout
MACRO PRINT_CHAR
    DEO CONSOLE_PUTC
ENDMACRO

; [..., int64] -> write a signed decimal integer and a newline
MACRO PRINT
    DEO CONSOLE_PUTI
ENDMACRO

; [..., double bits] -> write a double and a newline
MACRO FPRINT
    DEO CONSOLE_PUTF
ENDMACRO

; read one integer from stdin -> [..., int64]
MACRO READ_INT
    DEI CONSOLE_GETI
ENDMACRO

; [..., addr, len] -> write len bytes at addr, then a newline
; Address/length are latched by the preceding ports; PUSH 0 supplies the
; ignored value that DEO requires on the stack.
MACRO PRINT_STR
    DEO CONSOLE_PUTS_LEN
    DEO CONSOLE_PUTS_ADDR
    PUSH 0
    DEO CONSOLE_PUTS
ENDMACRO

; [..., addr, max] -> read a line into addr; leaves [..., addr, len]
; Address/max are latched by the preceding ports; PUSH 0 supplies the ignored
; DEO value, and GETS_LEN reads back the actual length.
MACRO READ_STR
    DEO CONSOLE_GETS_MAX
    DEO CONSOLE_GETS_ADDR
    PUSH 0
    DEO CONSOLE_GETS
    DEI CONSOLE_GETS_LEN
ENDMACRO

ENDIF
