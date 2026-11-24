; Executable conformance ROM for the frozen v2 machine.
; Each check jumps to `fail` on mismatch; a passing run prints exactly "OK\n".
; Only System version input and Console character output are required of a host.

        NOP

; PUSH / DUP / POP
        PUSH 5
        DUP
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 2
        POP
        PUSH 1
        CMP_EQ
        JMP_Z fail

; SWAP
        PUSH 1
        PUSH 2
        SWAP
        PUSH 1
        CMP_EQ
        JMP_Z fail
        PUSH 2
        CMP_EQ
        JMP_Z fail

; OVER
        PUSH 3
        PUSH 4
        OVER
        PUSH 3
        CMP_EQ
        JMP_Z fail
        POP
        PUSH 3
        CMP_EQ
        JMP_Z fail

; ROT
        PUSH 1
        PUSH 2
        PUSH 3
        ROT
        PUSH 1
        CMP_EQ
        JMP_Z fail
        POP
        PUSH 2
        CMP_EQ
        JMP_Z fail

; PICK
        PUSH 10
        PUSH 20
        PUSH 30
        PUSH 1
        PICK
        PUSH 20
        CMP_EQ
        JMP_Z fail
        POP
        PUSH 20
        CMP_EQ
        JMP_Z fail
        PUSH 10
        CMP_EQ
        JMP_Z fail

; ADD / SUB / MUL / DIV / MOD
        PUSH 5
        PUSH 2
        PUSH 3
        ADD
        CMP_EQ
        JMP_Z fail
        PUSH 5
        PUSH 9
        PUSH 4
        SUB
        CMP_EQ
        JMP_Z fail
        PUSH 42
        PUSH 6
        PUSH 7
        MUL
        CMP_EQ
        JMP_Z fail
        PUSH 5
        PUSH 20
        PUSH 4
        DIV
        CMP_EQ
        JMP_Z fail
        PUSH 3
        PUSH 23
        PUSH 4
        MOD
        CMP_EQ
        JMP_Z fail

; Comparisons
        PUSH 1
        PUSH 2
        PUSH 3
        CMP_LT
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 7
        PUSH 7
        CMP_EQ
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 7
        PUSH 8
        CMP_NE
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 7
        PUSH 7
        CMP_LE
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 8
        PUSH 7
        CMP_GT
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 7
        PUSH 7
        CMP_GE
        CMP_EQ
        JMP_Z fail

; Bitwise and logical
        PUSH -1
        PUSH 0
        NOT
        CMP_EQ
        JMP_Z fail
        PUSH 0x30
        PUSH 0xF0
        PUSH 0x3C
        AND
        CMP_EQ
        JMP_Z fail
        PUSH 0xFF
        PUSH 0xF0
        PUSH 0x0F
        OR
        CMP_EQ
        JMP_Z fail
        PUSH 0xF0
        PUSH 0xFF
        PUSH 0x0F
        XOR
        CMP_EQ
        JMP_Z fail
        PUSH 16
        PUSH 1
        PUSH 4
        SHL
        CMP_EQ
        JMP_Z fail
        PUSH 4
        PUSH 16
        PUSH 2
        SHR
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 0
        LNOT
        CMP_EQ
        JMP_Z fail
        PUSH 0
        PUSH 1
        PUSH 0
        LAND
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSH 0
        PUSH 1
        LOR
        CMP_EQ
        JMP_Z fail

; RAM widths and byte order
        PUSH 0x41
        PUSH 0x41
        PUSH 0
        STORE8
        PUSH 0
        LOAD8
        CMP_EQ
        JMP_Z fail
        PUSH 0x1234
        PUSH 0x1234
        PUSH 1
        STORE16
        PUSH 1
        LOAD16
        CMP_EQ
        JMP_Z fail
        PUSH -1
        PUSH -1
        PUSH 2
        STORE32
        PUSH 2
        LOAD32
        CMP_EQ
        JMP_Z fail
        PUSH 4294967295
        PUSH -1
        PUSH 8
        STORE32
        PUSH 8
        LOAD32U
        CMP_EQ
        JMP_Z fail
        PUSH 0x0102030405060708
        PUSH 0x0102030405060708
        PUSH 4
        STORE64
        PUSH 4
        LOAD64
        CMP_EQ
        JMP_Z fail

; Heap allocation and release
        PUSH 16
        ALLOC
        DUP
        PUSH 0x4000
        CMP_EQ
        JMP_Z fail
        FREE

; CALL / RET / ENTER / LOCAL_GET / LOCAL_SET
        PUSH 7
        CALL square
        SWAP
        POP
        PUSH 49
        CMP_EQ
        JMP_Z fail

; CALL_I
        PUSH 3
        PUSH square
        CALL_I
        SWAP
        POP
        PUSH 9
        CMP_EQ
        JMP_Z fail

; JMP_I skips a deliberately failing check
        PUSH jump_i_done
        JMP_I
        PUSH 0
        JMP_Z fail
jump_i_done:

; Floats
        PUSHF 3.75
        PUSHF 1.5
        PUSHF 2.25
        FADD
        CMP_EQ
        JMP_Z fail
        PUSHF 3.5
        PUSHF 5.0
        PUSHF 1.5
        FSUB
        CMP_EQ
        JMP_Z fail
        PUSHF 3.0
        PUSHF 1.5
        PUSHF 2.0
        FMUL
        CMP_EQ
        JMP_Z fail
        PUSHF 3.0
        PUSHF 6.0
        PUSHF 2.0
        FDIV
        CMP_EQ
        JMP_Z fail
        PUSHF -2.5
        PUSHF 2.5
        FNEG
        CMP_EQ
        JMP_Z fail
        PUSHF 2.5
        PUSHF -2.5
        FABS
        CMP_EQ
        JMP_Z fail
        PUSHF 3.0
        PUSHF 9.0
        FSQRT
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSHF 1.0
        PUSHF 2.0
        FCMP_LT
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSHF 2.0
        PUSHF 2.0
        FCMP_EQ
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSHF 2.0
        PUSHF 2.0
        FCMP_LE
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSHF 3.0
        PUSHF 2.0
        FCMP_GT
        CMP_EQ
        JMP_Z fail
        PUSH 1
        PUSHF 2.0
        PUSHF 2.0
        FCMP_GE
        CMP_EQ
        JMP_Z fail
        PUSH 3
        PUSHF 3.9
        FTOI
        CMP_EQ
        JMP_Z fail
        PUSHF -2.0
        PUSH -2
        ITOF
        CMP_EQ
        JMP_Z fail

; DEI reads a non-blocking System port. DEO is used by PRINT_CHAR below.
        PUSH 2
        DEI 0x00
        CMP_EQ
        JMP_Z fail

        JMP checks_done

fail:
        PUSH 70
        PRINT_CHAR
        PUSH 65
        PRINT_CHAR
        PUSH 73
        PRINT_CHAR
        PUSH 76
        PRINT_CHAR
        PUSH 10
        PRINT_CHAR
        HALT

checks_done:
        PUSH 79
        PRINT_CHAR
        PUSH 75
        PRINT_CHAR
        PUSH 10
        PRINT_CHAR
        HALT

; Squares the argument left by the caller, then returns the result.
; The caller must discard the original argument after the call.
square:
        ENTER 1
        PUSH 1
        PICK
        DUP
        MUL
        LOCAL_SET 0
        LOCAL_GET 0
        RET
