; SPDX-License-Identifier: MIT
; Frame rendering for the typography demo.
; Included by ../typography.asm.

; ------------------------------------------------------------------ rendering

; [..., x, y, w, h, colour] -> [..., 0]
; Solid fill used for the inverted 8px header strip.
fill_rect:
    ENTER 8                 ; 0=x 1=y 2=w 3=h 4=colour 5=row
    PUSH 12
    PICK
    LOCAL_SET 0
    PUSH 11
    PICK
    LOCAL_SET 1
    PUSH 10
    PICK
    LOCAL_SET 2
    PUSH 9
    PICK
    LOCAL_SET 3
    PUSH 8
    PICK
    LOCAL_SET 4
    PUSH 0
    LOCAL_SET 5
fr_loop:
    LOCAL_GET 5
    LOCAL_GET 3
    CMP_GE
    JMP_Z fr_body
    JMP fr_done
fr_body:
    LOCAL_GET 0
    LOCAL_GET 1
    LOCAL_GET 5
    ADD
    LOCAL_GET 2
    LOCAL_GET 4
    CALL hline
    POP
    POP
    POP
    POP
    POP
    LOCAL_GET 5
    INC
    LOCAL_SET 5
    JMP fr_loop
fr_done:
    PUSH 0
    RET

; [..., quit flag] -> [...]
draw_frame:
    ENTER 1                 ; 0=caret x while drawing the gutter bar
    PUSH C_CANVAS
    SET_COLOR
    CLEAR

    ; Inverted header strip: title left, editing mode right.
    PUSH 0
    PUSH 0
    PUSH G_W
    LOAD32
    PUSH HEADER_H
    PUSH C_INK
    CALL fill_rect
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH title_text
    DUP
    STRLENZ
    PUSH TILE
    PUSH 0
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_CANVAS
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH G_W
    LOAD32
    PUSH 4                  ; "EDIT"
    PUSH PIXEL_W
    MUL
    SUB
    PUSH TILE
    SUB
    PUSH mode_text
    PUSH 4
    ROT
    PUSH 0
    PUSH 4
    PUSH 1
    PUSH C_CANVAS
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    ; Hairline content box in the structural colour.
    PUSH BOX_X
    PUSH BOX_TOP
    PUSH G_W
    LOAD32
    PUSH BOX_X*2
    SUB
    PUSH C_STRUCT
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH BOX_X
    PUSH G_H
    LOAD32
    PUSH TILE
    SUB
    PUSH G_W
    LOAD32
    PUSH BOX_X*2
    SUB
    PUSH C_STRUCT
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH BOX_X
    PUSH BOX_TOP+1
    PUSH G_H
    LOAD32
    PUSH BOX_TOP+TILE+1
    SUB
    PUSH C_STRUCT
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_W
    LOAD32
    PUSH TILE+1
    SUB
    PUSH BOX_TOP+1
    PUSH G_H
    LOAD32
    PUSH BOX_TOP+TILE+1
    SUB
    PUSH C_STRUCT
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
    PUSH TEXT_X
    PUSH TEXT_Y
    PUSH G_COLS
    LOAD32
    PUSH G_ROWS
    LOAD32
    PUSH C_INK
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

    ; The caret lives in the 3px gutter after the glyph: an ink outline keeps
    ; the accent bar readable against both canvas and glyph pixels.
    PUSH TEXT_X
    PUSH G_CX
    LOAD32
    PUSH PIXEL_W
    MUL
    ADD
    PUSH GLYPH_W
    ADD
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH TEXT_Y
    PUSH G_CY
    LOAD32
    PUSH PIXEL_H
    MUL
    ADD
    PUSH PIXEL_H
    PUSH C_INK
    CALL vline
    POP
    POP
    POP
    POP
    POP
    LOCAL_GET 0
    INC
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH TEXT_Y
    PUSH G_CY
    LOAD32
    PUSH PIXEL_H
    MUL
    ADD
    PUSH PIXEL_H
    PUSH C_ACCENT
    CALL vline
    POP
    POP
    POP
    POP
    POP
    LOCAL_GET 0
    INC
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH TEXT_Y
    PUSH G_CY
    LOAD32
    PUSH PIXEL_H
    MUL
    ADD
    PUSH PIXEL_H
    PUSH C_ACCENT
    CALL vline
    POP
    POP
    POP
    POP
    POP
df_no_caret:

    ; Accent status line, one tile above the bottom border.
    PUSH status_text
    DUP
    STRLENZ
    PUSH TEXT_X
    PUSH G_H
    LOAD32
    PUSH TILE*2
    SUB
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_ACCENT
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
