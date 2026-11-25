; Small scalar utilities. All are opt-in macros and use CALL/RET frames so
; arguments never leak onto the caller's stack. This file is idempotent.

IFNDEF STD_UTIL
EQU STD_UTIL 1

; [..., a, b] -> [..., min]
MACRO MIN
    JMP %%after
%%body:
    ENTER 2
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    LOCAL_GET 1
    CMP_LT
    JMP_Z %%use_b
    LOCAL_GET 0
    RET
%%use_b:
    LOCAL_GET 1
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., a, b] -> [..., max]
MACRO MAX
    JMP %%after
%%body:
    ENTER 2
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    LOCAL_GET 1
    CMP_GT
    JMP_Z %%use_b
    LOCAL_GET 0
    RET
%%use_b:
    LOCAL_GET 1
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., x, lo, hi] -> [..., clamped]
MACRO CLAMP
    JMP %%after
%%body:
    ENTER 3
    PUSH 5
    PICK
    LOCAL_SET 0
    PUSH 4
    PICK
    LOCAL_SET 1
    PUSH 3
    PICK
    LOCAL_SET 2
    LOCAL_GET 0
    LOCAL_GET 1
    CMP_LT
    JMP_Z %%not_low
    LOCAL_GET 1
    RET
%%not_low:
    LOCAL_GET 0
    LOCAL_GET 2
    CMP_GT
    JMP_Z %%in_range
    LOCAL_GET 2
    RET
%%in_range:
    LOCAL_GET 0
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

; [..., x] -> [..., abs(x)]
MACRO ABS
    JMP %%after
%%body:
    ENTER 1
    PUSH 1
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH 0
    CMP_LT
    JMP_Z %%positive
    PUSH 0
    LOCAL_GET 0
    SUB
    RET
%%positive:
    LOCAL_GET 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
ENDMACRO

; [..., n] -> [..., (n+7) & ~7]
MACRO ALIGN8
    JMP %%after
%%body:
    ENTER 1
    PUSH 1
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH 7
    ADD
    PUSH -8
    AND
    RET
%%after:
    CALL %%body
    SWAP
    POP
ENDMACRO

; [..., word, bit] -> [..., word with bit set]
MACRO BIT_SET
    JMP %%after
%%body:
    ENTER 2
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    PUSH 1
    LOCAL_GET 1
    SHL
    LOCAL_GET 0
    OR
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., word, bit] -> [..., word with bit cleared]
MACRO BIT_CLEAR
    JMP %%after
%%body:
    ENTER 2
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    PUSH 1
    LOCAL_GET 1
    SHL
    NOT
    LOCAL_GET 0
    AND
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., word, bit] -> [..., bool]
MACRO BIT_TEST
    JMP %%after
%%body:
    ENTER 2
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    PUSH 1
    LOCAL_GET 1
    SHL
    LOCAL_GET 0
    AND
    JMP_Z %%clear
    PUSH 1
    RET
%%clear:
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; A 64-bit RNG state lives at an address supplied by the caller. Seed it with
; any non-zero value before calling RNG_NEXT.

; [..., state_addr, seed] -> [...]
MACRO RNG_SEED
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
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., state_addr] -> [..., next]
; xorshift64*; returns a uniformly distributed uint64_t pattern.
MACRO RNG_NEXT
    JMP %%after
%%body:
    ENTER 2                     ; local 0=state_addr, 1=x
    PUSH 2
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    LOAD64
    LOCAL_SET 1
    LOCAL_GET 1
    LOCAL_GET 1
    PUSH 13
    SHL
    XOR
    LOCAL_SET 1
    LOCAL_GET 1
    LOCAL_GET 1
    PUSH 7
    SHR
    XOR
    LOCAL_SET 1
    LOCAL_GET 1
    LOCAL_GET 1
    PUSH 17
    SHL
    XOR
    LOCAL_SET 1
    LOCAL_GET 1
    LOCAL_GET 0
    STORE64
    LOCAL_GET 1
    PUSH 2685821657736338717
    MUL
    RET
%%after:
    CALL %%body
    SWAP
    POP
ENDMACRO

ENDIF
