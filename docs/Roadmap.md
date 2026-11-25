# 0xvm roadmap

This roadmap tracks the work after the v2 VM core and assembler were complete.
Each milestone is validated with `make test`, which builds the core C tests and
runs the CLI conformance transcript.

## Done

1. **Opcode and assembler cleanup — [done].**
   - The VM is a raw 16-bit word-addressed machine with 256 RAM pages and a
     fixed instruction layout.
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
   - Optional follow-up: System inspection ports for `sp`, `fp`, tick count,
     and top-of-stack for guests that want to inspect themselves.

3. **Prove the host split with a second host — [done].**
   - Shared host layer and headless dispatch tests.
   - SDL3 host (`0xvm-sdl`) exposes Screen and Controller ports plus a timer.
     It is detected via `pkg-config sdl3` and never becomes a core dependency.
   - Console text remains on terminal stdin/stdout in both hosts.
   - `examples/paint.asm` is an interactive demo of the SDL ports.

## Next

1. **Developer experience**
   - Keep the debugger and conformance suite aligned with new opcodes and
     assembler features.
   - Expand `stdlib/` with tested, source-level helpers as programs need them.
   - Consider a `0xdb` watch/list-expression command once source variables are
     represented in metadata.

2. **Host ports**
   - Add a device/browser host only behind the same `VmDevice` callback split.
   - Keep the core, CLI tools, and test suite free of host GUI dependencies.

3. **Language and tooling**
   - Consider typed source metadata for locals and expression watches.
   - Consider a lightweight package/import convention on top of `INCLUDE`.

## Non-goals for the near term

- No vector/SIMD opcodes; scalar untyped cells remain the model.
- No host filesystem device; file access is via host-specific ports or offline
  tooling.
- No ABI change to ROMs for debugger metadata.
- No mandatory dependency added to `lib0xvm`, `0xvm`, or the assembler.
