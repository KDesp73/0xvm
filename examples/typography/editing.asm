; SPDX-License-Identifier: MIT
; Text-buffer insertion and deletion.
; Included by ../typography.asm.

; ------------------------------------------------------------------ editing

; [..., byte] -> [..., 1 on insert, 0 when full]
insert_byte:
    ENTER 2                 ; 0=i 1=byte
    PUSH 2
    PICK
    LOCAL_SET 1
    PUSH G_LEN
    LOAD32
    PUSH TEXT_CAP
    CMP_GE
    JMP_Z ib_space
    PUSH 0
    RET
ib_space:
    PUSH G_LEN
    LOAD32
    LOCAL_SET 0
ib_shift_loop:
    LOCAL_GET 0
    PUSH G_CARET
    LOAD32
    CMP_GT
    JMP_Z ib_shift_done
ib_shift_do:
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 0
    PUSH 1
    SUB
    ADD
    LOAD8
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 0
    ADD
    STORE8
    LOCAL_GET 0
    DEC
    LOCAL_SET 0
    JMP ib_shift_loop
ib_shift_done:
    LOCAL_GET 1
    PUSH G_CARET
    LOAD32
    PUSH G_TEXT
    LOAD32
    ADD
    STORE8
    PUSH G_LEN
    LOAD32
    INC
    PUSH G_LEN
    STORE32
    PUSH G_CARET
    LOAD32
    INC
    PUSH G_CARET
    STORE32
    PUSH 1
    RET

; [..., index] -> [..., 1 on delete, 0 when out of range]
delete_at:
    ENTER 2                 ; 0=index 1=i
    PUSH 2
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH 0
    CMP_GE
    JMP_Z da_bad
    LOCAL_GET 0
    PUSH G_LEN
    LOAD32
    CMP_LT
    JMP_Z da_bad
    LOCAL_GET 0
    LOCAL_SET 1
da_shift_loop:
    LOCAL_GET 1
    PUSH G_LEN
    LOAD32
    PUSH 1
    SUB
    CMP_LT
    JMP_Z da_shift_done
da_shift_do:
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 1
    PUSH 1
    ADD
    ADD
    LOAD8
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 1
    ADD
    STORE8
    LOCAL_GET 1
    INC
    LOCAL_SET 1
    JMP da_shift_loop
da_shift_done:
    PUSH G_LEN
    LOAD32
    DEC
    PUSH G_LEN
    STORE32
    PUSH 1
    RET
da_bad:
    PUSH 0
    RET

