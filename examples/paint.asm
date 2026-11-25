; =============================================================================
; Paint — an interactive demo for the SDL3 host (0xvm-sdl).
;
; Arrow keys move the brush, space toggles painting, enter cycles the colour,
; 'c' clears the canvas, and escape (or closing the window) exits. A palette
; swatch at (1,0) always shows the selected colour; the blinking pixel at (0,0)
; is driven by the host timer.
;
; Screen, Controller, and timer ports are optional: 0xvm does not provide them,
; so this program is meant for 0xvm-sdl. The startup banner uses Console ports,
; which both hosts share.
; =============================================================================

INCLUDE "all.asm"

; Zero-page globals (absolute byte addresses).
EQU G_X      0     ; brush x
EQU G_Y      4     ; brush y
EQU G_COLOR  8     ; current palette index
EQU G_PAINT  12    ; 1 while the brush is down
EQU G_W      16    ; framebuffer width
EQU G_H      20    ; framebuffer height
EQU G_LAST   24    ; timestamp of the last drawn frame

; Controller key codes (see docs/Machine-Contract.md).
EQU KEY_UP    1
EQU KEY_DOWN  2
EQU KEY_LEFT  3
EQU KEY_RIGHT 4
EQU KEY_SPACE 5
EQU KEY_ENTER 6
EQU KEY_ESC   7
EQU KEY_C     99

EQU PALETTE_COUNT 8
EQU DRAW_COLORS   7   ; index 7 is the background, index 6 the cursor
EQU INDEX_GREEN   1
EQU INDEX_WHITE   6
EQU INDEX_BG      7
EQU FRAME_MS      33  ; roughly 30 updates per second
EQU BLINK_MS      250

palette:
    DQ 0xFF0000         ; 0 red
    DQ 0x00FF00         ; 1 green
    DQ 0x0000FF         ; 2 blue
    DQ 0xFFFF00         ; 3 yellow
    DQ 0x00FFFF         ; 4 cyan
    DQ 0xFF00FF         ; 5 magenta
    DQ 0xFFFFFF         ; 6 white (brush-up cursor)
    DQ 0x101010         ; 7 background

banner:
    STR "paint | arrows move | space brush | enter colour | c clear | esc quit"
    DB 0

; ------------------------------------------------------------------ startup

; NUL-terminated strings are printed with LOAD8 + PRINT_CHAR; no length needed.
    PUSH banner
print_banner:
    DUP
    LOAD8
    DUP
    JMP_Z banner_done
    PRINT_CHAR
    PUSH 1
    ADD
    JMP print_banner
banner_done:
    POP
    POP
    PUSH 10
    PRINT_CHAR

; Query the framebuffer and centre the brush.
    SCREEN_SIZE
    PUSH G_H
    STORE32
    PUSH G_W
    STORE32

    PUSH G_W
    LOAD32
    PUSH 2
    DIV
    PUSH G_X
    STORE32

    PUSH G_H
    LOAD32
    PUSH 2
    DIV
    PUSH G_Y
    STORE32

    PUSH 0
    PUSH G_COLOR
    STORE32
    PUSH 0
    PUSH G_PAINT
    STORE32
    PUSH 0
    PUSH G_LAST
    STORE32

    PUSH INDEX_BG
    PUSH PALETTE_COUNT
    MUL
    PUSH palette
    ADD
    LOAD64
    SET_COLOR
    CLEAR

; ---------------------------------------------------------------- main loop

main_loop:
    ; The host steps the VM as fast as it can; cap updates to ~30 Hz.
    NOW_MS
    PUSH G_LAST
    LOAD32
    SUB
    PUSH FRAME_MS
    CMP_LT
    JMP_Z frame_ready
    JMP main_loop

frame_ready:
    NOW_MS
    PUSH G_LAST
    STORE32

    QUIT_REQUESTED
    JMP_Z no_quit
    JMP done
no_quit:

    ; Consume at most one key-down per frame.
    READ_KEY
    DUP
    PUSH 0
    CMP_EQ
    JMP_Z key_present
    JMP key_done
key_present:
    DUP
    PUSH KEY_UP
    CMP_EQ
    JMP_Z not_up
    PUSH G_Y
    LOAD32
    PUSH 1
    SUB
    DUP
    PUSH 0
    CMP_LT
    JMP_Z up_ok
    POP
    PUSH 0
up_ok:
    PUSH G_Y
    STORE32
    JMP key_done
not_up:
    DUP
    PUSH KEY_DOWN
    CMP_EQ
    JMP_Z not_down
    PUSH G_Y
    LOAD32
    PUSH 1
    ADD
    DUP
    PUSH G_H
    LOAD32
    CMP_GE
    JMP_Z down_ok
    POP
    PUSH G_H
    LOAD32
    PUSH 1
    SUB
down_ok:
    PUSH G_Y
    STORE32
    JMP key_done
not_down:
    DUP
    PUSH KEY_LEFT
    CMP_EQ
    JMP_Z not_left
    PUSH G_X
    LOAD32
    PUSH 1
    SUB
    DUP
    PUSH 0
    CMP_LT
    JMP_Z left_ok
    POP
    PUSH 0
left_ok:
    PUSH G_X
    STORE32
    JMP key_done
not_left:
    DUP
    PUSH KEY_RIGHT
    CMP_EQ
    JMP_Z not_right
    PUSH G_X
    LOAD32
    PUSH 1
    ADD
    DUP
    PUSH G_W
    LOAD32
    CMP_GE
    JMP_Z right_ok
    POP
    PUSH G_W
    LOAD32
    PUSH 1
    SUB
right_ok:
    PUSH G_X
    STORE32
    JMP key_done
not_right:
    DUP
    PUSH KEY_SPACE
    CMP_EQ
    JMP_Z not_space
    PUSH G_PAINT
    LOAD32
    LNOT
    PUSH G_PAINT
    STORE32
    JMP key_done
not_space:
    DUP
    PUSH KEY_ENTER
    CMP_EQ
    JMP_Z not_enter
    PUSH G_COLOR
    LOAD32
    PUSH 1
    ADD
    PUSH DRAW_COLORS
    MOD
    PUSH G_COLOR
    STORE32
    JMP key_done
not_enter:
    DUP
    PUSH KEY_C
    CMP_EQ
    JMP_Z not_clear
    PUSH INDEX_BG
    PUSH PALETTE_COUNT
    MUL
    PUSH palette
    ADD
    LOAD64
    SET_COLOR
    CLEAR
    JMP key_done
not_clear:
    DUP
    PUSH KEY_ESC
    CMP_EQ
    JMP_Z key_done
    POP
    JMP done
key_done:
    POP

    ; Brush up shows a white cursor; brush down leaves the palette colour.
    PUSH G_COLOR
    LOAD32
    PUSH PALETTE_COUNT
    MUL
    PUSH palette
    ADD
    LOAD64
    SET_COLOR

    ; Palette swatch: Enter is visible here even while the brush is up.
    PLOT_XY 1, 0

    PUSH G_PAINT
    LOAD32
    JMP_Z brush_up
    PUSH G_X
    LOAD32
    PUSH G_Y
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    PLOT
    JMP blink
brush_up:
    PUSH INDEX_WHITE
    PUSH PALETTE_COUNT
    MUL
    PUSH palette
    ADD
    LOAD64
    SET_COLOR
    PUSH G_X
    LOAD32
    PUSH G_Y
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    PLOT

; A timer-driven status pixel: green every other 250 ms, background otherwise.
blink:
    NOW_MS
    PUSH BLINK_MS
    DIV
    PUSH 1
    AND
    JMP_Z blink_off
    PUSH INDEX_GREEN
    PUSH PALETTE_COUNT
    MUL
    PUSH palette
    ADD
    LOAD64
    SET_COLOR
    PLOT_XY 0, 0
    DRAW
    JMP main_loop
blink_off:
    PUSH INDEX_BG
    PUSH PALETTE_COUNT
    MUL
    PUSH palette
    ADD
    LOAD64
    SET_COLOR
    PLOT_XY 0, 0
    DRAW
    JMP main_loop

done:
    HALT
