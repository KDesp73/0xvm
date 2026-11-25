; =============================================================================
; Type — a graphical typography demo for the SDL3 and browser hosts.
;
; This is deliberately an example, not a stdlib module: the bitmap font and
; layout routines below are self-contained and can be copied into an app that
; wants them. Arrows move the caret, printable keys (including space) insert at
; the caret, enter inserts a newline, and escape (or window close) exits.
;
; Run with: ./build/0xvm-sdl -f examples/typography.asm [-W 320] [-H 240]
;       or: make wasm && serve build/ and open 0xvm-wasm.html?rom=typography.rom
; =============================================================================

INCLUDE "all.asm"

; ------------------------------------------------------------------ constants

EQU SCALE        2
EQU GLYPH_W      5
EQU CELL_W       GLYPH_W+1
EQU CELL_H       8
EQU PIXEL_W      CELL_W*SCALE
EQU PIXEL_H      CELL_H*SCALE
EQU BORDER       2
EQU BAR_H        PIXEL_H+4
EQU FRAME_MS     66           ; ~15 updates/second
EQU BLINK_MS     250
EQU MAX_FRAMES   0            ; 0 = unlimited; smoke builds override this
EQU TEXT_CAP     1024
EQU FONT_FIRST   32
EQU FONT_LAST    126

EQU KEY_UP        1
EQU KEY_DOWN      2
EQU KEY_LEFT      3
EQU KEY_RIGHT     4
EQU KEY_SPACE     5
EQU KEY_ENTER     6
EQU KEY_ESC       7
EQU KEY_BACKSPACE 8
EQU KEY_DELETE    9
EQU NEWLINE       10

EQU C_BG         0x101018
EQU C_PANEL      0x181828
EQU C_BORDER     0x405080
EQU C_TITLE      0x00E0FF
EQU C_TEXT       0xE8E8F0
EQU C_CARET      0xFFC040
EQU C_STATUS     0x90A0C0

; Zero-page globals (absolute byte addresses).
EQU G_W          0            ; framebuffer width
EQU G_H          4            ; framebuffer height
EQU G_COLS       8            ; text columns
EQU G_ROWS       12           ; text rows
EQU G_TEXT       16           ; heap address of the byte buffer
EQU G_LEN        20           ; buffer length
EQU G_CARET      24           ; insertion index
EQU G_LAST       28           ; timestamp of the last frame
EQU G_FRAMES     32           ; frames drawn (for bounded smoke runs)
EQU G_CX         36           ; caret column
EQU G_CY         40           ; caret row
EQU G_DIRTY      44           ; redraw requested
EQU G_SLEEP      48           ; idle sleep ms

; ------------------------------------------------------------------ data

intro:
    STR "TYPE | arrows move | type inserts | enter newline | bksp/del remove | esc quits"
    DB 0

title_text:
    STR "0XVM TYPE / BITMAP TYPOGRAPHY"
    DB 0

status_text:
    STR "PRINTABLE ASCII ENTERS TEXT | SPACE IS A CHARACTER"
    DB 0

done_text:
    STR "TYPOGRAPHY OK"
    DB 0

sample_text:
    STR "TYPE IS A LITTLE GRAPHICAL\nEDITOR. MOVE THE CARET WITH\nARROWS, TYPE TO INSERT TEXT,\nAND WATCH THE BITMAP FONT.\n"
    DB 0

; 95 printable ASCII glyphs, 32..126. Each record is eight row bytes: bit 7 is
; the leftmost pixel. The table stores rows bottom-to-top, with row 7 blank, so
; draw_glyph flips the row index before reading it.
glyphs:
    DQ 0    ;  
    DQ 2305878331563712512    ; !
    DQ 1347440640    ; "
    DQ 5787398547818172416    ; #
    DQ 2373441467817926656    ; $
    DQ 1772236959835013120    ; %
    DQ 7534707172116029440    ; &
    DQ 1075863552    ; '
    DQ 1161999348559515648    ; (
    DQ 4620710878858199040    ; )
    DQ 9192401065213952    ; *
    DQ 9043449317687296    ; +
    DQ 4620798770798395392    ; ,
    DQ 1065151889408    ; -
    DQ 6944550625405304832    ; .
    DQ 36099303471054848    ; /
    DQ 8108951755519717376    ; 0
    DQ 8079493053856882688    ; 1
    DQ -558411100559216640    ; 2
    DQ 8108739994431059968    ; 3
    DQ 1157698402938523648    ; 4
    DQ 8108739963568322560    ; 5
    DQ 3479180832127070208    ; 6
    DQ 36099303471118336    ; 7
    DQ 8108881145988935680    ; 8
    DQ 6922041941048193024    ; 9
    DQ 27127152497393664    ; :
    DQ 4620798772415299584    ; ;
    DQ 1161999623437422592    ; <
    DQ 272683044438016    ; =
    DQ 4620710844498460672    ; >
    DQ 2305878262448418816    ; ?
    DQ 8117923493108543488    ; @
    DQ -8608357422428819456    ; A
    DQ -1114490341109993472    ; B
    DQ 8108872418481172480    ; C
    DQ -2265160492579233792    ; D
    DQ -540290184848017408    ; E
    DQ -9187201469399369728    ; F
    DQ 8685342207395786752    ; G
    DQ -8608480086694787072    ; H
    DQ 8079493053852708864    ; I
    DQ 6958079085462435840    ; J
    DQ -8606202138717026304    ; K
    DQ -540290665884385280    ; L
    DQ -8608480429750056960    ; M
    DQ -8608462837032384512    ; N
    DQ 8108881249068150784    ; O
    DQ -9187201469264629760    ; P
    DQ 7534707480950501376    ; Q
    DQ -8606201932961746944    ; R
    DQ -1150660425507899392    ; S
    DQ 2314885530818508800    ; T
    DQ 8108881249068156928    ; U
    DQ 2328511127338125312    ; V
    DQ 5812080760920246272    ; W
    DQ -8608542587998402560    ; X
    DQ 2314885738728556544    ; Y
    DQ -540361448832305152    ; Z
    DQ 8088535575457460224    ; [
    DQ 2269530520813568    ; backslash
    DQ 8074971793050333184    ; ]
    DQ 2286952448    ; ^
    DQ -576460752303423488    ; _
    DQ 270548992    ; `
    DQ 8685323859018121216    ; a
    DQ -1114490512238149632    ; b
    DQ 8108872418203795456    ; c
    DQ 8685342069545764864    ; d
    DQ 8106752594145181696    ; e
    DQ 4629771748832194560    ; f
    DQ 8072834859862065152    ; g
    DQ -8608480292182654976    ; h
    DQ 8079493054924333056    ; i
    DQ 6958079085998247936    ; j
    DQ -8025202740122583040    ; k
    DQ 8079493053852704768    ; l
    DQ -8608445244721070080    ; m
    DQ -8603941508853757952    ; n
    DQ 8108881248656556032    ; o
    DQ -9187078768903061504    ; p
    DQ 578845148092301312    ; q
    DQ -9187192844299763712    ; r
    DQ -1150546007855988736    ; s
    DQ 3479101359528099840    ; t
    DQ 7536924096383156224    ; u
    DQ 2328511127329177600    ; v
    DQ 5812080623472345088    ; w
    DQ -8624357756163325952    ; x
    DQ 8072834859854200832    ; y
    DQ -558411096541626368    ; z
    DQ 1161964163650555904    ; {
    DQ 2314885530818453504    ; |
    DQ 4620728471312678912    ; }
    DQ 9025856863207424    ; ~

    JMP start

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

; ------------------------------------------------------------------ glyphs

; [..., x, y, code, colour] -> [...]
; Draws one scaled glyph. Codes outside printable ASCII become '?'.
draw_glyph:
    ENTER 12                ; 0=x 1=y 2=code 3=colour 4=glyph 5=row 6=col
                            ; 7=rowbits 8=sy 9=sx 10=px 11=py
    PUSH 15
    PICK
    LOCAL_SET 0
    PUSH 14
    PICK
    LOCAL_SET 1
    PUSH 13
    PICK
    LOCAL_SET 2
    PUSH 12
    PICK
    LOCAL_SET 3

    LOCAL_GET 2
    PUSH FONT_FIRST
    CMP_LT
    JMP_Z dg_code_ok_low
    PUSH 63                 ; '?'
    LOCAL_SET 2
dg_code_ok_low:
    LOCAL_GET 2
    PUSH FONT_LAST
    CMP_GT
    JMP_Z dg_code_ok_high
    PUSH 63
    LOCAL_SET 2
dg_code_ok_high:

    LOCAL_GET 3
    SET_COLOR
    LOCAL_GET 2
    PUSH FONT_FIRST
    SUB
    PUSH CELL_H
    MUL
    PUSH glyphs
    ADD
    LOCAL_SET 4

    PUSH 0
    LOCAL_SET 5
dg_row_loop:
    LOCAL_GET 5
    PUSH CELL_H
    CMP_GE
    JMP_Z dg_row_body
    JMP dg_done
dg_row_body:
    LOCAL_GET 4
    PUSH CELL_H
    DEC
    LOCAL_GET 5
    SUB
    ADD
    LOAD8
    LOCAL_SET 7
    PUSH 0
    LOCAL_SET 6
dg_col_loop:
    LOCAL_GET 6
    PUSH GLYPH_W
    CMP_GE
    JMP_Z dg_col_body
    JMP dg_row_next
dg_col_body:
    LOCAL_GET 7
    PUSH 7
    LOCAL_GET 6
    SUB
    SHR
    PUSH 1
    AND
    JMP_Z dg_col_next
    PUSH 0
    LOCAL_SET 8
dg_sy_loop:
    LOCAL_GET 8
    PUSH SCALE
    CMP_GE
    JMP_Z dg_sy_body
    JMP dg_col_next
dg_sy_body:
    PUSH 0
    LOCAL_SET 9
dg_sx_loop:
    LOCAL_GET 9
    PUSH SCALE
    CMP_GE
    JMP_Z dg_sx_body
    JMP dg_sy_next
dg_sx_body:
    LOCAL_GET 0
    LOCAL_GET 6
    PUSH SCALE
    MUL
    ADD
    LOCAL_GET 9
    ADD
    LOCAL_SET 10
    LOCAL_GET 1
    LOCAL_GET 5
    PUSH SCALE
    MUL
    ADD
    LOCAL_GET 8
    ADD
    LOCAL_SET 11
    LOCAL_GET 10
    PUSH G_W
    LOAD32
    CMP_LT
    JMP_Z dg_pixel_skip
    LOCAL_GET 11
    PUSH G_H
    LOAD32
    CMP_LT
    JMP_Z dg_pixel_skip
    LOCAL_GET 10
    LOCAL_GET 11
    PLOT_STACK
dg_pixel_skip:
    LOCAL_GET 9
    INC
    LOCAL_SET 9
    JMP dg_sx_loop
dg_sy_next:
    LOCAL_GET 8
    INC
    LOCAL_SET 8
    JMP dg_sy_loop
dg_col_next:
    LOCAL_GET 6
    INC
    LOCAL_SET 6
    JMP dg_col_loop
dg_row_next:
    LOCAL_GET 5
    INC
    LOCAL_SET 5
    JMP dg_row_loop
dg_done:
    PUSH 0
    RET

; ------------------------------------------------------------------ lines

; [..., x, y, len, colour] -> [...]
hline:
    ENTER 6                 ; 0=x 1=y 2=len 3=colour 4=i
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
    SET_COLOR
    PUSH 0
    LOCAL_SET 4
hl_loop:
    LOCAL_GET 4
    LOCAL_GET 2
    CMP_GE
    JMP_Z hl_body
    JMP hl_done
hl_body:
    LOCAL_GET 0
    LOCAL_GET 4
    ADD
    LOCAL_GET 1
    PLOT_STACK
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP hl_loop
hl_done:
    PUSH 0
    RET

; [..., x, y, len, colour] -> [...]
vline:
    ENTER 6                 ; 0=x 1=y 2=len 3=colour 4=i
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
    SET_COLOR
    PUSH 0
    LOCAL_SET 4
vl_loop:
    LOCAL_GET 4
    LOCAL_GET 2
    CMP_GE
    JMP_Z vl_body
    JMP vl_done
vl_body:
    LOCAL_GET 0
    LOCAL_GET 1
    LOCAL_GET 4
    ADD
    PLOT_STACK
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP vl_loop
vl_done:
    PUSH 0
    RET

; ------------------------------------------------------------------ layout

; [..., addr, len, x, y, cols, rows, colour] -> [...]
draw_text:
    ENTER 12                ; 0=addr 1=len 2=x 3=y 4=cols 5=rows 6=colour
                            ; 7=i 8=col 9=row 10=unused 11=unused
    PUSH 18
    PICK
    LOCAL_SET 0
    PUSH 17
    PICK
    LOCAL_SET 1
    PUSH 16
    PICK
    LOCAL_SET 2
    PUSH 15
    PICK
    LOCAL_SET 3
    PUSH 14
    PICK
    LOCAL_SET 4
    PUSH 13
    PICK
    LOCAL_SET 5
    PUSH 12
    PICK
    LOCAL_SET 6
    PUSH 0
    LOCAL_SET 7
    PUSH 0
    LOCAL_SET 8
    PUSH 0
    LOCAL_SET 9
dt_loop:
    LOCAL_GET 7
    LOCAL_GET 1
    CMP_GE
    JMP_Z dt_body
    JMP dt_done
dt_body:
    LOCAL_GET 0
    LOCAL_GET 7
    ADD
    LOAD8
    DUP
    PUSH NEWLINE
    CMP_EQ
    JMP_Z dt_char
    POP
    PUSH 0
    LOCAL_SET 8
    LOCAL_GET 9
    INC
    LOCAL_SET 9
    JMP dt_next
dt_char:
    LOCAL_GET 9
    LOCAL_GET 5
    CMP_LT
    JMP_Z dt_out_of_range
    JMP dt_draw
dt_out_of_range:
    POP                     ; drop code, row out of range
    JMP dt_wrap_only
dt_draw:
    LOCAL_GET 2
    LOCAL_GET 8
    PUSH PIXEL_W
    MUL
    ADD
    LOCAL_GET 3
    LOCAL_GET 9
    PUSH PIXEL_H
    MUL
    ADD
    ROT                     ; code, x, y -> x, y, code
    LOCAL_GET 6
    CALL draw_glyph
    POP
    POP
    POP
    POP
    POP
dt_wrap_only:
    LOCAL_GET 8
    INC
    LOCAL_SET 8
    LOCAL_GET 8
    LOCAL_GET 4
    CMP_GE
    JMP_Z dt_next
    PUSH 0
    LOCAL_SET 8
    LOCAL_GET 9
    INC
    LOCAL_SET 9
dt_next:
    LOCAL_GET 7
    INC
    LOCAL_SET 7
    JMP dt_loop
dt_done:
    PUSH 0
    RET

; [..., index] -> [...] and stores the visual cell in G_CX/G_CY.
caret_xy:
    ENTER 6                 ; 0=index 1=col 2=row 3=i 4=byte 5=unused
    PUSH 6
    PICK
    LOCAL_SET 0
    PUSH 0
    LOCAL_SET 1
    PUSH 0
    LOCAL_SET 2
    PUSH 0
    LOCAL_SET 3
cx_loop:
    LOCAL_GET 3
    LOCAL_GET 0
    CMP_GE
    JMP_Z cx_body
    JMP cx_store
cx_body:
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 3
    ADD
    LOAD8
    LOCAL_SET 4
    LOCAL_GET 4
    PUSH NEWLINE
    CMP_EQ
    JMP_Z cx_char
    PUSH 0
    LOCAL_SET 1
    LOCAL_GET 2
    INC
    LOCAL_SET 2
    JMP cx_next
cx_char:
    LOCAL_GET 1
    INC
    LOCAL_SET 1
    LOCAL_GET 1
    PUSH G_COLS
    LOAD32
    CMP_GE
    JMP_Z cx_next
    PUSH 0
    LOCAL_SET 1
    LOCAL_GET 2
    INC
    LOCAL_SET 2
cx_next:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP cx_loop
cx_store:
    LOCAL_GET 1
    PUSH G_CX
    STORE32
    LOCAL_GET 2
    PUSH G_CY
    STORE32
    PUSH 0
    RET

; ------------------------------------------------------------------ editing

; [..., byte] -> [..., 1 on insert, 0 when full]
insert_byte:
    ENTER 2                 ; 0=i 1=byte
    PUSH 2
    PICK
    LOCAL_SET 1
    PUSH G_LEN
    LOAD32
    PUSH TEXT_CAP
    CMP_GE
    JMP_Z ib_space
    PUSH 0
    RET
ib_space:
    PUSH G_LEN
    LOAD32
    LOCAL_SET 0
ib_shift_loop:
    LOCAL_GET 0
    PUSH G_CARET
    LOAD32
    CMP_GT
    JMP_Z ib_shift_done
ib_shift_do:
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 0
    PUSH 1
    SUB
    ADD
    LOAD8
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 0
    ADD
    STORE8
    LOCAL_GET 0
    DEC
    LOCAL_SET 0
    JMP ib_shift_loop
ib_shift_done:
    LOCAL_GET 1
    PUSH G_CARET
    LOAD32
    PUSH G_TEXT
    LOAD32
    ADD
    STORE8
    PUSH G_LEN
    LOAD32
    INC
    PUSH G_LEN
    STORE32
    PUSH G_CARET
    LOAD32
    INC
    PUSH G_CARET
    STORE32
    PUSH 1
    RET

; [..., index] -> [..., 1 on delete, 0 when out of range]
delete_at:
    ENTER 2                 ; 0=index 1=i
    PUSH 2
    PICK
    LOCAL_SET 0
    LOCAL_GET 0
    PUSH 0
    CMP_LT
    JMP_Z da_bad
    LOCAL_GET 0
    PUSH G_LEN
    LOAD32
    CMP_GE
    JMP_Z da_bad
    LOCAL_GET 0
    LOCAL_SET 1
da_shift_loop:
    LOCAL_GET 1
    PUSH G_LEN
    LOAD32
    PUSH 1
    SUB
    CMP_GE
    JMP_Z da_shift_done
da_shift_do:
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 1
    PUSH 1
    ADD
    ADD
    LOAD8
    PUSH G_TEXT
    LOAD32
    LOCAL_GET 1
    ADD
    STORE8
    LOCAL_GET 1
    INC
    LOCAL_SET 1
    JMP da_shift_loop
da_shift_done:
    PUSH G_LEN
    LOAD32
    DEC
    PUSH G_LEN
    STORE32
    PUSH 1
    RET
da_bad:
    PUSH 0
    RET

; ------------------------------------------------------------------ rendering

; [..., quit flag] -> [...]
draw_frame:
    PUSH C_BG
    SET_COLOR
    CLEAR

    PUSH title_text
    DUP
    STRLENZ
    PUSH 4
    PUSH 4
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_TITLE
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH BORDER
    PUSH BAR_H
    PUSH G_W
    LOAD32
    PUSH 2*BORDER
    SUB
    PUSH C_BORDER
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH BORDER
    PUSH G_H
    LOAD32
    PUSH BAR_H
    SUB
    PUSH 1
    SUB
    PUSH G_W
    LOAD32
    PUSH 2*BORDER
    SUB
    PUSH C_BORDER
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH BORDER
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_H
    LOAD32
    PUSH 2*BAR_H
    SUB
    PUSH 2
    SUB
    PUSH C_BORDER
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_W
    LOAD32
    PUSH BORDER
    SUB
    PUSH 1
    SUB
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_H
    LOAD32
    PUSH 2*BAR_H
    SUB
    PUSH 2
    SUB
    PUSH C_BORDER
    CALL vline
    POP
    POP
    POP
    POP
    POP

    PUSH G_TEXT
    LOAD32
    PUSH G_LEN
    LOAD32
    PUSH BORDER
    PUSH 1
    ADD
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_COLS
    LOAD32
    PUSH G_ROWS
    LOAD32
    PUSH C_TEXT
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    NOW_MS
    PUSH BLINK_MS
    DIV
    PUSH 1
    AND
    JMP_Z df_no_caret
    PUSH G_CARET
    LOAD32
    CALL caret_xy
    POP
    POP
    PUSH G_CY
    LOAD32
    PUSH G_ROWS
    LOAD32
    CMP_LT
    JMP_Z df_no_caret
    PUSH BORDER
    PUSH 1
    ADD
    PUSH G_CX
    LOAD32
    PUSH PIXEL_W
    MUL
    ADD
    PUSH BAR_H
    PUSH 1
    ADD
    PUSH G_CY
    LOAD32
    PUSH PIXEL_H
    MUL
    ADD
    PUSH 124              ; '|'
    PUSH C_CARET
    CALL draw_glyph
    POP
    POP
    POP
    POP
    POP
df_no_caret:

    PUSH status_text
    DUP
    STRLENZ
    PUSH 4
    PUSH G_H
    LOAD32
    PUSH BAR_H
    SUB
    PUSH 3
    ADD
    PUSH G_COLS
    LOAD32
    PUSH 1
    PUSH C_STATUS
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
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
    PUSH G_H
    STORE32
    PUSH G_W
    STORE32

    PUSH G_W
    LOAD32
    PUSH 2*BORDER
    SUB
    PUSH PIXEL_W
    DIV
    PUSH G_COLS
    STORE32

    PUSH G_H
    LOAD32
    PUSH 2*BAR_H
    SUB
    PUSH 2*BORDER
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
    CMP_LT
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
    PUSH G_SLEEP
    LOAD32
    PUSH 0
    CMP_LE
    JMP_Z frame_ready
    PUSH G_SLEEP
    LOAD32
    PUSH 50
    CMP_GT
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
    CMP_LT
    JMP_Z bs_bad
    DUP
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
