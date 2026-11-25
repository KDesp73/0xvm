; Arena-backed collections. These are opt-in source macros; each routine uses
; CALL/RET frames and cleans up its arguments. Keys and vector cells are plain
; 64-bit integers, so no guest-side tagged values are needed. This file is
; idempotent.
;
; Vector header: arena addr, element size, count, data base.
; Map header:    arena addr, capacity, count, keys base, values base.
; A map key of 0 marks an empty slot and is therefore reserved.

INCLUDE "alloc.asm"

IFNDEF STD_COLLECTIONS
EQU STD_COLLECTIONS 1

; [..., vec, arena] -> [...]
MACRO VEC_INIT
    JMP %%after
%%body:
    ENTER 2
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    LOCAL_GET 1
    LOCAL_GET 0
    STORE64
    PUSH 8
    LOCAL_GET 0
    PUSH 8
    ADD
    STORE64
    PUSH 0
    LOCAL_GET 0
    PUSH 16
    ADD
    STORE64
    PUSH -1
    LOCAL_GET 0
    PUSH 24
    ADD
    STORE64
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., vec] -> [..., count]
MACRO VEC_LEN
    JMP %%after
%%body:
    ENTER 1
    PUSH 1
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH 16
    ADD
    LOAD64
    RET
%%after:
    CALL %%body
    SWAP
    POP
ENDMACRO

; [..., vec, value] -> [..., index]
; Returns -1 when the backing arena is full.
MACRO VEC_PUSH64
    JMP %%after
%%body:
    ENTER 6                     ; locals 2=arena, 3=count, 4=data, 5=addr
    PUSH 7
    PICK
    LOCAL_SET 0
    PUSH 6
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    LOAD64
    LOCAL_SET 2
    LOCAL_GET 0
    PUSH 16
    ADD
    LOAD64
    LOCAL_SET 3
    LOCAL_GET 0
    PUSH 24
    ADD
    LOAD64
    LOCAL_SET 4
    LOCAL_GET 2
    PUSH 8
    ARENA_ALLOC
    LOCAL_SET 5
    LOCAL_GET 5
    PUSH -1
    CMP_EQ
    JMP_Z %%got_addr
    PUSH -1
    RET
%%got_addr:
    LOCAL_GET 4
    PUSH -1
    CMP_EQ
    JMP_Z %%have_data
    LOCAL_GET 5
    LOCAL_GET 0
    PUSH 24
    ADD
    STORE64
%%have_data:
    LOCAL_GET 1
    LOCAL_GET 5
    STORE64
    LOCAL_GET 3
    INC
    LOCAL_GET 0
    PUSH 16
    ADD
    STORE64
    LOCAL_GET 3
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., vec, index] -> [..., value]
MACRO VEC_GET64
    JMP %%after
%%body:
    ENTER 4                     ; local 2=data
    PUSH 5
    PICK
    LOCAL_SET 0
    PUSH 4
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    PUSH 24
    ADD
    LOAD64
    LOCAL_SET 2
    LOCAL_GET 2
    LOCAL_GET 1
    PUSH 8
    MUL
    ADD
    LOAD64
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., vec, index, value] -> [...]
MACRO VEC_SET64
    JMP %%after
%%body:
    ENTER 5                     ; local 3=data
    PUSH 7
    PICK
    LOCAL_SET 0
    PUSH 6
    PICK
    LOCAL_SET 1
    PUSH 5
    PICK
    LOCAL_SET 2
    LOCAL_GET 0
    PUSH 24
    ADD
    LOAD64
    LOCAL_SET 3
    LOCAL_GET 3
    LOCAL_GET 1
    PUSH 8
    MUL
    ADD
    LOCAL_GET 2
    SWAP
    STORE64
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., map, arena, capacity] -> [...]
MACRO MAP_INIT
    JMP %%after
%%body:
    ENTER 6                     ; locals 3=keys, 4=values
    PUSH 8
    PICK
    LOCAL_SET 0
    PUSH 7
    PICK
    LOCAL_SET 1
    PUSH 6
    PICK
    LOCAL_SET 2
    LOCAL_GET 1
    LOCAL_GET 2
    PUSH 8
    MUL
    ARENA_ALLOC
    LOCAL_SET 3
    LOCAL_GET 3
    LOCAL_GET 2
    PUSH 8
    MUL
    MEMZERO
    LOCAL_GET 1
    LOCAL_GET 2
    PUSH 8
    MUL
    ARENA_ALLOC
    LOCAL_SET 4
    LOCAL_GET 4
    LOCAL_GET 2
    PUSH 8
    MUL
    MEMZERO
    LOCAL_GET 1
    LOCAL_GET 0
    STORE64
    LOCAL_GET 2
    LOCAL_GET 0
    PUSH 8
    ADD
    STORE64
    PUSH 0
    LOCAL_GET 0
    PUSH 16
    ADD
    STORE64
    LOCAL_GET 3
    LOCAL_GET 0
    PUSH 24
    ADD
    STORE64
    LOCAL_GET 4
    LOCAL_GET 0
    PUSH 32
    ADD
    STORE64
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., map, key] -> [..., value]
; Returns 0 when key is absent.
MACRO MAP_GET
    JMP %%after
%%body:
    ENTER 7                     ; locals 2=cap, 3=keys, 4=values, 5=slot, 6=i
    PUSH 8
    PICK
    LOCAL_SET 0
    PUSH 7
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    PUSH 8
    ADD
    LOAD64
    LOCAL_SET 2
    LOCAL_GET 0
    PUSH 24
    ADD
    LOAD64
    LOCAL_SET 3
    LOCAL_GET 0
    PUSH 32
    ADD
    LOAD64
    LOCAL_SET 4
    LOCAL_GET 1
    PUSH 0x7FFFFFFFFFFFFFFF
    AND
    LOCAL_GET 2
    MOD
    LOCAL_SET 5
    PUSH 0
    LOCAL_SET 6
%%loop:
    LOCAL_GET 6
    LOCAL_GET 2
    CMP_LT
    JMP_Z %%not_found
    LOCAL_GET 3
    LOCAL_GET 5
    PUSH 8
    MUL
    ADD
    LOAD64
    DUP
    JMP_Z %%not_found
    LOCAL_GET 1
    CMP_EQ
    JMP_Z %%advance
    LOCAL_GET 4
    LOCAL_GET 5
    PUSH 8
    MUL
    ADD
    LOAD64
    RET
%%advance:
    LOCAL_GET 5
    INC
    LOCAL_GET 2
    MOD
    LOCAL_SET 5
    LOCAL_GET 6
    INC
    LOCAL_SET 6
    JMP %%loop
%%not_found:
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., map, key, value] -> [..., bool]
; Key 0 is reserved. Returns 0 when the table is full.
MACRO MAP_SET
    JMP %%after
%%body:
    ENTER 9                     ; locals 3=cap, 4=keys, 5=values,
                                ; 6=slot, 7=i, 8=stored key
    PUSH 11
    PICK
    LOCAL_SET 0
    PUSH 10
    PICK
    LOCAL_SET 1
    PUSH 9
    PICK
    LOCAL_SET 2
    LOCAL_GET 1
    JMP_Z %%bad_key
    LOCAL_GET 0
    PUSH 8
    ADD
    LOAD64
    LOCAL_SET 3
    LOCAL_GET 0
    PUSH 24
    ADD
    LOAD64
    LOCAL_SET 4
    LOCAL_GET 0
    PUSH 32
    ADD
    LOAD64
    LOCAL_SET 5
    LOCAL_GET 1
    PUSH 0x7FFFFFFFFFFFFFFF
    AND
    LOCAL_GET 3
    MOD
    LOCAL_SET 6
    PUSH 0
    LOCAL_SET 7
%%loop:
    LOCAL_GET 7
    LOCAL_GET 3
    CMP_LT
    JMP_Z %%full
    LOCAL_GET 4
    LOCAL_GET 6
    PUSH 8
    MUL
    ADD
    LOAD64
    DUP
    JMP_Z %%empty
    LOCAL_GET 1
    CMP_EQ
    JMP_Z %%advance
    LOCAL_GET 2
    LOCAL_GET 5
    LOCAL_GET 6
    PUSH 8
    MUL
    ADD
    STORE64
    PUSH 1
    RET
%%empty:
    LOCAL_GET 1
    LOCAL_GET 4
    LOCAL_GET 6
    PUSH 8
    MUL
    ADD
    STORE64
    LOCAL_GET 2
    LOCAL_GET 5
    LOCAL_GET 6
    PUSH 8
    MUL
    ADD
    STORE64
    LOCAL_GET 0
    PUSH 16
    ADD
    LOAD64
    INC
    LOCAL_GET 0
    PUSH 16
    ADD
    STORE64
    PUSH 1
    RET
%%advance:
    LOCAL_GET 6
    INC
    LOCAL_GET 3
    MOD
    LOCAL_SET 6
    LOCAL_GET 7
    INC
    LOCAL_SET 7
    JMP %%loop
%%full:
    PUSH 0
    RET
%%bad_key:
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

ENDIF
