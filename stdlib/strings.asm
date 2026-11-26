; SPDX-License-Identifier: MIT
; Explicit-length string helpers. A string is a byte buffer addressed as
; (addr, len); no NUL terminator is required. Numeric formatting stays in the
; Console ports. This file is idempotent.
;
; Each multi-argument helper is a CALL/RET routine installed at the expansion
; site. Arguments are copied into locals so helper calls can nest freely.

INCLUDE "mem.asm"

IFNDEF STD_STRINGS
EQU STD_STRINGS 1

; [..., dst, src, n] -> [...]
; Copy n bytes from one string buffer to another.
MACRO STRCPY
    MEMCPY
ENDMACRO

; [..., a, alen, b, blen] -> [..., diff]
; Lexicographic byte comparison. Zero means equal; otherwise the sign of diff
; gives the order. Shorter-prefix strings sort before longer ones.
MACRO STRCMP
    JMP %%after
%%body:
    ENTER 4                     ; locals 0=a, 1=alen, 2=b, 3=blen
    PUSH 7
    PICK
    LOCAL_SET 0
    PUSH 6
    PICK
    LOCAL_SET 1
    PUSH 5
    PICK
    LOCAL_SET 2
    PUSH 4
    PICK
    LOCAL_SET 3
%%loop:
    LOCAL_GET 1
    JMP_Z %%alen_zero
    LOCAL_GET 3
    JMP_Z %%blen_zero
    LOCAL_GET 0
    LOAD8
    LOCAL_GET 2
    LOAD8
    SUB
    DUP
    JMP_Z %%equal_bytes
    RET
%%equal_bytes:
    POP
    LOCAL_GET 0
    INC
    LOCAL_SET 0
    LOCAL_GET 1
    DEC
    LOCAL_SET 1
    LOCAL_GET 2
    INC
    LOCAL_SET 2
    LOCAL_GET 3
    DEC
    LOCAL_SET 3
    JMP %%loop
%%alen_zero:
    PUSH 0
    LOCAL_GET 3
    SUB
    RET
%%blen_zero:
    LOCAL_GET 1
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., a, alen, b, blen] -> [..., bool]
; True (1) when the two strings are equal.
MACRO STREQUAL
    STRCMP
    PUSH 0
    CMP_EQ
ENDMACRO

; [..., dst, dlen, src, slen] -> [..., newlen]
; Append src to dst. The caller keeps dst; the result is the combined length.
MACRO STRCAT
    JMP %%after
%%body:
    ENTER 4                     ; locals 0=dst, 1=dlen, 2=src, 3=slen
    PUSH 7
    PICK
    LOCAL_SET 0
    PUSH 6
    PICK
    LOCAL_SET 1
    PUSH 5
    PICK
    LOCAL_SET 2
    PUSH 4
    PICK
    LOCAL_SET 3
    LOCAL_GET 0
    LOCAL_GET 1
    ADD
    LOCAL_GET 2
    LOCAL_GET 3
    MEMCPY
    LOCAL_GET 1
    LOCAL_GET 3
    ADD
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., dst, src, start, count] -> [..., count]
; Copy a slice of src into dst. Truncates when start+count exceeds src.
MACRO STRSLICE
    JMP %%after
%%body:
    ENTER 4                     ; locals 0=dst, 1=src, 2=start, 3=count
    PUSH 7
    PICK
    LOCAL_SET 0
    PUSH 6
    PICK
    LOCAL_SET 1
    PUSH 5
    PICK
    LOCAL_SET 2
    PUSH 4
    PICK
    LOCAL_SET 3
    LOCAL_GET 0
    LOCAL_GET 1
    LOCAL_GET 2
    ADD
    LOCAL_GET 3
    DUP
    JMP_Z %%zero
    MEMCPY
    LOCAL_GET 3
    RET
%%zero:
    LOCAL_GET 3
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., hay, hlen, needle, nlen] -> [..., index]
; First index of needle in hay, or -1. An empty needle matches at index 0.
MACRO STRFIND
    JMP %%after
%%body:
    ENTER 6                     ; locals 0=hay, 1=hlen, 2=needle, 3=nlen,
                                ; 4=i, 5=last possible start
    PUSH 9
    PICK
    LOCAL_SET 0
    PUSH 8
    PICK
    LOCAL_SET 1
    PUSH 7
    PICK
    LOCAL_SET 2
    PUSH 6
    PICK
    LOCAL_SET 3
    LOCAL_GET 3
    LOCAL_GET 1
    CMP_GT
    JMP_Z %%too_long
    PUSH -1
    RET
%%too_long:
    LOCAL_GET 1
    LOCAL_GET 3
    SUB
    LOCAL_SET 5
    PUSH 0
    LOCAL_SET 4
%%loop:
    LOCAL_GET 4
    LOCAL_GET 5
    CMP_LE
    JMP_Z %%not_found
    LOCAL_GET 0
    LOCAL_GET 4
    ADD
    LOCAL_GET 2
    LOCAL_GET 3
    MEMCMP
    JMP_Z %%found
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP %%loop
%%found:
    LOCAL_GET 4
    RET
%%not_found:
    PUSH -1
    RET
%%after:
    CALL %%body
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
    SWAP
    POP
ENDMACRO

; [..., s, len, ch] -> [..., index]
; First index of the low byte of ch in s, or -1.
MACRO STRCHR
    JMP %%after
%%body:
    ENTER 4                     ; locals 0=s, 1=len, 2=ch, 3=i
    PUSH 6
    PICK
    LOCAL_SET 0
    PUSH 5
    PICK
    LOCAL_SET 1
    PUSH 4
    PICK
    LOCAL_SET 2
    PUSH 0
    LOCAL_SET 3
%%loop:
    LOCAL_GET 3
    LOCAL_GET 1
    CMP_LT
    JMP_Z %%not_found
    LOCAL_GET 0
    LOCAL_GET 3
    ADD
    LOAD8
    LOCAL_GET 2
    CMP_EQ
    JMP_Z %%next
    LOCAL_GET 3
    RET
%%next:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP %%loop
%%not_found:
    PUSH -1
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

; [..., addr] -> [..., len]
; Length of a NUL-terminated string, excluding the terminator.
MACRO STRLENZ
    JMP %%after
%%body:
    ENTER 2                     ; locals 0=addr, 1=i
    PUSH 2
    PICK
    LOCAL_SET 0
    PUSH 0
    LOCAL_SET 1
%%loop:
    LOCAL_GET 0
    LOCAL_GET 1
    ADD
    LOAD8
    DUP
    JMP_Z %%found
    POP
    LOCAL_GET 1
    INC
    LOCAL_SET 1
    JMP %%loop
%%found:
    POP
    LOCAL_GET 1
    RET
%%after:
    CALL %%body
    SWAP
    POP
ENDMACRO

ENDIF
