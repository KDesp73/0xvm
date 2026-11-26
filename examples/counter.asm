; SPDX-License-Identifier: MIT
; Accumulator demo: print 0..5, then print the total (60).
; Comments start with ';', '#', or '//'. Labels are byte addresses in RAM.
; The accumulator lives in the addressable zero page at byte address 0.

INCLUDE "console.asm"

        PUSH 0          ; initial accumulator value
        PUSH 0          ; zero-page byte address
        STORE32         ; mem[0..3] = 0

        PUSH 0          ; loop counter i
loop:
        DUP
        PUSH 6
        CMP_LT
        JMP_Z done      ; exit loop when i >= 6

        DUP
        PRINT           ; print i
        PUSH 0
        LOAD32
        DUP
        PUSH 10
        ADD
        PUSH 0
        STORE32         ; mem[0..3] += 10
        POP
        PUSH 1
        ADD
        JMP loop

done:
        PUSH 0
        LOAD32
        PRINT           ; print 60
        HALT
