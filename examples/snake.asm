; =============================================================================
; Snake — the 0xvm flagship app for the SDL3 host (0xvm-sdl).
;
; Arrows steer, escape quits. The snake wraps at the edges, eats the red food,
; and grows. Body cells live in an arena-backed vector, food positions come
; from the deterministic RNG, and the score is an explicit-length string that
; is printed on the shared Console device. Screen, Controller, and timer ports
; are optional: the core 0xvm host does not provide them.
;
; Run with: ./build/0xvm-sdl -f examples/snake.asm [-W 320] [-H 240]
; =============================================================================

INCLUDE "all.asm"

; ------------------------------------------------------------------ constants

EQU CELL        8
EQU SPRITE      6
EQU FRAME_MS    80          ; ~12.5 updates/second
EQU MAX_MOVES   300         ; bounded run for unattended smoke tests
EQU MAX_LEN     96
EQU MAX_SCORE   20
EQU ARENA_CAP   1024

EQU KEY_UP      1
EQU KEY_DOWN    2
EQU KEY_LEFT    3
EQU KEY_RIGHT   4
EQU KEY_ESC     7

EQU C_BG        0x101010
EQU C_BODY      0x00C020
EQU C_HEAD      0xE0FFE0
EQU C_FOOD      0xFF5040

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
EQU G_STATE     64          ; 0 = running, 1 = quit requested
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

; ------------------------------------------------------------------ static data

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
    PUSH 1
    ADD
    LOCAL_SET 2
    LOCAL_GET 0
    PUSH 0xFFFF
    AND
    PUSH CELL
    MUL
    PUSH 1
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
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    PLOT
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

draw_frame:
    PUSH C_BG
    SET_COLOR
    CLEAR
    PUSH G_FX
    LOAD32
    PUSH G_FY
    LOAD32
    SWAP
    PUSH 16
    SHL
    SWAP
    OR
    PUSH C_FOOD
    CALL draw_cell
    POP
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
    PUSH C_BODY
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
    PUSH C_HEAD
    CALL draw_cell
    POP
    POP
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
    PUSH CELL
    DIV
    PUSH G_GW
    STORE32
    PUSH G_PH
    LOAD32
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
    PUSH 1
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
    PUSH over_text
    DUP
    STRLENZ
    PRINT_STR
    HALT

complete:
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
