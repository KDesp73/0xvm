; SPDX-License-Identifier: MIT
; Typography demo layout and key-code constants.
; Included by ../typography.asm.

; ------------------------------------------------------------------ constants

; House grid: 8x8 cells, 5px glyph ink plus a 3px gutter. Keeping SCALE at 1
; preserves the shared 5x8 font used by examples/boot.asm.
EQU SCALE        1
EQU GLYPH_W      5
EQU CELL_W       8
EQU CELL_H       8
EQU PIXEL_W      CELL_W*SCALE
EQU PIXEL_H      CELL_H*SCALE
EQU TILE         8

; Header strip, content box, text, and status row all land on the tile grid.
EQU HEADER_H     TILE
EQU BOX_X        TILE
EQU BOX_TOP      TILE*2
EQU TEXT_X       TILE*2
EQU TEXT_Y       TILE*3
EQU TEXT_MARGIN  TILE*4       ; H - 40 leaves room for header/box/status

IFNDEF FRAME_MS
EQU FRAME_MS     66           ; ~15 updates/second
ENDIF
EQU BLINK_MS     250
IFNDEF MAX_FRAMES
EQU MAX_FRAMES   0            ; 0 = unlimited; smoke builds override this
ENDIF
EQU TEXT_CAP     1024
EQU FONT_FIRST   32
EQU FONT_LAST    126

EQU KEY_UP        1
EQU KEY_DOWN      2
EQU KEY_LEFT      3
EQU KEY_RIGHT     4
EQU KEY_SPACE     5
EQU KEY_ENTER     6
EQU KEY_ESC       7
EQU KEY_BACKSPACE 8
EQU KEY_DELETE    9
EQU NEWLINE       10

; Deep Ink house palette: canvas, ink, structure, accent.
EQU C_CANVAS     0x060907
EQU C_INK        0xF2F6F0
EQU C_STRUCT     0x2A352C
EQU C_ACCENT     0xB9E5AD

; Zero-page globals (absolute byte addresses).
EQU G_W          0            ; framebuffer width
EQU G_H          4            ; framebuffer height
EQU G_COLS       8            ; text columns
EQU G_ROWS       12           ; text rows
EQU G_TEXT       16           ; heap address of the byte buffer
EQU G_LEN        20           ; buffer length
EQU G_CARET      24           ; insertion index
EQU G_LAST       28           ; timestamp of the last frame
EQU G_FRAMES     32           ; frames drawn (for bounded smoke runs)
EQU G_CX         36           ; caret column
EQU G_CY         40           ; caret row
EQU G_DIRTY      44           ; redraw requested
EQU G_SLEEP      48           ; idle sleep ms

