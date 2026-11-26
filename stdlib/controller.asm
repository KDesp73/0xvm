; Controller device conveniences (host-provided).
; Key codes: 0 = none, 1..4 = up/down/left/right, 5 = space,
; 6 = enter, 7 = escape, 8 = backspace, 9 = delete, 32..126 = printable ASCII.

INCLUDE "ports.asm"

IFNDEF STD_CONTROLLER
EQU STD_CONTROLLER 1

; -> [..., key code] (consumes the queued key-down)
MACRO READ_KEY
    DEI CONTROLLER_KEY
ENDMACRO

; -> [..., 1] if the host window was closed, else [..., 0]
MACRO QUIT_REQUESTED
    DEI CONTROLLER_QUIT
ENDMACRO

ENDIF
