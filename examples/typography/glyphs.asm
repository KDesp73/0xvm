; Scaled glyph drawing for the typography demo.
; Included by ../typography.asm.

; ------------------------------------------------------------------ glyphs

; [..., x, y, code, colour] -> [...]
; Draws one scaled glyph. Codes outside printable ASCII become '?'.
draw_glyph:
    ENTER 12                ; 0=x 1=y 2=code 3=colour 4=glyph 5=row 6=col
                            ; 7=rowbits 8=sy 9=sx 10=px 11=py
    PUSH 15
    PICK
    LOCAL_SET 0
    PUSH 14
    PICK
    LOCAL_SET 1
    PUSH 13
    PICK
    LOCAL_SET 2
    PUSH 12
    PICK
    LOCAL_SET 3

    LOCAL_GET 2
    PUSH FONT_FIRST
    CMP_LT
    JMP_Z dg_code_ok_low
    PUSH 63                 ; '?'
    LOCAL_SET 2
dg_code_ok_low:
    LOCAL_GET 2
    PUSH FONT_LAST
    CMP_GT
    JMP_Z dg_code_ok_high
    PUSH 63
    LOCAL_SET 2
dg_code_ok_high:

    LOCAL_GET 3
    SET_COLOR
    LOCAL_GET 2
    PUSH FONT_FIRST
    SUB
    PUSH CELL_H
    MUL
    PUSH glyphs
    ADD
    LOCAL_SET 4

    PUSH 0
    LOCAL_SET 5
dg_row_loop:
    LOCAL_GET 5
    PUSH CELL_H
    CMP_GE
    JMP_Z dg_row_body
    JMP dg_done
dg_row_body:
    LOCAL_GET 4
    PUSH CELL_H
    DEC
    LOCAL_GET 5
    SUB
    ADD
    LOAD8
    LOCAL_SET 7
    PUSH 0
    LOCAL_SET 6
dg_col_loop:
    LOCAL_GET 6
    PUSH GLYPH_W
    CMP_GE
    JMP_Z dg_col_body
    JMP dg_row_next
dg_col_body:
    LOCAL_GET 7
    PUSH 7
    LOCAL_GET 6
    SUB
    SHR
    PUSH 1
    AND
    JMP_Z dg_col_next
    PUSH 0
    LOCAL_SET 8
dg_sy_loop:
    LOCAL_GET 8
    PUSH SCALE
    CMP_GE
    JMP_Z dg_sy_body
    JMP dg_col_next
dg_sy_body:
    PUSH 0
    LOCAL_SET 9
dg_sx_loop:
    LOCAL_GET 9
    PUSH SCALE
    CMP_GE
    JMP_Z dg_sx_body
    JMP dg_sy_next
dg_sx_body:
    LOCAL_GET 0
    LOCAL_GET 6
    PUSH SCALE
    MUL
    ADD
    LOCAL_GET 9
    ADD
    LOCAL_SET 10
    LOCAL_GET 1
    LOCAL_GET 5
    PUSH SCALE
    MUL
    ADD
    LOCAL_GET 8
    ADD
    LOCAL_SET 11
    LOCAL_GET 10
    PUSH G_W
    LOAD32
    CMP_LT
    JMP_Z dg_pixel_skip
    LOCAL_GET 11
    PUSH G_H
    LOAD32
    CMP_LT
    JMP_Z dg_pixel_skip
    LOCAL_GET 10
    LOCAL_GET 11
    PLOT_STACK
dg_pixel_skip:
    LOCAL_GET 9
    INC
    LOCAL_SET 9
    JMP dg_sx_loop
dg_sy_next:
    LOCAL_GET 8
    INC
    LOCAL_SET 8
    JMP dg_sy_loop
dg_col_next:
    LOCAL_GET 6
    INC
    LOCAL_SET 6
    JMP dg_col_loop
dg_row_next:
    LOCAL_GET 5
    INC
    LOCAL_SET 5
    JMP dg_row_loop
dg_done:
    PUSH 0
    RET

