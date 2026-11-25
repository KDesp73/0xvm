; Euclidean GCD using integer remainder and the extended comparison ops.
;
; The two operands live in the addressable zero page:
;   mem[0..3] = a (1071)
;   mem[4..7] = b (462)
; result: 21

INCLUDE "console.asm"

        PUSH 1071
        PUSH 0
        STORE32         ; mem[0..3] = a

        PUSH 462
        PUSH 4
        STORE32         ; mem[4..7] = b

gcd_loop:
        PUSH 4
        LOAD32          ; b
        JMP_Z done      ; b == 0 => mem[0..3] is the gcd

        PUSH 0
        LOAD32          ; a
        PUSH 4
        LOAD32          ; a, b
        MOD             ; r = a % b

        DUP             ; r, r
        PUSH 4
        LOAD32          ; r, r, b_old
        PUSH 0
        STORE32         ; a = b_old; stack: r, r
        PUSH 4
        STORE32         ; b = r;     stack: r
        POP

        JMP gcd_loop

done:
        PUSH 0
        LOAD32
        PRINT           ; 21
        HALT
