; Screen device conveniences (host-provided; see docs/Machine-Contract.md).
; Include this once; it pulls in ports.asm for the port constants.

INCLUDE "ports.asm"

IFNDEF STD_SCREEN
EQU STD_SCREEN 1

; -> [..., width, height]
MACRO SCREEN_SIZE
    DEI SCREEN_WIDTH
    DEI SCREEN_HEIGHT
ENDMACRO

; [..., packed RGB] -> set the colour used by PLOT and CLEAR
MACRO SET_COLOR
    DEO SCREEN_COLOR
ENDMACRO

; [..., x, y] -> plot one pixel with the current colour
MACRO PLOT_XY x, y
    PUSH (%x << 16) | %y
    DEO SCREEN_PIXEL
ENDMACRO

; [..., packed x/y] -> plot one pixel (x in the high 16 bits, y in the low)
MACRO PLOT
    DEO SCREEN_PIXEL
ENDMACRO

; present the framebuffer (the DEO value is ignored)
MACRO DRAW
    PUSH 0
    DEO SCREEN_DRAW
ENDMACRO

; fill the framebuffer with the current colour (value ignored)
MACRO CLEAR
    PUSH 0
    DEO SCREEN_CLEAR
ENDMACRO

ENDIF
