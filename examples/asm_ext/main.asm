; SPDX-License-Identifier: MIT
; Assembler-extension demo.
;
; Exercises:
;   - nested INCLUDE with paths relative to the including file
;   - IF / ELIF / ELSE, IFDEF, IFNDEF
;   - MACRO parameters (%a, %n) and macro-local labels (%%loop, %%done)
;
; Expected output:
;   (42
;   3
;   2
;   1
;   2
;   1
;   )

INCLUDE "console.asm"

EQU DEMO_LEVEL 2
INCLUDE "lib/macros.asm"

IF DEMO_LEVEL >= 2
    PUSH OPEN_PAREN
    PRINT_CHAR
ELIF DEMO_LEVEL == 1
    PUSH 91                 ; '['
    PRINT_CHAR
ELSE
    PUSH 123                ; '{'
    PRINT_CHAR
ENDIF

PRINT_SUM 17, 25

COUNTDOWN 3
COUNTDOWN 2

IFDEF DEMO_LEVEL
    PUSH CLOSE_PAREN
    PRINT_CHAR
ENDIF

IFNDEF MISSING_FEATURE
    PUSH LINE_FEED
    PRINT_CHAR
ENDIF

HALT
