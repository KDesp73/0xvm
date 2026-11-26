; Text layout and caret-position mapping.
; Included by ../typography.asm.

; ------------------------------------------------------------------ layout

; [..., addr, len, x, y, cols, rows, colour] -> [...]
draw_text:
    ENTER 12                ; 0=addr 1=len 2=x 3=y 4=cols 5=rows 6=colour
                            ; 7=i 8=col 9=row 10=unused 11=unused
    PUSH 18
    PICK
    LOCAL_SET 0
    PUSH 17
    PICK
    LOCAL_SET 1
    PUSH 16
    PICK
    LOCAL_SET 2
    PUSH 15
    PICK
    LOCAL_SET 3
    PUSH 14
    PICK
    LOCAL_SET 4
    PUSH 13
    PICK
    LOCAL_SET 5
    PUSH 12
    PICK
    LOCAL_SET 6
    PUSH 0
    LOCAL_SET 7
    PUSH 0
    LOCAL_SET 8
    PUSH 0
    LOCAL_SET 9
dt_loop:
    LOCAL_GET 7
    LOCAL_GET 1
    CMP_GE
    JMP_Z dt_body
    JMP dt_done
dt_body:
    LOCAL_GET 0
    LOCAL_GET 7
    ADD
    LOAD8
    DUP
    PUSH NEWLINE
    CMP_EQ
    JMP_Z dt_char
    POP
    PUSH 0
    LOCAL_SET 8
    LOCAL_GET 9
    INC
    LOCAL_SET 9
    JMP dt_next
dt_char:
    LOCAL_GET 9
    LOCAL_GET 5
    CMP_LT
    JMP_Z dt_out_of_range
    JMP dt_draw
dt_out_of_range:
    POP                     ; drop code, row out of range
    JMP dt_wrap_only
dt_draw:
    LOCAL_GET 2
    LOCAL_GET 8
    PUSH PIXEL_W
    MUL
    ADD
    LOCAL_GET 3
    LOCAL_GET 9
    PUSH PIXEL_H
    MUL
    ADD
    ROT                     ; code, x, y -> x, y, code
    LOCAL_GET 6
    CALL draw_glyph
    POP
    POP
    POP
    POP
    POP
dt_wrap_only:
    LOCAL_GET 8
    INC
    LOCAL_SET 8
    LOCAL_GET 8
    LOCAL_GET 4
    CMP_GE
    JMP_Z dt_next
    PUSH 0
    LOCAL_SET 8
    LOCAL_GET 9
    INC
    LOCAL_SET 9
dt_next:
    LOCAL_GET 7
    INC
    LOCAL_SET 7
    JMP dt_loop
dt_done:
    PUSH 0
    RET

; [..., index] -> [...] and stores the visual cell in G_CX/G_CY.
caret_xy:
    ENTER 6                 ; 0=index 1=col 2=row 3=i 4=byte 5=unused
    PUSH 6
    PICK
    LOCAL_SET 0
    PUSH 0
    LOCAL_SET 1
    PUSH 0
    LOCAL_SET 2
    PUSH 0
    LOCAL_SET 3
cx_loop:
    LOCAL_GET 3
    LOCAL_GET 0
    CMP_GE
    JMP_Z cx_body
    JMP cx_store
cx_body:
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 3
    ADD
    LOAD8
    LOCAL_SET 4
    LOCAL_GET 4
    PUSH NEWLINE
    CMP_EQ
    JMP_Z cx_char
    PUSH 0
    LOCAL_SET 1
    LOCAL_GET 2
    INC
    LOCAL_SET 2
    JMP cx_next
cx_char:
    LOCAL_GET 1
    INC
    LOCAL_SET 1
    LOCAL_GET 1
    PUSH G_COLS
    LOAD32
    CMP_GE
    JMP_Z cx_next
    PUSH 0
    LOCAL_SET 1
    LOCAL_GET 2
    INC
    LOCAL_SET 2
cx_next:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP cx_loop
cx_store:
    LOCAL_GET 1
    PUSH G_CX
    STORE32
    LOCAL_GET 2
    PUSH G_CY
    STORE32
    PUSH 0
    RET

