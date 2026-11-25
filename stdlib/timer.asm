; Timer device conveniences (host-provided).

INCLUDE "ports.asm"

IFNDEF STD_TIMER
EQU STD_TIMER 1

; -> [..., milliseconds since host start]
MACRO NOW_MS
    DEI TIMER_MS
ENDMACRO

ENDIF
