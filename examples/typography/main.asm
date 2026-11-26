; SPDX-License-Identifier: MIT
; Startup and main loop for the typography demo.
; Included by ../typography.asm.

; ------------------------------------------------------------------ startup

start:
    PUSH intro
    DUP
    STRLENZ
    PRINT_STR

    SCREEN_SIZE
    PUSH G_H
    STORE32
    PUSH G_W
    STORE32

    PUSH G_W
    LOAD32
    PUSH TEXT_X*2
    SUB
    PUSH PIXEL_W
    DIV
    PUSH G_COLS
    STORE32

    PUSH G_H
    LOAD32
    PUSH TEXT_MARGIN
    SUB
    PUSH PIXEL_H
    DIV
    PUSH G_ROWS
    STORE32

    PUSH TEXT_CAP
    ALLOC_ZERO
    PUSH G_TEXT
    STORE32

    PUSH 0
    PUSH G_LEN
    STORE32
    PUSH 0
    PUSH G_CARET
    STORE32
    PUSH 0
    PUSH G_LAST
    STORE32
    PUSH 0
    PUSH G_FRAMES
    STORE32

    PUSH 0
    CALL draw_frame
    POP
    POP
    PUSH 1
    PUSH G_DIRTY
    STORE32

; ------------------------------------------------------------------ main loop

main_loop:
    QUIT_REQUESTED
    JMP_Z no_quit_early
    JMP done
no_quit_early:
    READ_KEY
    DUP
    JMP_Z no_key_early
    PUSH 1
    PUSH G_DIRTY
    STORE32
    JMP key_present
no_key_early:
    POP
    PUSH G_DIRTY
    LOAD32
    JMP_Z idle_wait
    JMP frame_ready
idle_wait:
    NOW_MS
    PUSH G_LAST
    LOAD32
    SUB
    PUSH FRAME_MS
    CMP_GE
    JMP_Z idle_wait_more
    JMP frame_ready
idle_wait_more:
    PUSH FRAME_MS
    NOW_MS
    PUSH G_LAST
    LOAD32
    SUB
    SUB
    PUSH G_SLEEP
    STORE32
    ; A positive sleep means there is time to wait; only draw early when the
    ; deadline has already passed. Cap long waits so keys stay responsive.
    PUSH G_SLEEP
    LOAD32
    PUSH 0
    CMP_GT
    JMP_Z frame_ready
    PUSH G_SLEEP
    LOAD32
    PUSH 50
    CMP_LE
    JMP_Z sleep_cap
    JMP sleep_do
sleep_cap:
    PUSH 50
    PUSH G_SLEEP
    STORE32
sleep_do:
    PUSH G_SLEEP
    LOAD32
    DEO 0x41
    JMP main_loop

frame_ready:
    NOW_MS
    PUSH G_LAST
    STORE32

    QUIT_REQUESTED
    JMP_Z no_quit
    JMP done
no_quit:
    READ_KEY
    DUP
    JMP_Z key_done
    PUSH 1
    PUSH G_DIRTY
    STORE32
    JMP key_present
key_present:
    DUP
    PUSH KEY_ESC
    CMP_EQ
    JMP_Z not_esc
    POP
    JMP done
not_esc:
    DUP
    PUSH KEY_UP
    CMP_EQ
    JMP_Z not_up
    PUSH G_CARET
    LOAD32
    PUSH G_COLS
    LOAD32
    SUB
    DUP
    PUSH 0
    CMP_LT
    JMP_Z up_ok
    POP
    PUSH 0
up_ok:
    PUSH G_CARET
    STORE32
    JMP key_done
not_up:
    DUP
    PUSH KEY_DOWN
    CMP_EQ
    JMP_Z not_down
    PUSH G_CARET
    LOAD32
    PUSH G_COLS
    LOAD32
    ADD
    DUP
    PUSH G_LEN
    LOAD32
    CMP_GT
    JMP_Z down_ok
    POP
    PUSH G_LEN
    LOAD32
down_ok:
    PUSH G_CARET
    STORE32
    JMP key_done
not_down:
    DUP
    PUSH KEY_LEFT
    CMP_EQ
    JMP_Z not_left
    PUSH G_CARET
    LOAD32
    DEC
    DUP
    PUSH 0
    CMP_LT
    JMP_Z left_ok
    POP
    PUSH 0
left_ok:
    PUSH G_CARET
    STORE32
    JMP key_done
not_left:
    DUP
    PUSH KEY_RIGHT
    CMP_EQ
    JMP_Z not_right
    PUSH G_CARET
    LOAD32
    INC
    DUP
    PUSH G_LEN
    LOAD32
    CMP_GT
    JMP_Z right_ok
    POP
    PUSH G_LEN
    LOAD32
right_ok:
    PUSH G_CARET
    STORE32
    JMP key_done
not_right:
    DUP
    PUSH KEY_ENTER
    CMP_EQ
    JMP_Z not_enter
    POP
    PUSH NEWLINE
    CALL insert_byte
    POP
    POP
    JMP key_done_popless
not_enter:
    DUP
    PUSH KEY_BACKSPACE
    CMP_EQ
    JMP_Z not_backspace
    POP
    PUSH G_CARET
    LOAD32
    DEC
    DUP
    PUSH 0
    CMP_GE
    JMP_Z bs_bad
    CALL delete_at
    POP
    PUSH G_CARET
    STORE32
    JMP key_done_popless
bs_bad:
    POP
    JMP key_done_popless
not_backspace:
    DUP
    PUSH KEY_DELETE
    CMP_EQ
    JMP_Z not_delete
    POP
    PUSH G_CARET
    LOAD32
    CALL delete_at
    POP
    POP
    JMP key_done_popless
not_delete:
    DUP
    PUSH KEY_SPACE
    CMP_EQ
    JMP_Z not_space
    POP
    PUSH FONT_FIRST
    CALL insert_byte
    POP
    POP
    JMP key_done_popless
not_space:
    DUP
    PUSH FONT_FIRST
    CMP_GE
    JMP_Z key_drop
    DUP
    PUSH FONT_LAST
    CMP_LE
    JMP_Z key_drop
    CALL insert_byte
    POP
    POP
    JMP key_done_popless
key_done:
    POP
key_done_popless:
    JMP after_key
key_drop:
    POP
    JMP after_key
after_key:

    PUSH G_DIRTY
    LOAD32
    JMP_Z skip_draw
    PUSH 0
    CALL draw_frame
    POP
    POP
    PUSH 0
    PUSH G_DIRTY
    STORE32
skip_draw:

    PUSH G_FRAMES
    LOAD32
    INC
    PUSH G_FRAMES
    STORE32
    PUSH MAX_FRAMES
    JMP_Z main_loop
    PUSH G_FRAMES
    LOAD32
    PUSH MAX_FRAMES
    CMP_GE
    JMP_Z done
    JMP main_loop

done:
    PUSH done_text
    DUP
    STRLENZ
    PRINT_STR
    HALT
