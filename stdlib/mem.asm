; SPDX-License-Identifier: MIT
; Raw memory helpers. These are opt-in source macros over LOAD*/STORE*; they
; do not allocate and never touch the heap pointer. This file is idempotent.
;
; All ranges are half-open byte ranges [addr, addr+n) and may live anywhere in
; RAM, including the static image area. Overlapping copies are not supported.
;
; `PICK` consumes an index from the stack, so `PUSH k / PICK` copies the value
; k slots below the top of the remaining stack.

INCLUDE "arith.asm"

IFNDEF STD_MEM
EQU STD_MEM 1

; [..., dst, src, n] -> [...]
; Copy n bytes from src to dst. Consumes all three arguments.
MACRO MEMCPY
%%loop:
    DUP
    JMP_Z %%done
    PUSH 1
    PICK
    LOAD8
    PUSH 3
    PICK
    STORE8
    ROT
    INC
    ROT
    ROT
    SWAP
    INC
    SWAP
    DEC
    JMP %%loop
%%done:
    POP
    POP
    POP
ENDMACRO

; [..., dst, byte, n] -> [...]
; Write n copies of the low byte of `byte` to dst. Consumes all three args.
MACRO MEMSET
%%loop:
    DUP
    JMP_Z %%done
    PUSH 1
    PICK
    PUSH 3
    PICK
    STORE8
    ROT
    INC
    ROT
    ROT
    DEC
    JMP %%loop
%%done:
    POP
    POP
    POP
ENDMACRO

; [..., dst, n] -> [...]
; Zero n bytes at dst. Consumes both arguments.
MACRO MEMZERO
    SWAP
    PUSH 0
    ROT
    MEMSET
ENDMACRO

; [..., a, b, n] -> [..., diff]
; Compare n bytes; result is a[0]-b[0] at the first difference, or 0 when all
; n bytes match. Only the sign of a non-zero result is meaningful. Consumes all
; three arguments and pushes the difference.
MACRO MEMCMP
%%loop:
    DUP
    JMP_Z %%zero
    PUSH 2
    PICK
    LOAD8
    PUSH 2
    PICK
    LOAD8
    SUB
    DUP
    JMP_Z %%next
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
    JMP %%done
%%next:
    POP
    ROT
    INC
    ROT
    ROT
    SWAP
    INC
    SWAP
    DEC
    JMP %%loop
%%zero:
    POP
    POP
    POP
    PUSH 0
%%done:
ENDMACRO

ENDIF
