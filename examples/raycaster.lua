-- A first-person raycaster written for the 0xluac subset.  It is a small
-- maze explorer: arrows turn and walk, space and enter strafe, and escape
-- quits.  Walls are drawn with integer DDA raycasting and a distance palette;
-- a minimap shows the player's cell.
--
-- As in the typography demo, `const` data tables are the subset's writable
-- global storage.  ENABLE defaults to 0 so the plain CLI host can run the
-- golden; SDL3 and WASM builds flip it to 1.  REPORT exposes the state and map
-- labels for the scripted browser check.

-- ---------------------------------------------------------------- constants
const ENABLE = 0
const REPORT = 0
const POS_SHIFT = 8
const POS_SCALE = 256
const TRIG_SCALE = 256
const ANGLE_COUNT = 256
const MOVE_STEP = 40
const TURN_STEP = 3
const PLAYER_RADIUS = 40
const FRAME_MS = 33
const MAX_FRAMES = 0
const MAP_W = 16
const MAP_H = 16
const MAX_STEPS = 64
const CAMERA_SCALE = 256

const KEY_UP = 1
const KEY_DOWN = 2
const KEY_LEFT = 3
const KEY_RIGHT = 4
const KEY_SPACE = 5
const KEY_ENTER = 6
const KEY_ESC = 7

const C_CEIL = 0x202838
const C_FLOOR = 0x303040
const C_MAP_WALL = 0x0080C0
const C_PLAYER = 0xFFC040
const C_CROSS = 0xE0E0FF

-- Indices into `state`.
const S_W = 0
const S_H = 1
const S_X = 2
const S_Y = 3
const S_ANGLE = 4
const S_SIDE = 5
const S_FRAMES = 6
const S_LAST = 7
const S_QUIT = 8

-- -------------------------------------------------------------- global data
const state = words { 0, 0, 0, 0, 0, 0, 0, 0, 0 }

const map = bytes {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -- row 0
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -- row 1
    1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, -- row 2
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, -- row 3
    1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, -- row 4
    1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, -- row 5
    1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, -- row 6
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, -- row 7
    1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, -- row 8
    1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, -- row 9
    1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, -- row 10
    1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -- row 11
    1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, -- row 12
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -- row 13
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, -- row 14
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 -- row 15
}

-- 256-entry sine/cosine tables scaled by 256.
const cos_table = words {
    256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245, 243, 241, 239,
    237, 234, 231, 229, 226, 223, 220, 216, 213, 209, 206, 202, 198, 194, 190, 185,
    181, 177, 172, 167, 162, 157, 152, 147, 142, 137, 132, 126, 121, 115, 109, 104,
    98, 92, 86, 80, 74, 68, 62, 56, 50, 44, 38, 31, 25, 19, 13, 6,
    0, -6, -13, -19, -25, -31, -38, -44, -50, -56, -62, -68, -74, -80, -86, -92,
    -98, -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
    -181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
    -237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
    -256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
    -237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
    -181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
    -98, -92, -86, -80, -74, -68, -62, -56, -50, -44, -38, -31, -25, -19, -13, -6,
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92,
    98, 104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177,
    181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256
}

const sin_table = words {
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92,
    98, 104, 109, 115, 121, 126, 132, 137, 142, 147, 152, 157, 162, 167, 172, 177,
    181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256,
    256, 256, 256, 255, 255, 254, 253, 252, 251, 250, 248, 247, 245, 243, 241, 239,
    237, 234, 231, 229, 226, 223, 220, 216, 213, 209, 206, 202, 198, 194, 190, 185,
    181, 177, 172, 167, 162, 157, 152, 147, 142, 137, 132, 126, 121, 115, 109, 104,
    98, 92, 86, 80, 74, 68, 62, 56, 50, 44, 38, 31, 25, 19, 13, 6,
    0, -6, -13, -19, -25, -31, -38, -44, -50, -56, -62, -68, -74, -80, -86, -92,
    -98, -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
    -181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
    -237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
    -256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
    -237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
    -181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
    -98, -92, -86, -80, -74, -68, -62, -56, -50, -44, -38, -31, -25, -19, -13, -6
}

-- Near-to-far wall colours; index 0 is closest.  The second palette is for
-- Y-side hits, which gives the walls a simple directional shade.
const wall_colors = words {
    0x00E0FF, 0x01D4F3, 0x03C8E7, 0x04BCDC, 0x06B1D0, 0x08A5C4, 0x0999B9, 0x0B8DAD, 0x0C82A1, 0x0E7696, 0x106A8A, 0x115E7E, 0x135373, 0x144767, 0x163B5B, 0x183050
}

const dark_colors = words {
    0x00707F, 0x006A79, 0x016473, 0x025E6E, 0x035868, 0x045262, 0x044C5C, 0x054656, 0x064150, 0x073B4B, 0x083545, 0x082F3F, 0x092939, 0x0A2333, 0x0B1D2D, 0x0C1828
}

-- ------------------------------------------------------------------- helpers
function abs(n)
    if n < 0 then
        return -n
    end
    return n
end

function hline(x, y, count, color)
    set_color(color)
    local i = 0
    while i < count do
        plot(x + i, y)
        i = i + 1
    end
end

function fill_rect(x, y, w, h, color)
    local row = 0
    while row < h do
        hline(x, y + row, w, color)
        row = row + 1
    end
end

function cell_at(mx, my)
    if mx < 0 then
        return 1
    end
    if mx >= MAP_W then
        return 1
    end
    if my < 0 then
        return 1
    end
    if my >= MAP_H then
        return 1
    end
    return map[my * MAP_W + mx]
end

-- ------------------------------------------------------------------- movement
function can_occupy(x, y)
    local x0 = (x - PLAYER_RADIUS) >> POS_SHIFT
    local x1 = (x + PLAYER_RADIUS) >> POS_SHIFT
    local y0 = (y - PLAYER_RADIUS) >> POS_SHIFT
    local y1 = (y + PLAYER_RADIUS) >> POS_SHIFT
    if cell_at(x0, y0) ~= 0 then
        return 0
    end
    if cell_at(x1, y0) ~= 0 then
        return 0
    end
    if cell_at(x0, y1) ~= 0 then
        return 0
    end
    if cell_at(x1, y1) ~= 0 then
        return 0
    end
    return 1
end

function try_move(dx, dy)
    local nx = state[S_X] + dx
    local ny = state[S_Y] + dy
    if can_occupy(nx, ny) == 0 then
        return 0
    end
    state[S_X] = nx
    state[S_Y] = ny
    return 1
end

function move_forward(direction)
    local dx = cos_table[state[S_ANGLE]] * MOVE_STEP * direction / TRIG_SCALE
    local dy = sin_table[state[S_ANGLE]] * MOVE_STEP * direction / TRIG_SCALE
    try_move(dx, dy)
end

function strafe(direction)
    local dx = -sin_table[state[S_ANGLE]] * MOVE_STEP * direction / TRIG_SCALE
    local dy = cos_table[state[S_ANGLE]] * MOVE_STEP * direction / TRIG_SCALE
    try_move(dx, dy)
end

function handle_key(key)
    if key == KEY_ESC then
        state[S_QUIT] = 1
    elseif key == KEY_LEFT then
        state[S_ANGLE] = (state[S_ANGLE] - TURN_STEP + ANGLE_COUNT) % ANGLE_COUNT
    elseif key == KEY_RIGHT then
        state[S_ANGLE] = (state[S_ANGLE] + TURN_STEP) % ANGLE_COUNT
    elseif key == KEY_UP then
        move_forward(1)
    elseif key == KEY_DOWN then
        move_forward(-1)
    elseif key == KEY_SPACE then
        strafe(-1)
    elseif key == KEY_ENTER then
        strafe(1)
    end
end

-- ------------------------------------------------------------------ raycast
-- Cast one column.  The hit side is stored in state, since Lua functions can
-- return only one integer cell in this subset.
function cast_ray(x, w)
    local camera = x * (CAMERA_SCALE * 2) / w - CAMERA_SCALE
    local dir_x = cos_table[state[S_ANGLE]]
    local dir_y = sin_table[state[S_ANGLE]]
    local plane_x = -dir_y
    local plane_y = dir_x
    local ray_x = dir_x + plane_x * camera / CAMERA_SCALE
    local ray_y = dir_y + plane_y * camera / CAMERA_SCALE

    local ax = abs(ray_x)
    local ay = abs(ray_y)
    if ax < 1 then
        ax = 1
    end
    if ay < 1 then
        ay = 1
    end
    local delta_x = (POS_SCALE << POS_SHIFT) / ax
    local delta_y = (POS_SCALE << POS_SHIFT) / ay

    local map_x = state[S_X] >> POS_SHIFT
    local map_y = state[S_Y] >> POS_SHIFT
    local side_x = 0
    local side_y = 0
    local step_x = 1
    local step_y = 1
    if ray_x < 0 then
        side_x = (state[S_X] - (map_x << POS_SHIFT)) * delta_x / POS_SCALE
        step_x = -1
    else
        side_x = (((map_x + 1) << POS_SHIFT) - state[S_X]) * delta_x / POS_SCALE
        step_x = 1
    end
    if ray_y < 0 then
        side_y = (state[S_Y] - (map_y << POS_SHIFT)) * delta_y / POS_SCALE
        step_y = -1
    else
        side_y = (((map_y + 1) << POS_SHIFT) - state[S_Y]) * delta_y / POS_SCALE
        step_y = 1
    end

    local side = 0
    local steps = 0
    while steps < MAX_STEPS do
        if side_x < side_y then
            side_x = side_x + delta_x
            map_x = map_x + step_x
            side = 0
        else
            side_y = side_y + delta_y
            map_y = map_y + step_y
            side = 1
        end
        if cell_at(map_x, map_y) ~= 0 then
            break
        end
        steps = steps + 1
    end

    local dist = side_x - delta_x
    if side == 1 then
        dist = side_y - delta_y
    end
    if dist < POS_SCALE then
        dist = POS_SCALE
    end
    state[S_SIDE] = side
    return dist
end

-- ------------------------------------------------------------------- drawing
function shade_index(dist)
    local shade = (dist >> POS_SHIFT) - 1
    if shade < 0 then
        shade = 0
    end
    if shade > 15 then
        shade = 15
    end
    return shade
end

function draw_minimap()
    local mx = 0
    while mx < MAP_W do
        local my = 0
        while my < MAP_H do
            if cell_at(mx, my) ~= 0 then
                fill_rect(mx * 2, my * 2, 2, 2, C_MAP_WALL)
            end
            my = my + 1
        end
        mx = mx + 1
    end
    set_color(C_PLAYER)
    plot((state[S_X] >> POS_SHIFT) * 2, (state[S_Y] >> POS_SHIFT) * 2)
end

function draw_frame()
    local w = state[S_W]
    local h = state[S_H]

    set_color(C_CEIL)
    clear()
    set_color(C_FLOOR)
    local floor_y = h / 2
    while floor_y < h do
        hline(0, floor_y, w, C_FLOOR)
        floor_y = floor_y + 1
    end

    local x = 0
    while x < w do
        local dist = cast_ray(x, w)
        local line_h = (h << POS_SHIFT) / dist
        if line_h > h * 2 then
            line_h = h * 2
        end
        local y0 = (h - line_h) / 2
        if y0 < 0 then
            y0 = 0
        end
        local y1 = (h + line_h) / 2
        if y1 > h then
            y1 = h
        end

        local shade = shade_index(dist)
        local color = wall_colors[shade]
        if state[S_SIDE] == 1 then
            color = dark_colors[shade]
        end
        set_color(color)
        local y = y0
        while y < y1 do
            plot(x, y)
            y = y + 1
        end
        x = x + 1
    end

    set_color(C_CROSS)
    plot(w / 2, h / 2)
    plot(w / 2 - 1, h / 2)
    plot(w / 2 + 1, h / 2)
    plot(w / 2, h / 2 - 1)
    plot(w / 2, h / 2 + 1)
    draw_minimap()
    draw()
end

-- -------------------------------------------------------------------- start
print_str("RAY | arrows move and turn | space/enter strafe | esc quits")
if REPORT == 1 then
    print(state)
    print(map)
end

if ENABLE == 1 then
    local w, h = screen_size()
    state[S_W] = w
    state[S_H] = h
    state[S_X] = 384
    state[S_Y] = 384
    state[S_ANGLE] = 0
    state[S_SIDE] = 0
    state[S_FRAMES] = 0
    state[S_LAST] = 0
    state[S_QUIT] = 0

    draw_frame()

    while 1 == 1 do
        if quit_requested() == 1 then
            break
        end
        if state[S_QUIT] == 1 then
            break
        end

        local key = read_key()
        if key == 0 then
            local wait = FRAME_MS - (now_ms() - state[S_LAST])
            if wait > 0 then
                if wait > 50 then
                    wait = 50
                end
                sleep(wait)
                continue
            end
        end

        state[S_LAST] = now_ms()
        if key ~= 0 then
            handle_key(key)
            draw_frame()
        end
        if state[S_QUIT] == 1 then
            break
        end

        state[S_FRAMES] = state[S_FRAMES] + 1
        if MAX_FRAMES ~= 0 then
            if state[S_FRAMES] >= MAX_FRAMES then
                break
            end
        end
    end
end

print_str("RAYCASTER OK")
