# Hosts

Executable front ends live here. They link against the same `lib0xvm` core and
the shared tools/Console helpers in `apps/common/`; none of them add code to the
VM core or change the v2 ABI.

| Directory | Binary/assets | Devices |
| --- | --- | --- |
| `cli/` | `build/0xvm` | System, Console over terminal stdin/stdout |
| `sdl3/` | `build/0xvm-sdl` | System, Console, Screen, Controller, timer |
| `wasm/` | `build/0xvm-wasm.js`, `build/0xvm-wasm.wasm`, `build/0xvm-wasm.html` | System, Console, Screen, Controller, timer in a browser |

Build and run:

```sh
make                         # help
make all                     # build every available host
make sdl                     # build only the SDL3 host
make wasm                    # build only the browser host

./build/0xvm -f examples/gcd.asm
./build/0xvm-sdl -f examples/snake.asm
./build/0xvm-sdl -f examples/typography.asm

make wasm
python3 -m http.server 8000 --directory build
# open http://localhost:8000/0xvm-wasm.html
```

The browser page loads `snake.rom` by default and accepts `?rom=URL` or a local
ROM file. `make wasm` also writes `build/typography.rom`, so
`0xvm-wasm.html?rom=typography.rom` runs the graphical typography example. Its
Console is a text panel; Screen and Controller map to a canvas and keyboard
events. Both graphical hosts present a frame only on `VM_SCREEN_DRAW`; `CLEAR`
and `PIXEL` update an offscreen framebuffer.
