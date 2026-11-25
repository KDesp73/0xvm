; Heap helpers and a simple arena policy. The core ALLOC/FREE opcodes round to
; 8 bytes and reuse dead blocks; these macros add zeroing, arrays, and a
; bump-pointer arena backed by one core block. This file is idempotent.
;
; Arena header layout: base, used, capacity (three 64-bit cells).
;
; A refcounted object can use the same discipline: ALLOC_ZERO (payload + 8)
; and keep the count in the first cell, returning base + 8 to callers. Increment
; the count on every new owner and decrement it when an owner releases the
; object; the final decrement calls FREE_PTR on base. This is a convention, not
; a VM feature, so hosts cannot accidentally manage guest lifetimes.

INCLUDE "mem.asm"

IFNDEF STD_ALLOC
EQU STD_ALLOC 1

; [..., n] -> [..., addr]
; Allocate n bytes and zero them.
MACRO ALLOC_ZERO
    DUP
    ALLOC
    SWAP
    OVER
    SWAP
    MEMZERO
ENDMACRO

; [..., count, size] -> [..., addr]
; Allocate and zero an array of count cells of size bytes.
MACRO ALLOC_ARRAY
    MUL
    ALLOC_ZERO
ENDMACRO

; [..., addr] -> [...]
; Release a block returned by ALLOC/ALLOC_ZERO/ALLOC_ARRAY.
MACRO FREE_PTR
    FREE
ENDMACRO

; An arena is three 64-bit cells at `arena`: base, used, and capacity.
; Pass the address of those cells to the macros below.

; [..., arena, bytes] -> [..., base]
; Allocate one core block and initialise the arena header.
MACRO ARENA_INIT
    JMP %%after
%%body:
    ENTER 2                     ; locals 0=arena, 1=bytes
    PUSH 3
    PICK
    LOCAL_SET 0
    PUSH 2
    PICK
    LOCAL_SET 1
    LOCAL_GET 1
    ALLOC
    DUP
    LOCAL_GET 0
    STORE64                     ; base
    PUSH 0
    LOCAL_GET 0
    PUSH 8
    ADD
    STORE64                     ; used
    LOCAL_GET 1
    LOCAL_GET 0
    PUSH 16
    ADD
    STORE64                     ; capacity
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., arena, n] -> [..., addr]
; Bump n bytes (rounded to 8) from the arena. Returns -1 when full.
MACRO ARENA_ALLOC
    JMP %%after
%%body:
    ENTER 4                     ; locals 0=arena, 1=n, 2=used, 3=base
    PUSH 5
    PICK
    LOCAL_SET 0
    PUSH 4
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    PUSH 8
    ADD
    LOAD64
    LOCAL_SET 2
    LOCAL_GET 0
    LOAD64
    LOCAL_SET 3
    LOCAL_GET 1
    PUSH 7
    ADD
    PUSH -8
    AND
    LOCAL_SET 1
    LOCAL_GET 2
    LOCAL_GET 1
    ADD
    LOCAL_GET 0
    PUSH 16
    ADD
    LOAD64
    CMP_GT
    JMP_Z %%fits
    PUSH -1
    RET
%%fits:
    LOCAL_GET 3
    LOCAL_GET 2
    ADD                         ; addr
    DUP
    LOCAL_GET 2
    LOCAL_GET 1
    ADD
    LOCAL_GET 0
    PUSH 8
    ADD
    STORE64                     ; used += aligned n
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., arena] -> [...]
; Release the arena's core block and clear its header.
MACRO ARENA_FREE
    JMP %%after
%%body:
    ENTER 1                     ; local 0=arena
    PUSH 1
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    LOAD64
    FREE
    PUSH 0
    LOCAL_GET 0
    STORE64
    PUSH 0
    LOCAL_GET 0
    PUSH 8
    ADD
    STORE64
    PUSH 0
    LOCAL_GET 0
    PUSH 16
    ADD
    STORE64
    PUSH 0
    RET
%%after:
    CALL %%body
    SWAP
    POP
ENDMACRO

ENDIF
