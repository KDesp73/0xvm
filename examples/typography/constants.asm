; SPDX-License-Identifier: MIT
; Typography demo layout and key-code constants.
; Included by ../typography.asm.

; ------------------------------------------------------------------ constants

EQU SCALE        2
EQU GLYPH_W      5
EQU CELL_W       GLYPH_W+1
EQU CELL_H       8
EQU PIXEL_W      CELL_W*SCALE
EQU PIXEL_H      CELL_H*SCALE
EQU BORDER       2
EQU BAR_H        PIXEL_H+4
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

EQU C_BG         0x101018
EQU C_PANEL      0x181828
EQU C_BORDER     0x405080
EQU C_TITLE      0x00E0FF
EQU C_TEXT       0xE8E8F0
EQU C_CARET      0xFFC040
EQU C_STATUS     0x90A0C0

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

