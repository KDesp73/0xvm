; SPDX-License-Identifier: MIT
; Reusable macros. This file also pulls in a sibling include, so the demo
; exercises nested relative INCLUDE paths.
INCLUDE "constants.asm"

; PRINT_SUM 17, 25 -> 42
MACRO PRINT_SUM a, b
    PUSH %a
    PUSH %b
    ADD
    PRINT
ENDMACRO

; Prints %n down to 1. %%loop / %%done are unique per invocation, so this
; macro can be called more than once without duplicate-label errors.
MACRO COUNTDOWN n
    PUSH %n
%%loop:
    DUP
    PRINT
    PUSH 1
    SUB
    DUP
    JMP_Z %%done
    JMP %%loop
%%done:
    POP
ENDMACRO
