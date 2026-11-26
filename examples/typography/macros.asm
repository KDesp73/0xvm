; SPDX-License-Identifier: MIT
; Small helper macros for the typography demo.
; Included by ../typography.asm.

; ------------------------------------------------------------------ macros

; [..., x, y] -> plot one pixel, consuming both values.
MACRO PLOT_STACK
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    PLOT
ENDMACRO

