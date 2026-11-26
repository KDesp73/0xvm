; SPDX-License-Identifier: MIT
; 0xvm boot ROM — house-style splash for every first-party host.
;
; Uses VM_SYS_CAPS to choose its output path: text-only hosts get the final
; docs/LOGO.txt block on the terminal (without the trailing wordmark), while
; screen hosts redraw the same 26x10 grid as 8x8 cells. Both paths include VM
; information. The screen layout uses the four house palette slots, an inverted
; 8px header strip, and hairline boxes from docs/Design-Language.md. The frame
; stays up until Escape or a window close; smoke builds can set BOOT_MAX_MS to
; cap the wait.

INCLUDE "all.asm"

; ------------------------------------------------------------------ constants

EQU SCALE        1
EQU GLYPH_W      5
EQU CELL_W       GLYPH_W+1
EQU CELL_H       8
EQU PIXEL_W      CELL_W*SCALE
EQU PIXEL_H      CELL_H*SCALE
EQU FONT_FIRST   32
EQU FONT_LAST    126
EQU NEWLINE      10

EQU TILE         8
EQU LOGO_COLS    26
EQU LOGO_ROWS    10
EQU LOGO_Y       24
EQU KEY_ESC      7
EQU SLEEP_MS     0x41
IFNDEF BOOT_MAX_MS
EQU BOOT_MAX_MS  0          ; 0 = wait for Escape or window close
ENDIF

; House palette: 0 canvas, 1 ink, 2 structure, 3 pale-green accent.
EQU C_CANVAS     0x060907
EQU C_INK        0xF2F6F0
EQU C_STRUCT     0x2A352C
EQU C_ACCENT     0xB9E5AD

; Zero-page globals shared with the typography glyph routines.
EQU G_W          0
EQU G_H          4
EQU G_COLS       8
EQU G_ROWS       12
EQU G_TEXT       16
EQU G_LEN        20
EQU G_CX         24
EQU G_CY         28
EQU G_END        32

; ------------------------------------------------------------------ data

logo_text:
    STR " ▄▄▄                      \n██▀██                     \n▓▓ ▓▓                     \n▓▓ ▓▓                     \n▒▒ ▒▒ ▄▄ ▄▄ ▄▄ ▄▄ ▄▄▄▄▄▄  \n▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒▒ ▒ ▒▒ \n░░ ░░ ░░ ░░ ░░ ░░ ░░ ░ ░░ \n░░ ░░  ░▀░  ░░ ░░ ░░ ░ ░░ \n██▄██ ██ ██ ▀█ █▀ ██   ██ \n ▀▀▀  ▀▀ ▀▀   ▀   ▀▀   ▀▀ "
    DB 0

; One byte per screen cell for the first 26 columns of logo_text:
; 0 blank, 1 full, 2 heavy, 3 medium, 4 light, 5 upper half, 6 lower half.
logo_cells:
    DB 0, 6, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    DB 1, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    DB 2, 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    DB 2, 2, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    DB 3, 3, 0, 3, 3, 0, 6, 6, 0, 6, 6, 0, 6, 6, 0, 6, 6, 0, 6, 6, 6, 6, 6, 6, 0, 0
    DB 3, 3, 0, 3, 3, 0, 3, 3, 0, 3, 3, 0, 3, 3, 0, 3, 3, 0, 3, 3, 0, 3, 0, 3, 3, 0
    DB 4, 4, 0, 4, 4, 0, 4, 4, 0, 4, 4, 0, 4, 4, 0, 4, 4, 0, 4, 4, 0, 4, 0, 4, 4, 0
    DB 4, 4, 0, 4, 4, 0, 0, 4, 5, 4, 0, 0, 4, 4, 0, 4, 4, 0, 4, 4, 0, 4, 0, 4, 4, 0
    DB 1, 1, 6, 1, 1, 0, 1, 1, 0, 1, 1, 0, 5, 1, 0, 1, 5, 0, 1, 1, 0, 0, 0, 1, 1, 0
    DB 0, 5, 5, 5, 0, 0, 5, 5, 0, 5, 5, 0, 0, 0, 5, 0, 0, 0, 5, 5, 0, 0, 0, 5, 5, 0

header_title:
    STR "0XVM / BOOT"
    DB 0

header_right:
    STR "VMIM V2"
    DB 0

info_line_1:
    STR "VMIM V2 / 64K RAM / 64-BIT CELLS"
    DB 0

info_line_2:
    STR "DEVICE PAGE / 4-COLOUR PALETTE"
    DB 0

status_text:
    STR "BOOT OK"
    DB 0

term_title:
    STR "0XVM BOOT"
    DB 0

term_version:
    STR "VMIM VERSION"
    DB 0

term_ram:
    STR "RAM BYTES"
    DB 0

term_cell:
    STR "CELL BITS"
    DB 0

term_palette:
    STR "HOUSE PALETTE"
    DB 0

term_palette_values:
    STR "0 #060907 1 #F2F6F0 2 #2A352C 3 #B9E5AD"
    DB 0

INCLUDE "typography/font.asm"
INCLUDE "typography/macros.asm"

    JMP start

INCLUDE "typography/glyphs.asm"
INCLUDE "typography/lines.asm"
INCLUDE "typography/layout.asm"

; ------------------------------------------------------------------ drawing helpers

; [..., x, y, w, h, colour] -> [..., 0]
fill_rect:
    ENTER 8                 ; 0=x 1=y 2=w 3=h 4=colour 5=row
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

; [..., x, y, code] -> [..., 0]
; Draws one 8x8 block cell using only canvas and ink, with dithered shades.
draw_block:
    ENTER 10                ; 0=x 1=y 2=code 3=row 4=col
                            ; 5=subx 6=suby 7=on 8=px 9=py
    PUSH 12
    PICK
    LOCAL_SET 0
    PUSH 11
    PICK
    LOCAL_SET 1
    PUSH 10
    PICK
    LOCAL_SET 2

    PUSH C_CANVAS
    SET_COLOR
    PUSH 0
    LOCAL_SET 3
db_bg_row:
    LOCAL_GET 3
    PUSH CELL_H
    CMP_GE
    JMP_Z db_bg_row_body
    JMP db_shape
db_bg_row_body:
    PUSH 0
    LOCAL_SET 4
db_bg_col:
    LOCAL_GET 4
    PUSH CELL_H
    CMP_GE
    JMP_Z db_bg_pixel
    JMP db_bg_row_next
db_bg_pixel:
    LOCAL_GET 0
    LOCAL_GET 4
    ADD
    LOCAL_GET 1
    LOCAL_GET 3
    ADD
    PLOT_STACK
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP db_bg_col
db_bg_row_next:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP db_bg_row

db_shape:
    LOCAL_GET 2
    JMP_Z db_done
    PUSH C_INK
    SET_COLOR
    PUSH 0
    LOCAL_SET 3
db_row:
    LOCAL_GET 3
    PUSH CELL_H
    CMP_GE
    JMP_Z db_row_body
    JMP db_done
db_row_body:
    PUSH 0
    LOCAL_SET 4
db_col:
    LOCAL_GET 4
    PUSH CELL_H
    CMP_GE
    JMP_Z db_col_body
    JMP db_row_next
db_col_body:
    LOCAL_GET 4
    PUSH 2
    SHR
    LOCAL_SET 5
    LOCAL_GET 3
    PUSH 2
    SHR
    LOCAL_SET 6
    PUSH 1
    LOCAL_SET 7

    LOCAL_GET 2
    PUSH 1
    CMP_EQ
    JMP_Z db_check_heavy
    JMP db_pixel

db_check_heavy:
    LOCAL_GET 2
    PUSH 2
    CMP_EQ
    JMP_Z db_check_medium
    LOCAL_GET 5
    PUSH 1
    CMP_EQ
    JMP_Z db_check_medium
    LOCAL_GET 6
    PUSH 1
    CMP_EQ
    JMP_Z db_check_medium
    PUSH 0
    LOCAL_SET 7
    JMP db_pixel

db_check_medium:
    LOCAL_GET 2
    PUSH 3
    CMP_EQ
    JMP_Z db_check_light
    LOCAL_GET 5
    LOCAL_GET 6
    ADD
    PUSH 1
    AND
    JMP_Z db_check_light
    PUSH 0
    LOCAL_SET 7
    JMP db_pixel

db_check_light:
    LOCAL_GET 2
    PUSH 4
    CMP_EQ
    JMP_Z db_check_upper
    LOCAL_GET 5
    JMP_Z db_keep_light
    PUSH 0
    LOCAL_SET 7
    JMP db_pixel
db_keep_light:
    LOCAL_GET 6
    JMP_Z db_pixel
    PUSH 0
    LOCAL_SET 7
    JMP db_pixel

db_check_upper:
    LOCAL_GET 2
    PUSH 5
    CMP_EQ
    JMP_Z db_check_lower
    LOCAL_GET 6
    JMP_Z db_pixel
    PUSH 0
    LOCAL_SET 7
    JMP db_pixel

db_check_lower:
    LOCAL_GET 2
    PUSH 6
    CMP_NE
    JMP_Z db_pixel
    LOCAL_GET 6
    JMP_Z db_clear_pixel
    JMP db_pixel
db_clear_pixel:
    PUSH 0
    LOCAL_SET 7

db_pixel:
    LOCAL_GET 7
    JMP_Z db_pixel_next
    LOCAL_GET 0
    LOCAL_GET 4
    ADD
    LOCAL_GET 1
    LOCAL_GET 3
    ADD
    PLOT_STACK
db_pixel_next:
    LOCAL_GET 4
    INC
    LOCAL_SET 4
    JMP db_col
db_row_next:
    LOCAL_GET 3
    INC
    LOCAL_SET 3
    JMP db_row
db_done:
    PUSH 0
    RET

; [...] -> [..., 0]
draw_logo:
    ENTER 6                 ; 0=x 1=row 2=col 3=code 4=cell
    PUSH G_W
    LOAD32
    PUSH LOGO_COLS
    PUSH CELL_H
    MUL
    SUB
    PUSH 1
    SHR
    LOCAL_SET 0
    PUSH 0
    LOCAL_SET 1
dl_row:
    LOCAL_GET 1
    PUSH LOGO_ROWS
    CMP_GE
    JMP_Z dl_row_body
    JMP dl_done
dl_row_body:
    PUSH 0
    LOCAL_SET 2
dl_col:
    LOCAL_GET 2
    PUSH LOGO_COLS
    CMP_GE
    JMP_Z dl_col_body
    JMP dl_row_next
dl_col_body:
    LOCAL_GET 1
    PUSH LOGO_COLS
    MUL
    LOCAL_GET 2
    ADD
    PUSH logo_cells
    ADD
    LOAD8
    LOCAL_SET 3
    LOCAL_GET 0
    LOCAL_GET 2
    PUSH CELL_H
    MUL
    ADD
    PUSH LOGO_Y
    LOCAL_GET 1
    PUSH CELL_H
    MUL
    ADD
    LOCAL_GET 3
    CALL draw_block
    POP
    POP
    POP
    POP
    LOCAL_GET 2
    INC
    LOCAL_SET 2
    JMP dl_col
dl_row_next:
    LOCAL_GET 1
    INC
    LOCAL_SET 1
    JMP dl_row
dl_done:
    PUSH 0
    RET

; [...] -> [..., 0]
draw_frame:
    PUSH C_CANVAS
    SET_COLOR
    CLEAR

    ; Inverted header strip.
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
    PUSH 8
    PUSH 0
    PUSH 64
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
    PUSH 7
    PUSH PIXEL_W
    MUL
    SUB
    PUSH 8
    SUB
    PUSH header_right
    DUP
    STRLENZ
    ROT
    PUSH 0
    PUSH 7
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
    PUSH 8
    PUSH 16
    PUSH G_W
    LOAD32
    PUSH 16
    SUB
    PUSH C_STRUCT
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH 8
    PUSH G_H
    LOAD32
    PUSH 8
    SUB
    PUSH G_W
    LOAD32
    PUSH 16
    SUB
    PUSH C_STRUCT
    CALL hline
    POP
    POP
    POP
    POP
    POP

    PUSH 8
    PUSH 17
    PUSH G_H
    LOAD32
    PUSH 25
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
    PUSH 9
    SUB
    PUSH 17
    PUSH G_H
    LOAD32
    PUSH 25
    SUB
    PUSH C_STRUCT
    CALL vline
    POP
    POP
    POP
    POP
    POP

    CALL draw_logo
    POP

    PUSH info_line_1
    DUP
    STRLENZ
    PUSH 16
    PUSH 120
    PUSH 64
    PUSH 1
    PUSH C_INK
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH info_line_2
    DUP
    STRLENZ
    PUSH 16
    PUSH 136
    PUSH 64
    PUSH 1
    PUSH C_INK
    CALL draw_text
    POP
    POP
    POP
    POP
    POP
    POP
    POP
    POP

    PUSH status_text
    DUP
    STRLENZ
    PUSH 16
    PUSH 168
    PUSH 16
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

    DRAW
    PUSH 0
    RET

; ------------------------------------------------------------------ startup

start:
    DEI SYS_CAPS
    PUSH CAPS_SCREEN
    AND
    JMP_Z terminal_only

screen_only:
    DEI SCREEN_WIDTH
    PUSH G_W
    STORE32
    DEI SCREEN_HEIGHT
    PUSH G_H
    STORE32
    CALL draw_frame
    POP

    ; Optional finite wait for smoke builds; the shipped ROM waits forever.
    PUSH BOOT_MAX_MS
    JMP_Z screen_no_deadline
    NOW_MS
    PUSH BOOT_MAX_MS
    ADD
    PUSH G_END
    STORE32
screen_no_deadline:

screen_wait:
    QUIT_REQUESTED
    JMP_Z screen_no_quit
    JMP screen_done
screen_no_quit:
    READ_KEY
    DUP
    PUSH KEY_ESC
    CMP_EQ
    JMP_Z screen_key_ignored
    POP
    JMP screen_done
screen_key_ignored:
    POP

    PUSH BOOT_MAX_MS
    JMP_Z screen_wait_more
    NOW_MS
    PUSH G_END
    LOAD32
    CMP_GE
    JMP_Z screen_wait_more
    JMP screen_done
screen_wait_more:
    PUSH 50
    DEO SLEEP_MS
    JMP screen_wait

screen_done:
    HALT

terminal_only:
    ; Text-only host: block glyphs from docs/LOGO.txt, then VM information.
    PUSH logo_text
    DUP
    STRLENZ
    PRINT_STR

    PUSH term_title
    DUP
    STRLENZ
    PRINT_STR

    PUSH term_version
    DUP
    STRLENZ
    PRINT_STR
    DEI SYS_VERSION
    PRINT

    PUSH term_ram
    DUP
    STRLENZ
    PRINT_STR
    PUSH 65536
    PRINT

    PUSH term_cell
    DUP
    STRLENZ
    PRINT_STR
    PUSH 64
    PRINT

    PUSH term_palette
    DUP
    STRLENZ
    PRINT_STR
    PUSH term_palette_values
    DUP
    STRLENZ
    PRINT_STR
    HALT
