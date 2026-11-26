# 0xluac compiler design

`0xluac` compiles a deliberately small Lua subset to `0xasm` text. Emitting
assembly rather than a raw VMIM ROM keeps labels, source paths, and line
mapping in the assembler, so `0xdb` can inspect generated programs without a
new ROM metadata format.

This document tracks the work needed before a typography-level graphical demo
can be written in the subset. Each numbered item is implemented, tested, and
committed on its own before the next one starts.

## Current subset

- Integer literals and signed 64-bit arithmetic: `+`, `-`, `*`, `/`, `%`, unary `-`.
- Comparisons: `==`, `~=`, `<`, `<=`, `>`, `>=`.
- Locals and assignment: `local x = expr`, `x = expr`.
- Control flow: `if`/`else`, `while`, numeric `for` with an implicit step of `1`.
- Functions with parameters, locals, `return`, forward calls, and recursion.
- Static byte/word arrays with literal or dynamic indices.
- Typed heap allocations, raw pointer offsets, and explicit `free`.
- Bitwise `&`, `|`, `~` (XOR), unary `~` (NOT), `<<`, `>>`, and hex literals.
- Top-level `const` values and initialized byte/word/quad data tables.
- Output: `print(integer_expr)` and `print("string literal")`.
- Static string literals are emitted as `(address, length)` buffers.

Not supported yet: device I/O, string values, modules, or Lua source-line
mapping.

## Work items

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 1 | Functions with parameters, returns, and scopes | Done | `CALL`/`RET` ABI, arguments via `PICK`, caller cleanup, recursion. |
| 2 | Arrays/byte buffers and indexing | Done | `a[i]`, `LOAD8`/`STORE8`, `LOAD32`/`STORE32`. |
| 3 | Heap allocation and pointers | Done | `alloc_bytes`/`alloc_words`, pointer offsets, `FREE`. |
| 4 | Bitwise operators and hex literals | Done | `&`, `|`, binary `~`/unary `~`, `<<`, `>>`, `0x...`. |
| 5 | Static data declarations | Done | `const`, `bytes`/`words`/`quads` tables, `EQU`/`DB`/`DW`/`DQ`. |
| 6 | Device/runtime intrinsics | Pending | Screen, controller, timer, sleep port. |
| 7 | Strings as values and string library bindings | Pending | String variables, `STRLENZ`, `PRINT_STR`, indexing/slicing. |
| 8 | Modules/includes | Pending | Split source files sharing constants, data, and functions. |
| 9 | Source-line mapping, `break`/`continue`, `elseif` | Pending | Debuggability and cleaner control flow for larger programs. |

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

**Design.** Treat these names as intrinsics that emit the corresponding
`stdlib` macros plus any required `INCLUDE` directives. `sleep(ms)` maps to the
host-extension `DEO 0x41`.

**Acceptance.** A generated ROM runs under the SDL3 and WASM hosts and draws,
reads input, and yields with the same cadence as the assembly demo.

### 7. Strings as values and string library bindings

**Goal.** Store titles/status text, measure it, and print it from variables.

**Syntax sketch.**

```lua
local title = "0XVM TYPE"
print(title)
local n = strlen(title)
```

**Design.** Represent strings as `(addr, length)` pairs. Since VM locals are
single cells, string values need a small descriptor table or stack-pair
convention; this item chooses and implements that representation. Bind
`strlen`, `print_str`, `strcmp`, and slicing to `stdlib/strings.asm`.

**Acceptance.** String literals may be assigned, passed, stored, indexed, and
printed; golden output includes multi-line text.

### 8. Modules/includes

**Goal.** Split a large program into files like `examples/typography/`.

**Syntax sketch.**

```lua
import "font"
import "layout"
```

**Design.** Resolve imports relative to the source file, compile each module
once, and merge symbol tables. Emit one `INCLUDE` per generated assembly file
or concatenate modules into a single `.asm` while preserving label prefixes.
Avoid duplicate `ENTER` frames across module boundaries.

**Acceptance.** A multi-file program compiles and runs; duplicate or missing
imports are clear errors.

### 9. Source-line mapping, `break`/`continue`, `elseif`

**Goal.** Make large generated programs debuggable and control flow readable.

**Design.** Add `break`, `continue`, and `elseif` to the parser/codegen with
label stacks. For `0xdb`, emit a sidecar mapping from generated `.asm` lines to
`.lua` lines and teach the debugger to apply it, because VMIM ROMs carry no
metadata.

**Acceptance.** `0xdb` can step and set breakpoints in `.lua` terms; loops can
skip or exit without flag variables.

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
