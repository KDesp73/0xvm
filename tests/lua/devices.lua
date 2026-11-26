-- SPDX-License-Identifier: MIT
-- Device intrinsic coverage for 0xluac. The CLI golden run keeps the device
-- branch dormant; the smoke build flips ENABLE to 1 for SDL3 and WASM.
const ENABLE = 0

print("DEVICE INTRINSICS")
if ENABLE == 1 then
    local w, h = screen_size()
    print(w)
    print(h)
    set_color(0xFFC040)
    plot(1, 2)
    draw()
    clear()
    print(read_key())
    print(quit_requested())
    print(now_ms() >= 0)
    sleep(1)
    print("DEVICE OK")
else
    print("DEVICE SKIPPED")
end
