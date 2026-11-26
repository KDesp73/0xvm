-- A typography demo written for the 0xluac subset.  This is the Lua
-- counterpart of examples/typography.asm: arrows move the caret, printable
-- keys (including space) insert at the caret, enter inserts a newline, and
-- backspace/delete remove bytes.  Escape or window close exits.
--
-- `const` data tables are the subset's global storage: assembly labels are
-- writable VM memory, so editor state and the text buffer live here instead of
-- in locals that functions could not share.  String function parameters and
-- returns are outside the subset, so on-screen captions are byte tables too.
-- The intro and completion line use the string literal bindings.  Set ENABLE
-- to 0 for the CLI golden run, where the host has no screen device.  REPORT
-- exposes the state and text labels for the scripted browser editing check.

-- ---------------------------------------------------------------- constants
const ENABLE = 1
const REPORT = 0
const SCALE = 2
const GLYPH_W = 5
const CELL_H = 8
const PIXEL_W = 12
const PIXEL_H = 16
const BORDER = 2
const BAR_H = 20
const FRAME_MS = 66
const BLINK_MS = 250
const MAX_FRAMES = 0
const TEXT_CAP = 256
const FONT_FIRST = 32
const FONT_LAST = 126

const KEY_UP = 1
const KEY_DOWN = 2
const KEY_LEFT = 3
const KEY_RIGHT = 4
const KEY_SPACE = 5
const KEY_ENTER = 6
const KEY_ESC = 7
const KEY_BACKSPACE = 8
const KEY_DELETE = 9
const NEWLINE = 10

const C_BG = 0x101018
const C_BORDER = 0x405080
const C_TITLE = 0x00E0FF
const C_TEXT = 0xE8E8F0
const C_CARET = 0xFFC040
const C_STATUS = 0x90A0C0

const SRC_TITLE = 0
const SRC_STATUS = 1
const SRC_TEXT = 2

-- Indices into `state`.
const S_W = 0
const S_H = 1
const S_COLS = 2
const S_ROWS = 3
const S_LEN = 4
const S_CARET = 5
const S_LAST = 6
const S_FRAMES = 7
const S_CX = 8
const S_CY = 9
const S_DIRTY = 10
const S_SLEEP = 11
const S_QUIT = 12

const TITLE_LEN = 29
const STATUS_LEN = 50

-- -------------------------------------------------------------- global data
const state = words { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

const text = bytes {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}

const title = bytes {
    48, 88, 86, 77, 32, 84, 89, 80, 69, 32, 47, 32, 66, 73, 84, 77,
    65, 80, 32, 84, 89, 80, 79, 71, 82, 65, 80, 72, 89
}

const status = bytes {
    80, 82, 73, 78, 84, 65, 66, 76, 69, 32, 65, 83, 67, 73, 73, 32,
    69, 78, 84, 69, 82, 83, 32, 84, 69, 88, 84, 32, 124, 32, 83, 80,
    65, 67, 69, 32, 73, 83, 32, 65, 32, 67, 72, 65, 82, 65, 67, 84,
    69, 82
}

-- 95 printable ASCII glyphs, 32..126.  Each quad holds eight row bytes in
-- big-endian order; row 0 is the low byte, matching the assembly font table.
const glyphs = quads {
    0, -- space
    2305878331563712512, -- !
    1347440640, -- "
    5787398547818172416, -- #
    2373441467817926656, -- $
    1772236959835013120, -- %
    7534707172116029440, -- &
    1075863552, -- '
    1161999348559515648, -- (
    4620710878858199040, -- )
    9192401065213952, -- *
    9043449317687296, -- +
    4620798770798395392, -- ,
    1065151889408, -- -
    6944550625405304832, -- .
    36099303471054848, -- /
    8108951755519717376, -- 0
    8079493053856882688, -- 1
    -558411100559216640, -- 2
    8108739994431059968, -- 3
    1157698402938523648, -- 4
    8108739963568322560, -- 5
    8108881695610007552, -- 6
    36099303471118336, -- 7
    8108881145988935680, -- 8
    6922041941048193024, -- 9
    27127152497393664, -- :
    4620798772415299584, -- ;
    1161999623437422592, -- <
    272683044438016, -- =
    4620710844498460672, -- >
    2305878262448418816, -- ?
    8117923493108543488, -- @
    -8608357422428819456, -- A
    -1114490341109993472, -- B
    8108872418481172480, -- C
    -2265160492579233792, -- D
    -540290184848017408, -- E
    -9187201469399369728, -- F
    8685342207395786752, -- G
    -8608480086694787072, -- H
    8079493053852708864, -- I
    6958079085462435840, -- J
    -8606202138717026304, -- K
    -540290665884385280, -- L
    -8608480429750056960, -- M
    -8608462837032384512, -- N
    8108881249068150784, -- O
    -9187201469264629760, -- P
    7534707480950501376, -- Q
    -8606201932961746944, -- R
    -1150660425507899392, -- S
    2314885530818508800, -- T
    8108881249068156928, -- U
    2328511127338125312, -- V
    5812080760920246272, -- W
    -8608542587998402560, -- X
    2314885738728556544, -- Y
    -540361448832305152, -- Z
    8088535575457460224, -- [
    2269530520813568, -- \
    8074971793050333184, -- ]
    2286952448, -- ^
    -576460752303423488, -- _
    270548992, -- `
    8685323859018121216, -- a
    -1114490512238149632, -- b
    8108872418203795456, -- c
    8685342069545764864, -- d
    8106752594145181696, -- e
    4629771748832194560, -- f
    8072834859862065152, -- g
    -8608480292182654976, -- h
    8079493054924333056, -- i
    6958079085998247936, -- j
    -8025202740122583040, -- k
    8079493053852704768, -- l
    -8608445244721070080, -- m
    -8608480292191076352, -- n
    8108881248656556032, -- o
    -9187078768903061504, -- p
    578845148092301312, -- q
    -9187192844299763712, -- r
    -1150546007855988736, -- s
    3479101359528099840, -- t
    7536924096383156224, -- u
    2328511127329177600, -- v
    5812080623472345088, -- w
    -8624357756163325952, -- x
    8072834859854200832, -- y
    -558411096541626368, -- z
    1161964163650555904, -- {
    2314885530818453504, -- |
    4620728471312678912, -- }
    9025856863207424 -- ~
}

-- ------------------------------------------------------------------- lines
function hline(x, y, count, color)
    set_color(color)
    local i = 0
    while i < count do
        plot(x + i, y)
        i = i + 1
    end
end

function vline(x, y, count, color)
    set_color(color)
    local i = 0
    while i < count do
        plot(x, y + i)
        i = i + 1
    end
end

-- ------------------------------------------------------------------- glyphs
function draw_glyph(x, y, code, color)
    if code < FONT_FIRST then
        code = 63
    elseif code > FONT_LAST then
        code = 63
    end
    set_color(color)
    local glyph = glyphs[code - FONT_FIRST]
    local row = 0
    while row < CELL_H do
        local bits = (glyph >> (row * 8)) & 0xFF
        local col = 0
        while col < GLYPH_W do
            if ((bits >> (7 - col)) & 1) == 1 then
                local sy = 0
                while sy < SCALE do
                    local sx = 0
                    while sx < SCALE do
                        local px = x + col * SCALE + sx
                        local py = y + row * SCALE + sy
                        if px < state[S_W] then
                            if py < state[S_H] then
                                plot(px, py)
                            end
                        end
                        sx = sx + 1
                    end
                    sy = sy + 1
                end
            end
            col = col + 1
        end
        row = row + 1
    end
end

-- ------------------------------------------------------------------- layout
function caret_xy(index)
    local col = 0
    local row = 0
    local i = 0
    while i < index do
        local byte = text[i]
        if byte == NEWLINE then
            col = 0
            row = row + 1
        else
            col = col + 1
            if col >= state[S_COLS] then
                col = 0
                row = row + 1
            end
        end
        i = i + 1
    end
    state[S_CX] = col
    state[S_CY] = row
end

function draw_text(source, count, x, y, cols, rows, color)
    local i = 0
    local col = 0
    local row = 0
    while i < count do
        local code = 0
        if source == SRC_TITLE then
            code = title[i]
        elseif source == SRC_STATUS then
            code = status[i]
        else
            code = text[i]
        end
        if code == NEWLINE then
            col = 0
            row = row + 1
        else
            if row < rows then
                draw_glyph(x + col * PIXEL_W, y + row * PIXEL_H, code, color)
            end
            col = col + 1
            if col >= cols then
                col = 0
                row = row + 1
            end
        end
        i = i + 1
    end
end

-- ------------------------------------------------------------------ editing
function insert_byte(byte)
    if state[S_LEN] >= TEXT_CAP then
        return 0
    end
    local i = state[S_LEN]
    while i > state[S_CARET] do
        text[i] = text[i - 1]
        i = i - 1
    end
    text[state[S_CARET]] = byte
    state[S_LEN] = state[S_LEN] + 1
    state[S_CARET] = state[S_CARET] + 1
    return 1
end

function delete_at(index)
    if index < 0 then
        return 0
    end
    if index >= state[S_LEN] then
        return 0
    end
    local i = index
    while i < state[S_LEN] - 1 do
        text[i] = text[i + 1]
        i = i + 1
    end
    state[S_LEN] = state[S_LEN] - 1
    return 1
end

function handle_key(key)
    if key == KEY_ESC then
        state[S_QUIT] = 1
    elseif key == KEY_UP then
        state[S_CARET] = state[S_CARET] - state[S_COLS]
        if state[S_CARET] < 0 then
            state[S_CARET] = 0
        end
    elseif key == KEY_DOWN then
        state[S_CARET] = state[S_CARET] + state[S_COLS]
        if state[S_CARET] > state[S_LEN] then
            state[S_CARET] = state[S_LEN]
        end
    elseif key == KEY_LEFT then
        state[S_CARET] = state[S_CARET] - 1
        if state[S_CARET] < 0 then
            state[S_CARET] = 0
        end
    elseif key == KEY_RIGHT then
        state[S_CARET] = state[S_CARET] + 1
        if state[S_CARET] > state[S_LEN] then
            state[S_CARET] = state[S_LEN]
        end
    elseif key == KEY_ENTER then
        insert_byte(NEWLINE)
    elseif key == KEY_BACKSPACE then
        local at = state[S_CARET] - 1
        if at >= 0 then
            delete_at(at)
            state[S_CARET] = at
        end
    elseif key == KEY_DELETE then
        delete_at(state[S_CARET])
    elseif key == KEY_SPACE then
        insert_byte(FONT_FIRST)
    elseif key >= FONT_FIRST then
        if key <= FONT_LAST then
            insert_byte(key)
        end
    end
end

-- ------------------------------------------------------------------ drawing
function draw_frame()
    set_color(C_BG)
    clear()

    draw_text(SRC_TITLE, TITLE_LEN, 4, 4, state[S_COLS], 1, C_TITLE)

    local inner_w = state[S_W] - 2 * BORDER
    hline(BORDER, BAR_H, inner_w, C_BORDER)
    hline(BORDER, state[S_H] - BAR_H - 1, inner_w, C_BORDER)
    vline(BORDER, BAR_H + 1, state[S_H] - 2 * BAR_H - 2, C_BORDER)
    vline(state[S_W] - BORDER - 1, BAR_H + 1,
          state[S_H] - 2 * BAR_H - 2, C_BORDER)

    draw_text(SRC_TEXT, state[S_LEN], BORDER + 1, BAR_H + 1,
              state[S_COLS], state[S_ROWS], C_TEXT)

    if ((now_ms() / BLINK_MS) & 1) == 1 then
        caret_xy(state[S_CARET])
        if state[S_CY] < state[S_ROWS] then
            local x = BORDER + 1 + state[S_CX] * PIXEL_W - 2
            local y = BAR_H + 1 + state[S_CY] * PIXEL_H
            vline(x, y, PIXEL_H, C_CARET)
            vline(x + 1, y, PIXEL_H, C_CARET)
        end
    end

    draw_text(SRC_STATUS, STATUS_LEN, 4, state[S_H] - BAR_H + 3,
              state[S_COLS], 1, C_STATUS)
    draw()
end

-- -------------------------------------------------------------------- start
print_str("TYPE | arrows move | type inserts | enter newline | bksp/del remove | esc quits")
if REPORT == 1 then
    print(state)
    print(text)
end

if ENABLE == 1 then
    local w, h = screen_size()
    state[S_W] = w
    state[S_H] = h
    state[S_COLS] = (w - 2 * BORDER) / PIXEL_W
    state[S_ROWS] = (h - 2 * BAR_H - 2 * BORDER) / PIXEL_H
    state[S_LEN] = 0
    state[S_CARET] = 0
    state[S_LAST] = 0
    state[S_FRAMES] = 0
    state[S_CX] = 0
    state[S_CY] = 0
    state[S_DIRTY] = 1
    state[S_SLEEP] = 0
    state[S_QUIT] = 0

    draw_frame()
    state[S_DIRTY] = 0

    while 1 == 1 do
        if quit_requested() == 1 then
            break
        end
        if state[S_QUIT] == 1 then
            break
        end

        local key = read_key()
        if key == 0 then
            if state[S_DIRTY] == 0 then
                local wait = FRAME_MS - (now_ms() - state[S_LAST])
                if wait > 0 then
                    if wait > 50 then
                        wait = 50
                    end
                    sleep(wait)
                    continue
                end
            end
        end

        state[S_LAST] = now_ms()
        if key ~= 0 then
            state[S_DIRTY] = 1
            handle_key(key)
        end
        if state[S_QUIT] == 1 then
            break
        end
        if state[S_DIRTY] == 1 then
            draw_frame()
            state[S_DIRTY] = 0
        end

        state[S_FRAMES] = state[S_FRAMES] + 1
        if MAX_FRAMES ~= 0 then
            if state[S_FRAMES] >= MAX_FRAMES then
                break
            end
        end
    end
end

print_str("TYPOGRAPHY OK")
