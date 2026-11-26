# 0xluac compiler design

`0xluac` compiles a deliberately small Lua subset to `0xasm` text. Emitting
assembly rather than a raw VMIM ROM keeps labels, source paths, and line
mapping in the assembler, so `0xdb` can inspect generated programs without a
new ROM metadata format.

This document tracks the work that made a typography-level graphical demo
possible in the subset. Each numbered item was implemented, tested, and
committed on its own before the next one started.

## Current subset

- Integer literals and signed 64-bit arithmetic: `+`, `-`, `*`, `/`, `%`, unary `-`.
- Comparisons: `==`, `~=`, `<`, `<=`, `>`, `>=`.
- Locals and assignment: `local x = expr`, `local w, h = screen_size()`,
  `x = expr`.
- Control flow: `if`/`elseif`/`else`, `while`, numeric `for` with an implicit
  step of `1`, plus `break` and `continue` inside loops.
- Functions with parameters, locals, `return`, forward calls, and recursion.
- Static byte/word arrays with literal or dynamic indices.
- Typed heap allocations, raw pointer offsets, and explicit `free`.
- Bitwise `&`, `|`, `~` (XOR), unary `~` (NOT), `<<`, `>>`, and hex literals.
- Top-level `const` values and initialized byte/word/quad data tables.
- Device intrinsics: `screen_size`, `set_color`, `plot`, `draw`, `clear`,
  `read_key`, `quit_requested`, `now_ms`, and `sleep`.
- String locals hold `(address, length)` pairs; literals, string variables,
  `slice`, byte indexing, and the `strlen`, `print_str`, `strcmp`, `strfind`,
  `strchr`, and `strcpy` bindings are supported.
- Output: `print(integer_expr)` and `print(string_expr)`.
- Static string literals are emitted as `(address, length)` buffers.
- Top-level `import "name"` loads `name.lua` relative to the importing file and
  merges constants, data, functions, and top-level statements into one program.
- `-m/--map` writes a Lua source-line sidecar; `0xdb -m` overlays it on the
  assembler's line data for stepping, listing, and line breakpoints.

Not supported yet: string function parameters/returns or table values.

## Work items

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 1 | Functions with parameters, returns, and scopes | Done | `CALL`/`RET` ABI, arguments via `PICK`, caller cleanup, recursion. |
| 2 | Arrays/byte buffers and indexing | Done | `a[i]`, `LOAD8`/`STORE8`, `LOAD32`/`STORE32`. |
| 3 | Heap allocation and pointers | Done | `alloc_bytes`/`alloc_words`, pointer offsets, `FREE`. |
| 4 | Bitwise operators and hex literals | Done | `&`, `|`, binary `~`/unary `~`, `<<`, `>>`, `0x...`. |
| 5 | Static data declarations | Done | `const`, `bytes`/`words`/`quads` tables, `EQU`/`DB`/`DW`/`DQ`. |
| 6 | Device/runtime intrinsics | Done | Screen, controller, timer macros; `DEO 0x41` sleep; SDL3/WASM smoke. |
| 7 | Strings as values and string library bindings | Done | `(addr, len)` locals, stdlib bindings, read-only indexing, clamped slicing. |
| 8 | Modules/includes | Done | `import`, relative resolution, one-time expansion, merged symbols. |
| 9 | Source-line mapping, `break`/`continue`, `elseif` | Done | Sidecar line map for `0xdb`; loop jumps and chained conditions. |

### 1. Functions with parameters, returns, and scopes

**Goal.** Allow a program to define reusable functions and call them from
expressions or as statements, with correct stack behavior.

**Syntax sketch.**

```lua
function add(a, b)
    return a + b
end

print(add(2, 3))   -- expression call
add(1, 2)          -- statement call; result discarded
```

**Design.**

- Each function gets a label, its own local frame, and its own scope.
- Arguments arrive on the stack at `fp`. `ENTER n` reserves argument and body
  locals; `PICK (n + argc - i)` copies argument `i` into local slot `i - 1`.
- `RET` requires exactly one result. A missing `return` emits `PUSH 0; RET`.
- After an expression call, the caller removes the arguments with `SWAP; POP`
  repeated `argc` times. After a statement call, it pops `argc + 1` values.
- Calls may be forward references, enabling recursion.

**Acceptance.**

- Zero-argument, multi-argument, nested, and recursive calls work.
- Function locals do not collide with top-level or other functions' locals.
- Unknown functions, wrong arity, nested definitions, and `return` outside a
  function are compile errors.
- A golden program exercises calls as expressions and statements.

### 2. Arrays/byte buffers and indexing

**Goal.** Represent the editor text buffer and bitmap font rows as indexable
byte arrays.

**Syntax sketch.**

```lua
local text = byte_array(16)
text[0] = 65
print(text[0])
```

**Design.** Add `Index` and array-valued `Local` expressions, then emit
`LOAD8`/`STORE8` for byte arrays and `LOAD32`/`STORE32` for word arrays. Bounds
checks are compile-time only for static sizes; dynamic values remain unchecked
unless a runtime helper is added later.

**Acceptance.** Read/write byte and word arrays; indexing works in expressions
and assignments; out-of-range static indices are rejected.

### 3. Heap allocation and pointers

**Goal.** Allocate the 1024-byte text buffer and derive offsets for helpers.

**Syntax sketch.**

```lua
local text = alloc_bytes(1024)
text[caret] = byte
local tail = text + 16
tail[0] = 10
free(text)
```

**Design.** `alloc_bytes(n)` emits `ALLOC` with `n` bytes; `alloc_words(n)`
emits `ALLOC` with `4 * n` bytes. Both return an ordinary 64-bit address that
can be stored in a local, indexed with the same `LOAD8`/`STORE8` and
`LOAD32`/`STORE32` paths as static arrays, and combined with `+`/`-`. Pointer
arithmetic is a raw byte offset, so `words + 4` addresses the second word.
The compiler tracks literal offsets for compile-time bounds checks; runtime
offsets and sizes are unchecked and fail through the VM's own address checks.
`free(ptr)` maps to `FREE` and invalidates the pointer local. Allocation
failures remain a runtime error for a later error-handling pass.

**Acceptance.** Byte and word buffers can be allocated with literal or runtime
sizes, indexed through derived pointers, and freed. Out-of-range literal
indices and releases of non-pointer expressions are compile errors.

### 4. Bitwise operators and hex literals

**Goal.** Support pixel packing (`x << 16 | y`), caret blink (`& 1`), and color
constants.

**Syntax sketch.**

```lua
local packed = x << 16 | y
local color = 0xFFC040
```

**Design.** The lexer accepts `<<`, `>>`, `&`, `|`, `~`, and `0x`/`0X` literals.
Hex values become normal integer tokens and are emitted as decimal VM
immediates. Precedence follows Lua: unary `-`/`~`, multiplicative, additive,
shifts, `&`, binary `~`, `|`, then comparisons. Binary `~` maps to `XOR`, while
unary `~` maps to `NOT`; `~=` remains the inequality comparison. Logical
`and`/`or`/`not` remain a separate decision.

**Acceptance.** Shifts, AND/OR/XOR, unary NOT, and hex literals produce the
same values as the VM conformance program, and precedence relative to
arithmetic and comparisons matches Lua.

### 5. Static data declarations

**Goal.** Emit the bitmap font and compile-time constants without raw assembly.

**Syntax sketch.**

```lua
const FRAME_MS = 66
const BASE = 0x100
const glyphs = bytes { 0x88, 0x88, 0xC8, 0xB0 }
const offsets = words { BASE, BASE + 2 }
const rows = quads { 0x0102030405060708, -1 }
```

**Design.** Top-level constants are folded with the VM's 64-bit arithmetic and
emitted as `EQU` directives. Initialized tables get labels and are emitted with
`DB`, `DW`, or `DQ`. Because the assembler's `DW` is a 16-bit directive while
`LOAD32` reads four bytes, each compiler word becomes two `DW` halves in
big-endian order; quadwords use `DQ`. Strings continue to use `STR` labels.
Const table names behave like array pointers, support `name[i]`, and carry the
same compile-time bounds checks. Declarations must appear before use, and
constants cannot be reassigned. Data is emitted after code so the assembler's
entry `JMP` still skips it.

**Acceptance.** Constants are usable in expressions and data initializers;
byte, word, and quadword tables are emitted with correct big-endian layout;
labels are referenceable and indexable.

### 6. Device/runtime intrinsics

**Goal.** Expose the stdlib device macros to generated programs.

**Syntax sketch.**

```lua
local w, h = screen_size()
set_color(0x101018)
clear()
plot(x, y)
draw()
local key = read_key()
if quit_requested() then return end
local now = now_ms()
sleep(20)
```

**Design.** The intrinsic names are reserved and cannot be redefined as Lua
functions. Each call emits the matching `stdlib` macro and pulls in only the
`screen.asm`, `controller.asm`, or `timer.asm` file that defines it.
`screen_size()` is the one two-result intrinsic and requires
`local w, h = screen_size()`; codegen emits `SCREEN_SIZE` and assigns the locals
in reverse stack order. `plot(x, y)` packs `(x << 16) | y` before calling
`PLOT`. Void intrinsics are statements, while `read_key()`, `quit_requested()`,
and `now_ms()` fold into integer expressions. `sleep(ms)` emits
`DEO 0x41`, the SDL3/WASM host-extension port used by the typography demo.

**Acceptance.** The CLI golden fixture keeps the device branch dormant so its
transcript stays deterministic; a smoke build flips `ENABLE` to `1` and runs the
same generated ROM under SDL3 and WASM. It prints the 320x240 framebuffer size,
reads an idle controller, checks the timer, draws/clears, and yields through
`sleep` before halting.

### 7. Strings as values and string library bindings

**Goal.** Store titles/status text, measure it, and print it from variables.

**Syntax sketch.**

```lua
local title = "0XVM TYPE"
print(title)
local n = strlen(title)
```

**Design.** A string local reserves two VM cells: `addr` then `length`. Every
string expression pushes that pair in the same order, and assignment writes the
length slot before the address slot. Literals and string locals can be passed to
`strlen`, `print_str`, `strcmp`, `strfind`, `strchr`, and `strcpy`; only the
bindings that need runtime helpers include `stdlib/strings.asm`. `slice(s,
start, count)` is compiled inline with constant offsets and emits a new pair
clamped to the source bounds. Byte indexing reads one byte from `addr + i` and
is read-only; string values cannot be used in integer arithmetic or returned
from functions.

**Acceptance.** The golden fixture assigns literals to string locals, prints
them (including embedded newlines), measures and compares them, indexes and
searches them, slices to a shorter string, copies a slice into heap memory, and
frees that buffer.

### 8. Modules/includes

**Goal.** Split a large program into files like `examples/typography/`.

**Syntax sketch.**

```lua
import "font"
import "layout"
```

**Design.** `import "name"` is a top-level statement; the loader resolves
`name.lua` relative to the importing file and splices the module's statements at
the import point. Imports may nest, but each file may be imported only once and
cycles are rejected. Constants, data tables, functions, and top-level locals
share one symbol table, so modules can call each other's functions and use each
other's constants. Everything is concatenated into one `.asm` with globally
unique generated labels, so only the root program emits the main `ENTER` frame;
each Lua function keeps its own frame. `compile_file` performs expansion, while
the string-based `compile_source` helper rejects unresolved imports.

**Acceptance.** A multi-file golden program imports a nested module, uses its
constants, data, functions, and text, and runs through the CLI host. Missing,
duplicate, and circular imports produce clear compile errors.

### 9. Source-line mapping, `break`/`continue`, `elseif`

**Goal.** Make large generated programs debuggable and control flow readable.

**Design.** `elseif` is parsed as nested `If` nodes sharing one `end`, and
`break`/`continue` jump through a codegen loop-label stack. `continue` jumps to
the condition label in `while` loops and to the increment label in numeric
`for` loops. Each AST node carries its source line and file; emitted assembly
lines are recorded in a `.lumap` sidecar written by `0xluac -m`. `0xdb -m` loads
that map and overlays it on assembler line metadata, so `print_current`, `list`,
and line breakpoints can use Lua file/line references. Fallback behavior for
assembly sources or ROMs without a map is unchanged.

**Acceptance.** A golden program uses `elseif`, `break`, and `continue`; a
scripted `0xdb` session sets a breakpoint with `control.lua:9` and steps with
Lua line display. Missing loop jumps and missing source metadata remain clear
compile errors.

## Workflow

1. Update the status and notes in this document for the chosen item.
2. Implement the item in `luac/`.
3. Add/extend unit checks in `tests/test_luac.py` and a golden program under
   `tests/lua/`.
4. Run `make lua-check` and `make test`.
5. Commit only that item with a focused message, then start the next item.

## Progress log

- **Baseline** — initial compiler package and `0xluac` CLI committed as
  `ec42e21`.
- **Item 1** — functions with parameters, returns, scopes, forward calls, and
  recursion; golden fixture `tests/lua/functions.lua`.
- **Item 2** — static byte/word arrays, indexing, and compile-time bounds
  checks; golden fixture `tests/lua/arrays.lua`.
- **Item 3** — typed heap allocations, pointer offsets, runtime sizes, and
  `free`; golden fixture `tests/lua/heap.lua`.
- **Item 4** — shifts, bitwise AND/OR/XOR/NOT, hex literals, and Lua
  precedence; golden fixture `tests/lua/bitops.lua`.
- **Item 5** — `EQU` constants and initialized byte/word/quad data tables;
  golden fixture `tests/lua/data.lua`. Also corrected static word arrays to
  reserve four bytes per element.
- **Item 6** — screen/controller/timer intrinsics and `sleep`; golden fixture
  `tests/lua/devices.lua` plus SDL3/WASM device smoke runs.
- **Item 7** — `(addr, len)` string locals, stdlib bindings, read-only indexing,
  and clamped slicing; golden fixture `tests/lua/strings.lua`.
- **Item 8** — top-level `import`, relative module resolution, one-time
  expansion, and merged symbol tables; golden fixture `tests/lua/modules.lua`.
- **Item 9** — `elseif`, `break`/`continue`, AST source metadata, `.lumap`
  sidecars, and `0xdb` Lua line breakpoints; golden fixture
  `tests/lua/control.lua`.
- **Capstone** — `examples/typography.lua`, a Lua-source graphical text editor
  using the font table, layout helpers, caret editing, blink, and timed loop.
  Its `ENABLE` constant is flipped for the CLI golden run, and bounded smoke
  builds run under SDL3 and WASM.

## Capstone demo

`examples/typography.lua` is the typography-level program the work items were
building toward. It keeps editor state and the text buffer in `const` data
tables, which are writable global VM labels because functions cannot share
top-level locals in the current ABI. Caption byte tables replace string
arguments, since string values are two-cell `(address, length)` pairs and are
not valid function parameters yet. The program still uses string literals for
the intro and completion lines.

`make lua-typography-check` runs a CLI-safe build (`ENABLE = 0`), then a bounded
SDL3 smoke build (`FRAME_MS = 1`, `MAX_FRAMES = 3`) that draws the full editor.
`make wasm` includes the interactive ROM. `make wasm-check` builds it again with
`REPORT = 1` so the script can read the state and text labels, then exercises
insert, caret movement, backspace, delete, and exit in the browser host. The
assembly demo remains unchanged, so its browser editing checks continue to
cover the original app too.
