; SPDX-License-Identifier: MIT
; =============================================================================
; Snake — the 0xvm flagship app for the SDL3 and browser hosts.
;
; Arrows steer, escape quits. The snake wraps at the edges, eats the dithered
; accent food, and grows. Body cells live in an arena-backed vector, food
; positions come from the deterministic RNG, and the score is an explicit-length
; string printed on both the inverted header strip and the Console device.
; Drawing follows the house style: Deep Ink palette, 8px grid, inverted header,
; and hairline frame. Screen, Controller, and timer ports are optional: the core
; 0xvm host does not provide them.
;
; Run with: ./build/0xvm-sdl -f examples/snake.asm [-W 320] [-H 240]
;       or: make wasm && serve build/ and open 0xvm-wasm.html
; =============================================================================

INCLUDE "all.asm"

; ------------------------------------------------------------------ constants

; House grid: 8x8 cells with a 1px inset for crisp, evenly spaced sprites.
EQU CELL        8
EQU TILE        8
EQU INSET       1
EQU SPRITE      CELL-2*INSET
EQU GRID_X      TILE        ; board fills the hairline box horizontally
EQU GRID_Y      TILE*2      ; board starts at the box top
EQU GRID_MARGIN TILE*3      ; box top + bottom border
EQU FRAME_MS    80          ; ~12.5 updates/second
EQU MAX_MOVES   0           ; 0 = unlimited; smoke builds override this
EQU MAX_SCORE   20          ; win after 20 apples
EQU MAX_LEN     MAX_SCORE+1 ; score plus the original head
EQU ARENA_CAP   1024

EQU KEY_UP      1
EQU KEY_DOWN    2
EQU KEY_LEFT    3
EQU KEY_RIGHT   4
EQU KEY_ESC     7

; Deep Ink house palette: canvas, ink, structure, accent.
EQU C_CANVAS    0x060907
EQU C_INK       0xF2F6F0
EQU C_STRUCT    0x2A352C
EQU C_ACCENT    0xB9E5AD

; Shared bitmap font geometry (same values as examples/typography).
EQU SCALE       1
EQU GLYPH_W     5
EQU CELL_W      8
EQU CELL_H      8
EQU PIXEL_W     CELL_W*SCALE
EQU PIXEL_H     CELL_H*SCALE
EQU FONT_FIRST  32
EQU FONT_LAST   126
EQU NEWLINE     10

; Zero-page globals (absolute byte addresses).
EQU G_PW        0           ; framebuffer width in pixels
EQU G_PH        4           ; framebuffer height in pixels
EQU G_GW        8           ; grid width in cells
EQU G_GH        12          ; grid height in cells
EQU G_HX        16          ; head x
EQU G_HY        20          ; head y
EQU G_DX        24          ; current direction x
EQU G_DY        28          ; current direction y
EQU G_NDX       32          ; queued direction x
EQU G_NDY       36          ; queued direction y
EQU G_FX        40          ; food x
EQU G_FY        44          ; food y
EQU G_SCORE     48
EQU G_LEN       52
EQU G_LAST      56          ; timestamp of the last frame
EQU G_MOVES     60
EQU G_STATE     64          ; 0=running 1=quit 2=game over 3=complete
EQU G_SEED      68
EQU G_NX        72          ; candidate head x
EQU G_NY        76          ; candidate head y
EQU G_ATE       80
EQU G_LIMIT     84          ; number of body cells to test for collision
EQU G_I         88          ; loop index
EQU G_HIT       92          ; collision result
EQU ARENA       96          ; 24-byte arena header
EQU VEC         128         ; 32-byte vector header
EQU SCORE_BUF   160         ; 8-byte explicit-length string

; Names expected by the shared typography drawing modules.
EQU G_W         G_PW
EQU G_H         G_PH
EQU G_COLS      G_GW
; caret_xy is unused here; park its scratch cells past the score buffer.
EQU G_TEXT      168
EQU G_CX        172
EQU G_CY        176

; ------------------------------------------------------------------ static data

header_title:
    STR "0XVM // SNAKE"
    DB 0

status_over:
    STR "GAME OVER"
    DB 0

status_win:
    STR "RUN COMPLETE"
    DB 0

intro:
    STR "SNAKE | arrows move | esc quits | score on console"
    DB 0

over_text:
    STR "GAME OVER"
    DB 0

complete_text:
    STR "RUN COMPLETE"
    DB 0

bye_text:
    STR "BYE"
    DB 0

; Fixed-width score strings avoid guest-side numeric formatting.
score_texts:
    STR "SCORE 00"
    STR "SCORE 01"
    STR "SCORE 02"
    STR "SCORE 03"
    STR "SCORE 04"
    STR "SCORE 05"
    STR "SCORE 06"
    STR "SCORE 07"
    STR "SCORE 08"
    STR "SCORE 09"
    STR "SCORE 10"
    STR "SCORE 11"
    STR "SCORE 12"
    STR "SCORE 13"
    STR "SCORE 14"
    STR "SCORE 15"
    STR "SCORE 16"
    STR "SCORE 17"
    STR "SCORE 18"
    STR "SCORE 19"
    STR "SCORE 20"

    JMP start

; Shared house-font drawing routines; the data above and the game code below
; are emitted after this jump.
INCLUDE "typography/font.asm"
INCLUDE "typography/macros.asm"
INCLUDE "typography/glyphs.asm"
INCLUDE "typography/lines.asm"
INCLUDE "typography/layout.asm"

; ------------------------------------------------------------------ input
;
; Consume at most one queued key-down. Reverse direction is ignored. Escape
; sets G_STATE and falls through to the normal frame path.

queue_input:
    READ_KEY
    DUP
    JMP_Z qi_none
    DUP
    PUSH KEY_ESC
    CMP_EQ
    JMP_Z qi_not_esc
    POP
    PUSH 1
    PUSH G_STATE
    STORE32
    JMP qi_end
qi_not_esc:
    DUP
    PUSH KEY_UP
    CMP_EQ
    JMP_Z qi_not_down
    PUSH G_DY
    LOAD32
    PUSH 1
    CMP_EQ
    JMP_Z qi_accept_up
    JMP qi_drop
qi_accept_up:
    PUSH 0
    PUSH G_NDX
    STORE32
    PUSH -1
    PUSH G_NDY
    STORE32
    JMP qi_drop
qi_not_down:
    DUP
    PUSH KEY_DOWN
    CMP_EQ
    JMP_Z qi_not_left
    PUSH G_DY
    LOAD32
    PUSH -1
    CMP_EQ
    JMP_Z qi_accept_down
    JMP qi_drop
qi_accept_down:
    PUSH 0
    PUSH G_NDX
    STORE32
    PUSH 1
    PUSH G_NDY
    STORE32
    JMP qi_drop
qi_not_left:
    DUP
    PUSH KEY_LEFT
    CMP_EQ
    JMP_Z qi_not_right
    PUSH G_DX
    LOAD32
    PUSH 1
    CMP_EQ
    JMP_Z qi_accept_left
    JMP qi_drop
qi_accept_left:
    PUSH -1
    PUSH G_NDX
    STORE32
    PUSH 0
    PUSH G_NDY
    STORE32
    JMP qi_drop
qi_not_right:
    DUP
    PUSH KEY_RIGHT
    CMP_EQ
    JMP_Z qi_drop
    PUSH G_DX
    LOAD32
    PUSH -1
    CMP_EQ
    JMP_Z qi_accept_right
    JMP qi_drop
qi_accept_right:
    PUSH 1
    PUSH G_NDX
    STORE32
    PUSH 0
    PUSH G_NDY
    STORE32
    JMP qi_drop
qi_drop:
    POP
    JMP qi_end
qi_none:
    POP
qi_end:
    PUSH 0
    RET

; ------------------------------------------------------------------ collision
;
; [..., x, y, limit] -> [..., bool]
; True when (x, y) matches any of the first `limit` body cells.

hits_body:
    ENTER 5                     ; 0=x, 1=y, 2=limit, 3=i, 4=packed target
    PUSH 7
    PICK
    LOCAL_SET 0
    PUSH 6
    PICK
    LOCAL_SET 1
    PUSH 5
    PICK
    LOCAL_SET 2
    LOCAL_GET 0
    LOCAL_GET 1
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    LOCAL_SET 4
    PUSH 0
    LOCAL_SET 3
hb_loop:
    LOCAL_GET 3
    LOCAL_GET 2
    CMP_LT
    JMP_Z hb_no
    PUSH VEC
    LOCAL_GET 3
    VEC_GET64
    LOCAL_GET 4
    CMP_EQ
    JMP_Z hb_next
    PUSH 1
    RET
hb_next:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP hb_loop
hb_no:
    PUSH 0
    RET

; ------------------------------------------------------------------ food
;
; Pick a random grid cell that is not occupied by the body.

place_food:
    ENTER 4                     ; 0=index, 1=x, 2=y, 3=occupied
pf_loop:
    PUSH G_SEED
    RNG_NEXT
    PUSH 0x7FFFFFFFFFFFFFFF
    AND
    PUSH G_GW
    LOAD32
    PUSH G_GH
    LOAD32
    MUL
    MOD
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH G_GW
    LOAD32
    MOD
    LOCAL_SET 1
    LOCAL_GET 0
    PUSH G_GW
    LOAD32
    DIV
    LOCAL_SET 2
    LOCAL_GET 1
    PUSH G_FX
    STORE32
    LOCAL_GET 2
    PUSH G_FY
    STORE32
    LOCAL_GET 1
    LOCAL_GET 2
    PUSH G_LEN
    LOAD32
    CALL hits_body
    LOCAL_SET 3
    POP
    POP
    POP
    LOCAL_GET 3
    JMP_Z pf_ok
    JMP pf_loop
pf_ok:
    PUSH 0
    RET

; ------------------------------------------------------------------ drawing
;
; Solid fill for the inverted 8px header strip.
; [..., x, y, w, h, colour] -> [..., 0]

fill_rect:
    ENTER 8                     ; 0=x 1=y 2=w 3=h 4=colour 5=row
    PUSH 12
    PICK
    LOCAL_SET 0
    PUSH 11
    PICK
    LOCAL_SET 1
    PUSH 10
    PICK
    LOCAL_SET 2
    PUSH 9
    PICK
    LOCAL_SET 3
    PUSH 8
    PICK
    LOCAL_SET 4
    PUSH 0
    LOCAL_SET 5
fr_loop:
    LOCAL_GET 5
    LOCAL_GET 3
    CMP_GE
    JMP_Z fr_body
    JMP fr_done
fr_body:
    LOCAL_GET 0
    LOCAL_GET 1
    LOCAL_GET 5
    ADD
    LOCAL_GET 2
    LOCAL_GET 4
    CALL hline
    POP
    POP
    POP
    POP
    POP
    LOCAL_GET 5
    INC
    LOCAL_SET 5
    JMP fr_loop
fr_done:
    PUSH 0
    RET

; [..., packed x/y, colour] -> [..., 0]
; Plot a filled SPRITE x SPRITE square with a one-pixel inset.
draw_cell:
    ENTER 7                     ; 0=packed, 1=colour, 2=px, 3=py, 4=row, 5=col
    PUSH 8
    PICK
    LOCAL_SET 0
    PUSH 7
    PICK
    LOCAL_SET 1
    LOCAL_GET 0
    PUSH 16
    SHR
    PUSH CELL
    MUL
    PUSH GRID_X
    ADD
    PUSH INSET
    ADD
    LOCAL_SET 2
    LOCAL_GET 0
    PUSH 0xFFFF
    AND
    PUSH CELL
    MUL
    PUSH GRID_Y
    ADD
    PUSH INSET
    ADD
    LOCAL_SET 3
    LOCAL_GET 1
    SET_COLOR
    PUSH 0
    LOCAL_SET 4
dc_row:
    LOCAL_GET 4
    PUSH SPRITE
    CMP_LT
    JMP_Z dc_done
    PUSH 0
    LOCAL_SET 5
dc_col:
    LOCAL_GET 5
    PUSH SPRITE
    CMP_LT
    JMP_Z dc_next_row
    LOCAL_GET 2
    LOCAL_GET 5
    ADD
    LOCAL_GET 3
    LOCAL_GET 4
    ADD
    PLOT_STACK
    LOCAL_GET 5
    INC
    LOCAL_SET 5
    JMP dc_col
dc_next_row:
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP dc_row
dc_done:
    PUSH 0
    RET

; [..., packed x/y] -> [..., 0]
; The target is a dithered accent square so it reads apart from the solid head.
draw_food:
    ENTER 6                     ; 0=packed 1=px 2=py 3=row 4=col 5=parity
    PUSH 6
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH 16
    SHR
    PUSH CELL
    MUL
    PUSH GRID_X
    ADD
    PUSH INSET
    ADD
    LOCAL_SET 1
    LOCAL_GET 0
    PUSH 0xFFFF
    AND
    PUSH CELL
    MUL
    PUSH GRID_Y
    ADD
    PUSH INSET
    ADD
    LOCAL_SET 2
    PUSH 0
    LOCAL_SET 3
dfd_row:
    LOCAL_GET 3
    PUSH SPRITE
    CMP_LT
    JMP_Z dfd_done
    PUSH 0
    LOCAL_SET 4
dfd_col:
    LOCAL_GET 4
    PUSH SPRITE
    CMP_LT
    JMP_Z dfd_next_row
    LOCAL_GET 3
    LOCAL_GET 4
    ADD
    PUSH 1
    AND
    LOCAL_SET 5
    LOCAL_GET 5
    JMP_Z dfd_canvas
    PUSH C_ACCENT
    SET_COLOR
    JMP dfd_plot
dfd_canvas:
    PUSH C_CANVAS
    SET_COLOR
dfd_plot:
    LOCAL_GET 1
    LOCAL_GET 4
    ADD
    LOCAL_GET 2
    LOCAL_GET 3
    ADD
    PLOT_STACK
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP dfd_col
dfd_next_row:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP dfd_row
dfd_done:
    PUSH 0
    RET

; [...] -> [..., 0]
; Bottom status row; controls live in the intro only. The accent is reserved
; for the terminal modes, so the playing field stays free of text.
draw_status_line:
    PUSH G_STATE
    LOAD32
    PUSH 2
    CMP_LT
    JMP_Z dsl_visible
    PUSH 0
    RET
dsl_visible:
    PUSH G_STATE
    LOAD32
    PUSH 2
    CMP_EQ
    JMP_Z dsl_win
    PUSH status_over
    JMP dsl_have_text
dsl_win:
    PUSH status_win
dsl_have_text:
    DUP
    STRLENZ
    PUSH GRID_X
    PUSH G_H
    LOAD32
    PUSH TILE*2
    SUB
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_ACCENT
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    PUSH 0
    RET

draw_frame:
    PUSH C_CANVAS
    SET_COLOR
    CLEAR

    ; Inverted header strip: title left, live score right.
    PUSH 0
    PUSH 0
    PUSH G_W
    LOAD32
    PUSH TILE
    PUSH C_INK
    CALL fill_rect
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH header_title
    DUP
    STRLENZ
    PUSH TILE
    PUSH 0
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_CANVAS
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH G_W
    LOAD32
    PUSH 8                      ; "SCORE 00" is eight cells wide
    PUSH PIXEL_W
    MUL
    SUB
    PUSH TILE
    SUB
    PUSH SCORE_BUF
    PUSH 8
    ROT
    PUSH 0
    PUSH 8
    PUSH 1
    PUSH C_CANVAS
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    ; Hairline content box.
    PUSH TILE
    PUSH TILE*2
    PUSH G_W
    LOAD32
    PUSH TILE*2
    SUB
    PUSH C_STRUCT
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH TILE
    PUSH G_H
    LOAD32
    PUSH TILE
    SUB
    PUSH G_W
    LOAD32
    PUSH TILE*2
    SUB
    PUSH C_STRUCT
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH TILE
    PUSH TILE*2+1
    PUSH G_H
    LOAD32
    PUSH TILE*3+1
    SUB
    PUSH C_STRUCT
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_W
    LOAD32
    PUSH TILE+1
    SUB
    PUSH TILE*2+1
    PUSH G_H
    LOAD32
    PUSH TILE*3+1
    SUB
    PUSH C_STRUCT
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_FX
    LOAD32
    PUSH G_FY
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    CALL draw_food
    POP
    POP

    PUSH G_LEN
    LOAD32
    DEC
    PUSH G_I
    STORE32
df_body:
    PUSH G_I
    LOAD32
    PUSH 1
    CMP_GE
    JMP_Z df_body_done
    PUSH VEC
    PUSH G_I
    LOAD32
    VEC_GET64
    PUSH C_INK
    CALL draw_cell
    POP
    POP
    POP
    PUSH G_I
    LOAD32
    DEC
    PUSH G_I
    STORE32
    JMP df_body
df_body_done:
    PUSH G_HX
    LOAD32
    PUSH G_HY
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    PUSH C_ACCENT
    CALL draw_cell
    POP
    POP
    POP

    CALL draw_status_line
    POP

    DRAW
    PUSH 0
    RET

; ------------------------------------------------------------------ startup

start:
    PUSH intro
    DUP
    STRLENZ
    PRINT_STR

    SCREEN_SIZE
    PUSH G_PH
    STORE32
    PUSH G_PW
    STORE32
    PUSH G_PW
    LOAD32
    PUSH GRID_X*2
    SUB
    PUSH CELL
    DIV
    PUSH G_GW
    STORE32
    PUSH G_PH
    LOAD32
    PUSH GRID_MARGIN
    SUB
    PUSH CELL
    DIV
    PUSH G_GH
    STORE32

    PUSH 0x12345678
    PUSH G_SEED
    STORE32
    PUSH ARENA
    PUSH ARENA_CAP
    ARENA_INIT
    POP
    PUSH VEC
    PUSH ARENA
    VEC_INIT
    POP

    PUSH G_GW

    LOAD32
    PUSH 2
    DIV
    PUSH G_HX
    STORE32
    PUSH G_GH
    LOAD32
    PUSH 2
    DIV
    PUSH G_HY
    STORE32
    PUSH 1
    PUSH G_DX
    STORE32
    PUSH 0
    PUSH G_DY
    STORE32
    PUSH 1
    PUSH G_NDX
    STORE32
    PUSH 0
    PUSH G_NDY
    STORE32
    PUSH 0
    PUSH G_SCORE
    STORE32
    PUSH 0
    PUSH G_LEN
    STORE32
    PUSH 0
    PUSH G_LAST
    STORE32
    PUSH 0
    PUSH G_MOVES
    STORE32
    PUSH 0
    PUSH G_STATE
    STORE32

    CALL place_food
    POP
    PUSH VEC
    PUSH G_HX
    LOAD32
    PUSH G_HY
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    VEC_PUSH64
    POP
    PUSH 1
    PUSH G_LEN
    STORE32

    PUSH SCORE_BUF
    PUSH score_texts
    PUSH 8
    STRCPY
    PUSH SCORE_BUF
    PUSH 8
    PRINT_STR

    CALL draw_frame
    POP

; ------------------------------------------------------------------ main loop

main_loop:
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
    CALL queue_input
    POP
    PUSH G_STATE
    LOAD32
    JMP_Z still_playing
    JMP done

still_playing:
    PUSH G_NDX
    LOAD32
    PUSH G_DX
    STORE32
    PUSH G_NDY
    LOAD32
    PUSH G_DY
    STORE32

    PUSH G_HX

    LOAD32
    PUSH G_DX
    LOAD32
    ADD
    PUSH G_GW
    LOAD32
    ADD
    PUSH G_GW
    LOAD32
    MOD
    PUSH G_NX
    STORE32
    PUSH G_HY
    LOAD32
    PUSH G_DY
    LOAD32
    ADD
    PUSH G_GH
    LOAD32
    ADD
    PUSH G_GH
    LOAD32
    MOD
    PUSH G_NY
    STORE32

    PUSH 0
    PUSH G_ATE
    STORE32
    PUSH G_NX
    LOAD32
    PUSH G_FX
    LOAD32
    CMP_EQ
    JMP_Z not_eating
    PUSH G_NY
    LOAD32
    PUSH G_FY
    LOAD32
    CMP_EQ
    JMP_Z not_eating
    PUSH 1
    PUSH G_ATE
    STORE32
not_eating:
    PUSH G_LEN
    LOAD32
    PUSH G_ATE
    LOAD32
    SUB
    PUSH G_LIMIT
    STORE32

    PUSH G_NX

    LOAD32
    PUSH G_NY
    LOAD32
    PUSH G_LIMIT
    LOAD32
    CALL hits_body
    PUSH G_HIT
    STORE32
    POP
    POP
    POP
    PUSH G_HIT
    LOAD32
    JMP_Z no_hit
    JMP game_over

no_hit:
    PUSH G_LEN
    LOAD32
    PUSH MAX_LEN
    CMP_GE
    JMP_Z can_move
    JMP complete
can_move:
    PUSH MAX_MOVES
    JMP_Z keep_going
    PUSH G_MOVES
    LOAD32
    INC
    PUSH G_MOVES
    STORE32
    PUSH G_MOVES
    LOAD32
    PUSH MAX_MOVES
    CMP_GE
    JMP_Z keep_going
    JMP complete

keep_going:
    PUSH VEC
    PUSH 0
    VEC_PUSH64
    POP
    PUSH G_LEN
    LOAD32
    DEC
    PUSH G_I
    STORE32
shift_loop:
    PUSH G_I
    LOAD32
    PUSH 0
    CMP_GE
    JMP_Z shift_done
    PUSH VEC
    PUSH G_I
    LOAD32
    VEC_GET64
    PUSH VEC
    PUSH G_I
    LOAD32
    INC
    ROT
    VEC_SET64
    POP
    PUSH G_I
    LOAD32
    DEC
    PUSH G_I
    STORE32
    JMP shift_loop
shift_done:
    PUSH VEC
    PUSH 0
    PUSH G_NX
    LOAD32
    PUSH G_NY
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    VEC_SET64
    POP
    PUSH G_NX
    LOAD32
    PUSH G_HX
    STORE32
    PUSH G_NY
    LOAD32
    PUSH G_HY
    STORE32

    PUSH G_ATE

    LOAD32
    JMP_Z not_ate_after
    PUSH G_LEN
    LOAD32
    INC
    PUSH G_LEN
    STORE32
    PUSH G_SCORE
    LOAD32
    INC
    PUSH G_SCORE
    STORE32
    PUSH G_SCORE
    LOAD32
    PUSH MAX_SCORE
    CMP_LE
    JMP_Z score_capped
    PUSH SCORE_BUF
    PUSH score_texts
    PUSH G_SCORE
    LOAD32
    PUSH 8
    MUL
    ADD
    PUSH 8
    STRCPY
    PUSH SCORE_BUF
    PUSH 8
    PRINT_STR
score_capped:
    CALL place_food
    POP
    JMP render
not_ate_after:
    PUSH G_LEN
    LOAD32
    PUSH VEC
    PUSH 16
    ADD
    STORE32
render:
    CALL draw_frame
    POP
    JMP main_loop

game_over:
    PUSH 2
    PUSH G_STATE
    STORE32
    CALL draw_frame
    POP
    PUSH over_text
    DUP
    STRLENZ
    PRINT_STR
    HALT

complete:
    PUSH 3
    PUSH G_STATE
    STORE32
    CALL draw_frame
    POP
    PUSH complete_text
    DUP
    STRLENZ
    PRINT_STR
    HALT

done:
    PUSH bye_text
    DUP
    STRLENZ
    PRINT_STR
    HALT
