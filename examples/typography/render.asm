; SPDX-License-Identifier: MIT
; Frame rendering for the typography demo.
; Included by ../typography.asm.

; ------------------------------------------------------------------ rendering

; [..., quit flag] -> [...]
draw_frame:
    PUSH C_BG
    SET_COLOR
    CLEAR

    PUSH title_text
    DUP
    STRLENZ
    PUSH 4
    PUSH 4
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_TITLE
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH BORDER
    PUSH BAR_H
    PUSH G_W
    LOAD32
    PUSH 2*BORDER
    SUB
    PUSH C_BORDER
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH BORDER
    PUSH G_H
    LOAD32
    PUSH BAR_H
    SUB
    PUSH 1
    SUB
    PUSH G_W
    LOAD32
    PUSH 2*BORDER
    SUB
    PUSH C_BORDER
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH BORDER
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_H
    LOAD32
    PUSH 2*BAR_H
    SUB
    PUSH 2
    SUB
    PUSH C_BORDER
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_W
    LOAD32
    PUSH BORDER
    SUB
    PUSH 1
    SUB
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_H
    LOAD32
    PUSH 2*BAR_H
    SUB
    PUSH 2
    SUB
    PUSH C_BORDER
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_TEXT
    LOAD32
    PUSH G_LEN
    LOAD32
    PUSH BORDER
    PUSH 1
    ADD
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_COLS
    LOAD32
    PUSH G_ROWS
    LOAD32
    PUSH C_TEXT
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    NOW_MS
    PUSH BLINK_MS
    DIV
    PUSH 1
    AND
    JMP_Z df_no_caret
    PUSH G_CARET
    LOAD32
    CALL caret_xy
    POP
    POP
    PUSH G_CY
    LOAD32
    PUSH G_ROWS
    LOAD32
    CMP_LT
    JMP_Z df_no_caret
    ; Draw the caret in the two-pixel gutter immediately before the current
    ; cell, so it sits between glyphs instead of on top of the next glyph.
    PUSH BORDER
    PUSH 1
    ADD
    PUSH G_CX
    LOAD32
    PUSH PIXEL_W
    MUL
    ADD
    PUSH 2
    SUB
    DUP                     ; keep x for the second half of the caret
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_CY
    LOAD32
    PUSH PIXEL_H
    MUL
    ADD
    PUSH PIXEL_H
    PUSH C_CARET
    CALL vline
    POP
    POP
    POP
    POP
    POP
    INC                     ; x+1: fill the whole inter-cell gutter
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_CY
    LOAD32
    PUSH PIXEL_H
    MUL
    ADD
    PUSH PIXEL_H
    PUSH C_CARET
    CALL vline
    POP
    POP
    POP
    POP
    POP
df_no_caret:

    PUSH status_text
    DUP
    STRLENZ
    PUSH 4
    PUSH G_H
    LOAD32
    PUSH BAR_H
    SUB
    PUSH 3
    ADD
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_STATUS
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    DRAW
    PUSH 0
    RET

