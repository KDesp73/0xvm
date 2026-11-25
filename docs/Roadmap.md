# 0xvm roadmap

This roadmap tracks the work after the v2 VM core and assembler were complete.
Each milestone is validated with `make test`, which builds the core C tests,
runs the CLI conformance transcript, checks `0xdb`, and — when SDL3 is present —
checks the SDL host too.

## Done

1. **Opcode and assembler cleanup — [done].**
   - ABI v2 is frozen: 64-bit cells, 64 KiB byte-addressed RAM, 16-bit addresses,
     byte-encoded instructions, VMIM v2 ROMs, and a device page for all I/O.
   - `0xasm` supports the complete opcode set, expressions, conditionals,
     macros, and `INCLUDE`.
   - All readable pseudo-ops are opt-in macros from `stdlib/`. They are not VM
     opcodes and are not loaded implicitly.
   - `make test` runs the C unit tests and the golden CLI transcript.

2. **Finish the debugger — [done].**
   - `0xdb` provides `step`, `next`, `finish`, `until`, `continue`, breakpoints,
     stack/RAM/frame dumps, source listing, and scripted sessions.
   - Assembler symbols and source paths are exposed as source-level metadata;
     the ROM format is unchanged.
   - `apps/db/` is split into focused modules behind a shared `db.h`.
   - Optional follow-up: System inspection ports for `sp`, `fp`, tick count,
     and top-of-stack for guests that want to inspect themselves.

3. **Prove the host split with a second host — [done].**
   - Shared host layer and headless dispatch tests.
   - SDL3 host (`0xvm-sdl`) exposes Screen, Controller, and timer ports. It is
     detected via `pkg-config sdl3` and never becomes a core dependency.
   - Console text remains on terminal stdin/stdout in both hosts.
   - `examples/paint.asm` is an interactive demo of the SDL ports.

## Next

The toolchain is complete; the missing layer is software. The next milestone is
to make the platform pleasant to program and prove it with a real application.

1. **Grow the software layer — [next].**
   - Turn `stdlib/` into a tested runtime, still entirely opt-in source macros.
     Every addition gets a golden test under `tests/`.
     - Strings: length, compare, concat, substring, find, and copy, all based on
       the `(addr, len)` convention used by `PRINT_STR` / `READ_STR`. Numeric
       formatting stays in the host Console ports.
     - Collections: a dynamic array and a key/value table. With only 64 heap
       blocks, use chunked/arena allocation rather than one allocation per entry.
     - Allocator policy: arena/bump wrapper, explicit-free helper, and a
       documented refcount pattern.
     - Utilities: a deterministic RNG, min/max/clamp, and bit helpers.
   - Add one flagship app that exercises the whole stack at once. Snake on
     `0xvm-sdl` is the target: Screen, Controller, and timer ports, heap for the
     body, strings for the score, and RNG for food.
   - Keep the debugger and conformance suite aligned with each addition.

2. **Browser / WASM host — [after the software layer].**
   - Add a browser host behind the same `VmDevice` callback split so programs
     can be shared by URL.
   - Keep the core, CLI tools, and test suite free of host GUI dependencies.

3. **Lua-subset compiler — [after the runtime exists].**
   - Compile numbers, strings, tables, `if`/`while`/`for`, and plain functions
     to `0xasm` text.
   - Target the `stdlib/` runtime routines instead of emitting raw heap and
     memory manipulation.
   - Emit line/symbol metadata so `0xdb` steps through the source language.

4. **Richer debugger — [later].**
   - Watch/list expressions and conditional breakpoints once source-level
     variable metadata exists.
   - Consider typed source metadata for locals.
   - Consider a lightweight package/import convention on top of `INCLUDE`.

## Non-goals for the near term

- No new opcodes; ABI v2 is frozen and the current instruction set is sufficient.
- No vector/SIMD opcodes; scalar untyped cells remain the model.
- No host filesystem device; file access is via host-specific ports or offline
  tooling.
- No ABI change to ROMs for debugger metadata.
- No mandatory dependency added to `lib0xvm`, `0xvm`, or the assembler.
