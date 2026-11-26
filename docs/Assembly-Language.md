# 0xvm Assembly Language

> Language reference for VMIM v2 assembly source.

This document describes the source language accepted by `0xasm`. For the frozen
binary contract, see the [Machine Contract](Machine-Contract.md). For encoder and
interpreter implementation details, see [VM Implementation](VM-Implementation.md).

## 1. Program model

A program is written as one or more assembly source files, assembled into a VMIM
ROM, then loaded by a host.

```sh
./build/0xasm -f program.asm -o program.rom
./build/0xvm -f program.rom
```

The machine model relevant to source code is:

- A **cell** is a signed 64-bit integer. Arithmetic is two's-complement wrapping.
- A **byte address** is 16 bits. `ip` and all jump/call targets are absolute byte
  addresses, not instruction indices.
- RAM is 64 KiB. The ROM image is loaded at `0x0100`; static data and code live
  in the image. The heap starts at `0x4000`.
- The operand stack, call frames, and allocator metadata live outside
  addressable RAM. Only `mem` is reachable by `LOAD*`/`STORE*`.
- I/O is not part of the instruction set. `DEI` and `DEO` access a 256-entry
  device page supplied by the host.
- The assembler has **no built-in pseudo-ops**. Readable aliases such as `INC`,
  `PRINT`, and `SET_COLOR` are opt-in macros from `stdlib/`.

## 2. Lexical structure

### Statements

- One instruction or directive per line.
- Mnemonics, directive names, and macro names are case-insensitive.
- Labels have the form `name:`; names start with a letter or `_` and continue
  with letters, digits, or `_`.
- A label may share a line with an instruction or data directive.

```asm
loop:
    PUSH 1
    ADD
    JMP loop
```

### Comments

Comments start with `;`, `#`, or `//` and run to the end of the line. Comment
markers inside quoted strings are literal text.

```asm
PUSH 42       ; decimal
PUSH 0x2A     # hex
STR "a//b"    // the slashes are part of the string
```

### Operands

Instruction operand kinds are:

| Kind | Used by | Example |
| --- | --- | --- |
| none | `HALT`, `DUP`, `RET`, ... | `HALT` |
| integer expression | `PUSH`, `ENTER`, `LOCAL_GET/SET`, data directives | `PUSH N*2` |
| float literal | `PUSHF` | `PUSHF 1.5` |
| jump target | `JMP`, `JMP_Z`, `CALL` | `JMP done` |
| device port | `DEI`, `DEO` | `DEO CONSOLE_PUTC` |

Missing or illegal operands are reported with source line numbers. `DEI` and
`DEO` ports must be expressions in `0..255`.

## 3. Expressions and constants

Integer expressions are accepted by `PUSH`, `ENTER`, `LOCAL_GET`, `LOCAL_SET`,
`CALL`, jump targets, `DEI`/`DEO` ports, and numeric data directives.

| Form | Examples |
| --- | --- |
| Decimal | `42`, `-7` |
| Hexadecimal | `0x2A`, `0xFFFF` |
| Named constant | `N`, `VM_IMAGE_START` |
| Parentheses | `(N + 1) * 2` |
| Unary operators | `+x`, `-x`, `~x` |
| Binary operators | `+ - * / % << >> & ^ | == != < <= > >=` |

Operator precedence is C-like. Arithmetic uses `uint64_t` wrapping; division by
zero is an assembler error; shifts of 64 or more yield zero. The literal
`-9223372036854775808` is accepted as `INT64_MIN`.

Define constants with `EQU`:

```asm
EQU WIDTH 320
EQU HEIGHT 200
EQU CELL_BYTES WIDTH * HEIGHT
```

Constants are evaluated assembly-time only. The same expression grammar is used
by `IF` and `ELIF`, except those branches may reference constants only.

`PUSHF` takes a floating-point literal and pushes its raw IEEE-754 bit pattern
as a cell. The VM has no type tags, so the same cell can be interpreted as an
integer or a double.

## 4. Instruction set

Stack effects are written bottom-to-top, so the rightmost value is the top. For
binary operations, `b` is popped first and `a` is the value below it.

### 4.1 Core and control flow

| Instruction | Effect | Description |
| --- | --- | --- |
| `HALT` | `[...] → [...]` | Stop with `VM_HALTED`; `ip` stays on the opcode |
| `NOP` | `[...] → [...]` | Advance `ip` and `ticks` |
| `PUSH x` | `[...] → [..., x]` | Push a big-endian `int64` immediate |
| `POP` | `[..., x] → [...]` | Discard the top |
| `DUP` | `[..., x] → [..., x, x]` | Duplicate the top |
| `ADD` | `[..., a, b] → [..., a+b]` | Wrapping 64-bit addition |
| `SUB` | `[..., a, b] → [..., a-b]` | Wrapping 64-bit subtraction |
| `MUL` | `[..., a, b] → [..., a*b]` | Wrapping 64-bit multiplication |
| `CMP_LT` | `[..., a, b] → [..., 0\|1]` | Signed less-than |
| `JMP target` | `[...] → [...]` | Set `ip` to an absolute byte address |
| `JMP_Z target` | `[..., c] → [...]` | Pop `c`; jump if zero |
| `JMP_I` | `[..., target] → [...]` | Pop target and jump |

Comparisons and logical operations push `1` or `0`, so they feed `JMP_Z`
directly.

### 4.2 Arithmetic, comparison, bitwise, and logical

| Instruction | Effect | Description |
| --- | --- | --- |
| `DIV` | `[..., a, b] → [..., a/b]` | Truncates toward zero; `INT64_MIN / -1` wraps |
| `MOD` | `[..., a, b] → [..., a%b]` | Sign of `a`; `INT64_MIN % -1` is `0` |
| `CMP_EQ` / `CMP_NE` | `[..., a, b] → [..., 0\|1]` | Equality / inequality |
| `CMP_LE` / `CMP_GT` / `CMP_GE` | `[..., a, b] → [..., 0\|1]` | Signed comparisons |
| `AND` / `OR` / `XOR` | `[..., a, b] → [..., a op b]` | 64-bit bitwise operations |
| `NOT` | `[..., a] → [..., ~a]` | Bitwise complement |
| `SHL` / `SHR` | `[..., a, b] → [..., a shift b]` | Count masked to `0..63`; `SHR` is logical |
| `LNOT` | `[..., a] → [..., !a]` | Logical NOT of a booleanized operand |
| `LAND` / `LOR` | `[..., a, b] → [..., a && b \| a \|\| b]` | Logical AND / OR |

A zero divisor returns `VM_ERR_DIV_ZERO` without changing the stack.

### 4.3 Stack shuffling

| Instruction | Effect | Description |
| --- | --- | --- |
| `SWAP` | `[..., a, b] → [..., b, a]` | Exchange the top two |
| `OVER` | `[..., a, b] → [..., a, b, a]` | Copy the second item to the top |
| `ROT` | `[..., a, b, c] → [..., b, c, a]` | Rotate the top three |
| `PICK` | `[..., x, n] → [..., x_n]` | Pop `n`, copy `stack[sp-2-n]`; `n=0` duplicates the new top |

### 4.4 RAM and heap

| Instruction | Effect | Description |
| --- | --- | --- |
| `LOAD8` / `LOAD16` | `[..., addr] → [..., value]` | Unsigned byte / 16-bit read |
| `LOAD32` | `[..., addr] → [..., value]` | Signed 32-bit read, sign-extended |
| `LOAD32U` | `[..., addr] → [..., value]` | Unsigned 32-bit read, zero-extended |
| `LOAD64` | `[..., addr] → [..., bits]` | Raw 64-bit read |
| `STORE8` / `STORE16` / `STORE32` / `STORE64` | `[..., val, addr] → [...]` | Write 1/2/4/8 bytes; **address is on top** |
| `ALLOC` | `[..., size] → [..., addr]` | Allocate a heap block, reusing free blocks |
| `FREE` | `[..., addr] → [...]` | Mark a live block dead for reuse |

`ALLOC` requires a positive size, rounds up to a multiple of 8, and returns an
address in the heap region. `FREE` requires a live block's start address.

### 4.5 Procedures and locals

| Instruction | Operand | Effect | Description |
| --- | --- | --- | --- |
| `CALL` | target | `[...] → [...]` | Push `{return address, fp}`, set `fp = sp`, jump |
| `CALL_I` | — | `[..., target] → [...]` | Pop target, then call |
| `RET` | — | `[..., result] → [..., result]` | Unwind one frame, leave result for caller |
| `ENTER n` | `0..255` | `[...] → [..., 0 × n]` | Reserve `n` zeroed locals at `[fp, fp+n)` |
| `LOCAL_GET i` | `0..255` | `[...] → [..., local]` | Push `stack[fp+i]` |
| `LOCAL_SET i` | `0..255` | `[..., v] → [...]` | Pop `v` into `stack[fp+i]` |

Locals share the 256-cell operand stack with ordinary pushes. A typical
procedure body starts with `ENTER n`, then uses `LOCAL_SET`/`LOCAL_GET`. `RET`
requires a result on the stack.

### 4.6 Floats

| Instruction | Effect | Description |
| --- | --- | --- |
| `PUSHF x` | `[...] → [..., bits]` | Push a raw 64-bit double literal |
| `FADD` / `FSUB` / `FMUL` / `FDIV` | `[..., a, b] → [..., bits]` | Double-precision arithmetic |
| `FNEG` / `FABS` / `FSQRT` | `[..., a] → [..., bits]` | Unary double operations |
| `FCMP_LT` / `FCMP_EQ` / `FCMP_LE` / `FCMP_GT` / `FCMP_GE` | `[..., a, b] → [..., 0\|1]` | Double comparisons |
| `FTOI` | `[..., bits] → [..., int]` | Truncating conversion; NaN/overflow saturate |
| `ITOF` | `[..., int] → [..., bits]` | `int64_t` to double |

### 4.7 Device access

| Instruction | Effect | Description |
| --- | --- | --- |
| `DEI port` | `[...] → [..., value]` | Read a cell from the host device |
| `DEO port` | `[..., value] → [...]` | Pass the top cell to the host device |

Port constants are supplied by `stdlib/ports.asm`. System and Console ports are
available on every host; Screen, Controller, and timer ports depend on host
capabilities. See [Devices](#8-devices-and-host-capabilities) below.

### 4.8 Encoding summary

| Immediate | Instructions | Total bytes | Encoding |
| --- | --- | ---: | --- |
| none | most opcodes | 1 | — |
| 1 byte | `ENTER`, `LOCAL_GET`, `LOCAL_SET`, `DEI`, `DEO` | 2 | unsigned index/port |
| 2 bytes | `JMP`, `JMP_Z`, `CALL` | 3 | big-endian absolute byte address |
| 8 bytes | `PUSH`, `PUSHF` | 9 | big-endian `int64` / raw double bits |

`JMP_I` and `CALL_I` take their target from the stack and are single-byte
instructions. Frozen opcode numbers are listed in the
[Machine Contract](Machine-Contract.md).

## 5. Data directives and image layout

The first pass records instructions, data directives, and labels. The second
pass assigns absolute addresses and emits this layout:

```
VM_IMAGE_START + 0:  JMP <code_start>       (3-byte entry stub)
VM_IMAGE_START + 3:  static data from DB/DW/DQ/STR/ALIGN
VM_IMAGE_START + n:  encoded instructions
```

| Directive | Meaning |
| --- | --- |
| `DB a, b, ...` | Emit bytes |
| `DW a, b, ...` | Emit big-endian 16-bit values |
| `DQ a, b, ...` | Emit big-endian 64-bit values |
| `STR "text"` | Emit a string with no automatic terminator |
| `ALIGN n` | Pad the data area to a multiple of `n` (`1..4096`) |

`STR` understands `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, and `\xNN` escapes.
Labels on data directives resolve to absolute RAM addresses, so programs read
and write static data with `LOAD*`/`STORE*`. The image is not copied into the
heap.

```asm
greeting:
    STR "hello\n"
    DB 0
greeting_len:
    DB greeting_len - greeting

    PUSH greeting
    PUSH greeting_len
    LOAD8                     ; load the length byte
    PRINT_STR                 ; expects [addr, len]
```

The assembled payload must fit in `VM_IMAGE_MAX` (16128 bytes).

## 6. Source directives

These directives are expanded by the assembler and emit no VM opcodes.

### 6.1 Includes

```asm
INCLUDE "all.asm"
INCLUDE "graphics/sprites.asm"
```

Relative paths resolve against the including file's directory; absolute paths
are used as-is. `0xasm -I DIR` adds search directories after the including
file's directory and before the built-in `stdlib/` directory. Includes may nest
32 levels; cycles are rejected. Diagnostics inside included files use
`file:line:` prefixes.

### 6.2 Conditionals

```asm
IF WIDTH == 320
    ; ...
ELIF WIDTH == 640
    ; ...
ELSE
    ; ...
ENDIF

IFDEF DEBUG
    ; ...
ENDIF

IFNDEF NO_SCREEN
    ; ...
ENDIF
```

`IF`/`ELIF` use the constant-only expression grammar.
`IFDEF`/`IFNDEF` test whether a constant or macro name is defined. Inactive
branches are skipped but still parsed for balanced directives; they cannot
define constants, labels, or macros.

### 6.3 Macros

```asm
MACRO INC
    PUSH 1
    ADD
ENDMACRO

MACRO STORE_XY value, x, y
    PUSH %value
    PUSH %x
    PUSH %y
    CALL pixel_addr
    STORE8
ENDMACRO
```

- `MACRO name p1, p2, ...` ends with `ENDMACRO` or `ENDM`.
- Parameters are `%1`, `%2`, ... or `%p1`, `%p2`, ...
- `%%` expands to a unique prefix, making labels such as `%%done` safe to reuse.
- Invocation requires exactly the declared number of arguments. Commas inside
  quotes or parentheses stay within one argument.
- Macro names are case-insensitive and may not shadow instructions or
  directives. Zero-parameter macros are allowed.
- Nesting is limited to 32 levels; recursion is rejected by that limit.
- Macro-generated bytes report the invocation line in traces and diagnostics.

## 7. Standard library

`stdlib/` is a set of opt-in, idempotent macro files. Include `all.asm` to pull
in the general-purpose modules, or include individual files. Every file is
guarded with `IFNDEF`, so repeated inclusion is harmless. Programs may override
a stdlib macro with their own `MACRO` definition.

| Module | Provides |
| --- | --- |
| `all.asm` | Umbrella include for every stdlib module |
| `arith.asm` | `INC`, `DEC`, `NEG` |
| `locals.asm` | `GET`/`SET` aliases for `LOCAL_GET`/`LOCAL_SET` |
| `console.asm` | `PRINT_CHAR`, `PRINT`, `FPRINT`, `READ_INT`, `PRINT_STR`, `READ_STR` |
| `ports.asm` | `SYS_*`, `CAPS_*`, `CONSOLE_*`, `SCREEN_*`, `CONTROLLER_*`, `TIMER_*` constants |
| `screen.asm` | `SCREEN_SIZE`, `SET_COLOR`, `PLOT_XY`, `PLOT`, `DRAW`, `CLEAR` |
| `controller.asm` | `READ_KEY`, `QUIT_REQUESTED` |
| `timer.asm` | `NOW_MS` |
| `mem.asm` | `MEMCPY`, `MEMSET`, `MEMZERO`, `MEMCMP` |
| `strings.asm` | `STRCPY`, `STRCMP`, `STREQUAL`, `STRCAT`, `STRSLICE`, `STRFIND`, `STRCHR`, `STRLENZ` |
| `alloc.asm` | `ALLOC_ZERO`, `ALLOC_ARRAY`, `FREE_PTR`, `ARENA_INIT`/`ARENA_ALLOC`/`ARENA_FREE` |
| `util.asm` | `MIN`, `MAX`, `CLAMP`, `ABS`, `ALIGN8`, bit helpers, `RNG_SEED`/`RNG_NEXT` |
| `collections.asm` | `VEC_*` and `MAP_*` helpers over an arena |

Strings are explicit `(addr, length)` byte buffers. `STRLENZ` measures a
zero-terminated buffer; the other string macros take explicit lengths.

## 8. Devices and host capabilities

System and Console ports are provided by every host. Programs that need
graphics or input should read `SYS_CAPS` first.

| Port | Constant | Access | Meaning |
| ---: | --- | --- | --- |
| `0x00` | `SYS_VERSION` | read | ROM format version (`2`) |
| `0x01` | `SYS_DEBUG` | read/write | tick-prefix flag |
| `0x02` | `SYS_HALT` | write | halt with `VM_HALTED` |
| `0x03` | `SYS_CAPS` | read | capability bitmask |
| `0x10` | `CONSOLE_PUTC` | write | low byte to stdout |
| `0x11` | `CONSOLE_GETC` | read | one byte from stdin (`0` on EOF) |
| `0x12` | `CONSOLE_PUTI` | write | signed decimal + newline |
| `0x13` | `CONSOLE_PUTF` | write | double bits + newline |
| `0x14` | `CONSOLE_PUTS_ADDR` | write | string address |
| `0x15` | `CONSOLE_PUTS_LEN` | write | string length |
| `0x16` | `CONSOLE_PUTS` | write | string + newline (value ignored) |
| `0x17` | `CONSOLE_GETS_ADDR` | write | input buffer address |
| `0x18` | `CONSOLE_GETS_MAX` | write | maximum input length |
| `0x19` | `CONSOLE_GETS` | write | read a line (value ignored) |
| `0x1A` | `CONSOLE_GETS_LEN` | read | length from last `GETS` |
| `0x1B` | `CONSOLE_GETI` | read | one integer from stdin |

Capability bits from `SYS_CAPS`:

| Bit | Constant | Device |
| ---: | --- | --- |
| `0x01` | `CAPS_CONSOLE` | Console ports |
| `0x02` | `CAPS_SCREEN` | Screen ports |
| `0x04` | `CAPS_CONTROLLER` | Controller ports |
| `0x08` | `CAPS_TIMER` | Timer port |

Optional devices:

| Port | Constant | Access | Meaning |
| ---: | --- | --- | --- |
| `0x20` | `SCREEN_WIDTH` | read | framebuffer width |
| `0x21` | `SCREEN_HEIGHT` | read | framebuffer height |
| `0x22` | `SCREEN_COLOR` | write | RGB colour in the low 24 bits |
| `0x23` | `SCREEN_PIXEL` | write | `x` in the high 16 bits, `y` in the low 16 |
| `0x24` | `SCREEN_DRAW` | write | present the framebuffer (value ignored) |
| `0x25` | `SCREEN_CLEAR` | write | fill with the current colour (value ignored) |
| `0x30` | `CONTROLLER_KEY` | read | next key-down, `0` if none (consumes) |
| `0x31` | `CONTROLLER_QUIT` | read | `1` after a close request |
| `0x40` | `TIMER_MS` | read | milliseconds since host start |

Read ports are synchronous and blocking; there is no ready/status port.
`CONSOLE_GETS` strips a trailing newline and records its length in
`CONSOLE_GETS_LEN`.

## 9. `0xasm` command line

```sh
./build/0xasm -f program.asm -o program.rom
./build/0xasm -f program.asm -I extra/includes -o program.rom
./build/0xasm -h
```

| Flag | Meaning |
| --- | --- |
| `-f FILE` | input source file |
| `-o FILE` | output ROM path |
| `-I DIR` | add an include search directory (repeatable) |
| `-t` | report macro-generated bytes at their invocation line |
| `-v`, `--version` | print version |
| `-h`, `--help` | print help |

Errors are reported as `file:line: message`, using the innermost file when the
error occurs inside an include or macro expansion.

## 10. Examples

| Example | Demonstrates |
| --- | --- |
| `examples/counter.asm` | a minimal loop |
| `examples/features.asm` | data, procedures, floats, and Console I/O |
| `examples/asm_ext/main.asm` | includes, conditionals, and macros |
| `examples/statistics.asm` | heap allocations and larger programs |
| `examples/boot.asm` | host-aware Console and Screen output |
| `examples/paint.asm` | Screen, Controller, and timer devices |
| `examples/snake.asm` | arena allocation, RNG, strings, and game loop |
| `examples/typography.asm` | bitmap font and text editing |

## 11. Common pitfalls

- `STORE*` expects the **value first, address on top**.
- `PICK 0` duplicates the new top after popping the index; it is not the same as
  `OVER`.
- Jump and call targets are absolute byte addresses, not line or instruction
  numbers.
- `DEI`/`DEO` ports are `0..255`; use the constants from `ports.asm`.
- Static data lives in the image region, not on the heap. Use `ALLOC` for
  runtime buffers.
- Floats are raw bits; `ADD` on a double bit pattern is integer arithmetic.
- Screen, Controller, and timer ports may be absent. Check `SYS_CAPS` before
  using them.
- The image has a fixed maximum size; large embedded bitmaps can overflow it.
- Macro recursion is rejected; use parameterised helpers and labels with `%%`
  instead.
