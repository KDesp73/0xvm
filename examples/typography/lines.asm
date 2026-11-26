; Horizontal and vertical line helpers.
; Included by ../typography.asm.

; ------------------------------------------------------------------ lines

; [..., x, y, len, colour] -> [...]
hline:
    ENTER 6                 ; 0=x 1=y 2=len 3=colour 4=i
    PUSH 9
    PICK
    LOCAL_SET 0
    PUSH 8
    PICK
    LOCAL_SET 1
    PUSH 7
    PICK
    LOCAL_SET 2
    PUSH 6
    PICK
    LOCAL_SET 3
    LOCAL_GET 3
    SET_COLOR
    PUSH 0
    LOCAL_SET 4
hl_loop:
    LOCAL_GET 4
    LOCAL_GET 2
    CMP_GE
    JMP_Z hl_body
    JMP hl_done
hl_body:
    LOCAL_GET 0
    LOCAL_GET 4
    ADD
    LOCAL_GET 1
    PLOT_STACK
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP hl_loop
hl_done:
    PUSH 0
    RET

; [..., x, y, len, colour] -> [...]
vline:
    ENTER 6                 ; 0=x 1=y 2=len 3=colour 4=i
    PUSH 9
    PICK
    LOCAL_SET 0
    PUSH 8
    PICK
    LOCAL_SET 1
    PUSH 7
    PICK
    LOCAL_SET 2
    PUSH 6
    PICK
    LOCAL_SET 3
    LOCAL_GET 3
    SET_COLOR
    PUSH 0
    LOCAL_SET 4
vl_loop:
    LOCAL_GET 4
    LOCAL_GET 2
    CMP_GE
    JMP_Z vl_body
    JMP vl_done
vl_body:
    LOCAL_GET 0
    LOCAL_GET 1
    LOCAL_GET 4
    ADD
    PLOT_STACK
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP vl_loop
vl_done:
    PUSH 0
    RET

