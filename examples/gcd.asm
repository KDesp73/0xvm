; Euclidean GCD using integer remainder and the extended comparison ops.
;
;   heap[0] = a (1071)
;   heap[4] = b (462)
; result: 21

        PUSH 1071
        PUSH 4
        ALLOC
        STORE32         ; heap[0] = a

        PUSH 462
        PUSH 4
        ALLOC
        STORE32         ; heap[4] = b

gcd_loop:
        PUSH 4
        LOAD32          ; b
        JMP_Z done      ; b == 0 => heap[0] is the gcd

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
        MIGRATE         ; carry the computed result to the destination
        PUSH 0
        LOAD32
        PRINT           ; 21
        HALT
