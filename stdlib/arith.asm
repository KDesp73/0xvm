; Stack arithmetic conveniences. All are plain macros over core opcodes.
; This file is idempotent.

IFNDEF STD_ARITH
EQU STD_ARITH 1

MACRO INC
    PUSH 1
    ADD
ENDMACRO

MACRO DEC
    PUSH 1
    SUB
ENDMACRO

MACRO NEG
    PUSH 0
    SWAP
    SUB
ENDMACRO

ENDIF
