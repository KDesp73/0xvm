# 0xvm Lua Subset

> Language guide for the `0xluac` compiler.

`0xluac` compiles a deliberately small Lua-like language to `0xasm` source. It
does not emit a ROM directly: the generated assembly is passed through `0xasm`,
which supplies labels, line data, and the VMIM ROM wrapper. This keeps the
generated program inspectable in `0xdb`.

For the compiler's development history, see [Compiler Design](Compiler-Design.md).
For the assembly output language, see [Assembly Language](Assembly-Language.md).
For the target machine, see the [Machine Contract](Machine-Contract.md).

## 1. Build pipeline

```sh
python3 -m luac compile program.lua -o build/program.asm -m build/program.lumap
./build/0xasm -f build/program.asm -o build/program.rom
./build/0xvm -f build/program.rom
```

The optional `-m/--map` file is a Lua source-line sidecar for `0xdb`.

## 2. Command line

```sh
python3 -m luac compile SOURCE -o OUTPUT [-m MAP]
0xluac compile SOURCE -o OUTPUT [-m MAP]
```

`0xluac` is the installed form of the same command; the name avoids a collision
with the Lua `luac` compiler.

| Flag | Meaning |
| --- | --- |
| `SOURCE` | input `.lua` file |
| `-o OUTPUT`, `--output OUTPUT` | required generated `0xasm` path |
| `-m MAP`, `--map MAP` | write a Lua source-line map |

Install the command with:

```sh
pip3 install -e .
# or
make luac-install
```

Compile errors are reported as `0xluac: line N: message`, or with a file name
when they occur inside an imported module.

## 3. Lexical rules

### Comments

```lua
-- A line comment runs to the end of the line.
local x = 1 -- trailing comment
```

### Identifiers and keywords

Identifiers start with a letter or `_` and continue with letters, digits, or
`_`. Reserved words are:

```
local print if then elseif else end while do for
function return break continue const import
```

There are no boolean literals. Comparisons produce the integers `1` and `0`.

### Literals

```lua
local dec = 42
local hex = 0xFFC040
local text = "hello\n"
local quoted = 'single quotes work too'
```

Integer literals are decimal or `0x` hexadecimal. String literals support
`\n`, `\r`, `\t`, `\\`, `\"`, `\'`, and `\xNN` escapes. Floating-point literals
are not part of the subset.

## 4. Values and storage

### Integers

All numeric values are signed 64-bit cells. Arithmetic follows the VM's
two's-complement wrapping rules. Comparison results are `0` or `1`.

### Static data

`const` declarations with `bytes`, `words`, or `quads` create global data labels
in the generated assembly. They are initialized at compile time and remain
writable VM memory at runtime.

```lua
const WIDTH = 320
const BASE = 0x100
const row = bytes { 0x80, 0x40, 0x20 }
const offsets = words { BASE, BASE + 2, -1 }
const glyphs = quads { 0x0102030405060708, -1 }

print(row[0])
print(offsets[1])
print(glyphs[0] >> 56)
```

| Constructor | Element width | Generated directive |
| --- | ---: | --- |
| `bytes { ... }` | 1 byte | `DB` |
| `words { ... }` | 4 bytes | `DW` |
| `quads { ... }` | 8 bytes | `DQ` |

Indexing uses element units: `words[1]` is the second 4-byte word, not byte
offset 1.

### Strings

A string value is an `(address, length)` pair. A string local occupies two VM
cells: the address of its bytes and its length.

```lua
local title = "0XVM TYPE"
print(title)               -- string + newline
print(strlen(title))       -- 8
print(title[0])            -- 48, the byte at index 0
```

String literals are interned into static data. String function parameters,
string returns, and general table values are not supported.

### Heap pointers

`alloc_bytes(n)` and `alloc_words(n)` allocate runtime buffers and return
pointers. Indexing is typed by the allocation; pointer arithmetic uses **byte**
offsets.

```lua
local text = alloc_bytes(4)
text[0] = 65
local tail = text + 2
tail[0] = 66
print(text[2])             -- 66
free(text)
```

`alloc_words(3)` reserves three 4-byte elements. `words + 8` points at the
third word because 8 is a byte offset.

`free` expects an allocated pointer variable, not an arbitrary address.

## 5. Declarations

### Locals

```lua
local x = 1
local w, h = screen_size()
x = x + 2
```

`local name = expr` declares a local. Multiple locals are only supported for
`local w, h = screen_size()`. Locals have function-wide scope: declarations in
nested blocks share the same function frame and are visible throughout that
function. They are initialized when execution reaches the declaration.

### Constants

```lua
const FRAME_MS = 66
const MASK = 0xFF
const COLOR = 0xFFC040
```

`const name = expr` evaluates a compile-time integer expression and emits an
`EQU` in the generated assembly. Constant expressions use the same operators as
normal expressions but may only reference constants and literals.

`const name = bytes/words/quads { ... }` creates a static data label rather than
a scalar constant.

### Functions

```lua
function add(a, b)
    return a + b
end

print(add(2, 3))
```

Functions are top-level only. They may call functions defined later in the
file, call themselves recursively, and be used as expressions or statements. A
function with no explicit `return` returns `0`.

### Imports

```lua
import "modules/main"
```

Imports are resolved relative to the importing file. The `.lua` suffix is added
when omitted. Each file is expanded once; duplicate and circular imports are
errors. Constants, data labels, functions, and top-level statements from the
imported file are merged into one generated program.

## 6. Expressions

### Operators

From lowest to highest precedence:

| Level | Operators | Meaning |
| --- | --- | --- |
| comparison | `== ~= < <= > >=` | integer comparisons |
| bitwise OR | `|` | bitwise OR |
| bitwise XOR | `~` | bitwise XOR |
| bitwise AND | `&` | bitwise AND |
| shift | `<< >>` | left / right shift |
| additive | `+ -` | addition / subtraction |
| multiplicative | `* / %` | multiplication / division / remainder |
| unary | `- ~` | negation / bitwise complement |
| primary | `expr`, `name(args)`, `value[index]`, `(expr)` | grouping, calls, indexing |

Parentheses may be used to override precedence. Boolean `and`, `or`, `not`,
concatenation, exponentiation, varargs, and method calls are not supported.
Arithmetic on a string is a compile error.

### Data constructors

`bytes { ... }`, `words { ... }`, and `quads { ... }` may appear where an
expression is expected. Values must be compile-time integer expressions.

### Indexing

```lua
local byte = row[i]
row[i] = byte + 1
```

Indexing works on static arrays, heap pointers, and strings. Static array and
string indices are in element units; heap pointer indices are in allocation
element units. No runtime bounds checks are inserted, so invalid indices read
or write adjacent VM memory.

## 7. Statements

### Assignment

```lua
x = expr
row[0] = expr
pointer[i] = expr
```

Both simple and indexed assignment are supported.

### Output

```lua
print(42)          -- signed decimal + newline
print("hello")     -- string bytes + newline
print_str(text)    -- string bytes + newline
```

`print` selects the Console integer or string printer based on the expression.

### Conditionals

```lua
if x == 0 then
    print("zero")
elseif x < 0 then
    print("negative")
else
    print("positive")
end
```

`elseif` and `else` are optional. Conditions are integer expressions; `0` is
false and any non-zero value is true.

### Loops

```lua
local i = 0
while i < 10 do
    i = i + 1
    if i == 3 then
        continue
    end
    if i == 8 then
        break
    end
end

for j = 1, 3 do
    print(j)
end
```

`while` runs until its condition is zero. `for name = start, limit do` counts
from `start` to `limit` inclusive with an implicit step of `1`; `limit` is
evaluated once. `break` and `continue` are valid only inside loops.

### Calls

```lua
add(1, 2)          -- statement call; result discarded
local sum = add(1, 2)
```

Calls to unknown functions, or calls with the wrong argument count, are compile
errors.

### Return

`return expr` is valid only inside a function. A bare `return` returns `0`.

## 8. Function ABI and scope

Generated functions compile to `CALL`/`RET` frames:

- Arguments are copied into zeroed locals at function entry.
- All locals collected anywhere in the function body share one `ENTER` frame.
- The result is the value left on the stack for `RET`.
- Functions always leave one result cell, defaulting to `0`.
- Nested functions and closures are not supported.

Function names are global within the merged program. Imported functions and
top-level functions share the same namespace.

## 9. String bindings

| Binding | Arguments | Result / effect |
| --- | --- | --- |
| `print_str(s)` | string | write string + newline |
| `strlen(s)` | string | byte length |
| `strcmp(a, b)` | string, string | `-1`, `0`, or `1` |
| `slice(s, start, count)` | string, integer, integer | substring `(addr, len)` |
| `strcpy(dest, s)` | pointer, string | copy bytes into `dest` |
| `strfind(s, needle)` | string, string | first index or `-1` |
| `strchr(s, byte)` | string, integer | first index of byte or `-1` |

`slice` requires compile-time, non-negative `start` and `count` values. It
clamps the requested range to the string; a start beyond the end yields an
empty string. `strchr` searches for a byte value, not a one-character string.

```lua
local part = slice("0XVM TYPE", 5, 4)
print(part)
print(strlen(part))
```

String bindings automatically include `stdlib/strings.asm` in the generated
assembly. String function parameters and string returns are not supported, so
pass labels or indices when sharing text across functions.

## 10. Device intrinsics

| Intrinsic | Arguments | Result / effect | Stdlib include |
| --- | ---: | --- | --- |
| `screen_size()` | 0 | width, height (multi-local only) | `screen.asm` |
| `set_color(rgb)` | 1 | set current colour | `screen.asm` |
| `plot(x, y)` | 2 | write one pixel | `screen.asm` |
| `draw()` | 0 | present the frame | `screen.asm` |
| `clear()` | 0 | fill with current colour | `screen.asm` |
| `read_key()` | 0 | next Controller code, `0` if none | `controller.asm` |
| `quit_requested()` | 0 | `1` after close request | `controller.asm` |
| `now_ms()` | 0 | milliseconds since host start | `timer.asm` |
| `sleep(ms)` | 1 | host sleep / wait request | direct `DEO 0x41` |

```lua
local w, h = screen_size()
set_color(0xB9E5AD)
plot(10, 10)
draw()

local key = read_key()
if quit_requested() == 1 then
    print("bye")
elseif key == 7 then
    print("escape")
end
```

Boolean `or` is not supported; use nested `if` or `elseif` instead.

Screen, Controller, and timer devices depend on host capabilities. A CLI host
provides Console only. Generated programs that call these intrinsics will run
in CLI hosts only if the device branch is kept dormant, as in
`tests/lua/devices.lua`.

## 11. Generated assembly conventions

`0xluac` emits ordinary assembly with:

- `INCLUDE "console.asm"` always.
- `INCLUDE "screen.asm"`, `controller.asm`, `timer.asm`, or `strings.asm` when
  the corresponding intrinsics or bindings are used.
- `EQU` lines for scalar constants.
- Static `DB`/`DW`/`DQ` labels for data tables and interned strings.
- One `ENTER n` frame for the top-level program and one per function.
- A final `HALT` after top-level statements.
- Function labels emitted after main, allowing forward calls.

The output is intended to be readable and debuggable, not minified.

## 12. Source maps and debugging

```sh
python3 -m luac compile examples/typography.lua \
  -o build/lua-typography.asm -m build/lua-typography.lumap
./build/0xasm -f build/lua-typography.asm -o build/lua-typography.rom
./build/0xdb -f build/lua-typography.rom \
  -s build/lua-typography.asm -m build/lua-typography.lumap
```

The map is a tab-separated sidecar beginning with `# luac source map v1`. Each
entry maps a generated assembly line to a Lua file and line. `0xdb` overlays it
on the assembler's line data, so stepping, listing, and line breakpoints can use
Lua source lines.

## 13. Limitations

- No general tables, metatables, or userdata.
- No floating-point literals or arithmetic.
- No boolean operators or boolean literals.
- No string function parameters or string returns.
- No nested functions or closures.
- No generic `for`, `repeat`, `goto`, or multiple assignment except
  `screen_size()`.
- No runtime bounds checking on arrays or pointers.
- `slice` offsets must be compile-time non-negative integers.
- Device intrinsics require a host that provides the corresponding device.

These limits are deliberate: the subset targets a small stack VM with fixed
memory and a transparent assembly output.

## 14. Examples and tests

| File | Demonstrates |
| --- | --- |
| `examples/typography.lua` | full graphical text editor with data tables and intrinsics |
| `examples/raycaster.lua` | first-person maze explorer with DDA raycasting |
| `tests/lua/smoke.lua` | basic statements, loops, and output |
| `tests/lua/functions.lua` | forward calls, recursion, statement calls, locals |
| `tests/lua/control.lua` | `if`/`elseif`, `while`, `for`, `break`, `continue` |
| `tests/lua/arrays.lua` | byte/word storage and dynamic indices |
| `tests/lua/heap.lua` | allocation, typed pointer offsets, `free` |
| `tests/lua/data.lua` | constants, `bytes`/`words`/`quads`, big-endian layout |
| `tests/lua/bitops.lua` | hex literals, shifts, AND/OR/XOR, unary NOT |
| `tests/lua/strings.lua` | string values, indexing, slicing, string bindings |
| `tests/lua/devices.lua` | device intrinsics with a dormant CLI branch |
| `tests/lua/modules.lua` | imports and one-time module expansion |

Run the golden checks with:

```sh
make lua-check
```
