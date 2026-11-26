; =============================================================================
; Type — a graphical typography demo for the SDL3 and browser hosts.
;
; This is deliberately an example, not a stdlib module: the bitmap font and
; layout routines are self-contained and can be copied into an app that wants
; them. Arrows move the caret, printable keys (including space) insert at the
; caret, enter inserts a newline, and escape (or window close) exits.
;
; The implementation is split across examples/typography/; this file is the
; entry point that pulls the pieces together in emission order.
;
; Run with: ./build/0xvm-sdl -f examples/typography.asm [-W 320] [-H 240]
;       or: make wasm && serve build/ and open 0xvm-wasm.html?rom=typography.rom
; =============================================================================

INCLUDE "all.asm"

; Constants, strings, and the font table are emitted first, then the jump
; skips over them to the startup code below.
INCLUDE "typography/constants.asm"
INCLUDE "typography/strings.asm"
INCLUDE "typography/font.asm"

    JMP start

; Macros produce no bytes; keep them with the code modules that use them.
INCLUDE "typography/macros.asm"
INCLUDE "typography/glyphs.asm"
INCLUDE "typography/lines.asm"
INCLUDE "typography/layout.asm"
INCLUDE "typography/editing.asm"
INCLUDE "typography/render.asm"
INCLUDE "typography/main.asm"
