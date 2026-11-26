# VM Implementation

This document describes the virtual machine in this repository: its state model,
instruction encoding, execution rules, assembler, ROM format, and test setup.
The [Machine Contract](Machine-Contract.md) is the frozen ABI; this document
explains how the current code implements it.

## 1. Overview

The project implements a small, single-threaded **stack VM** in the spirit of
Uxn. It is a standalone VM: assembly source is turned into a byte payload,
wrapped in a `VMIM` ROM, loaded into RAM, and executed locally from start to
halt. The CLI can also load a previously built ROM directly.

The layers are:

1. **Core VM** (`include/vm.h`, `src/vm.c`) — fixed state, byte decoder,
   interpreter, RAM allocator metadata, and two device-port instructions.
2. **Assembler** (`include/asm.h`, `src/asm.c`) — source text to byte payload
   and ROM, with a per-byte source-line map.
3. **Libraries** (`build/lib0xvm.a`, `build/lib0xvm.so`) — the VM, assembler,
   and decoder built as one static and one shared library.
4. **Tools** (`apps/asm`, `apps/dis`, `apps/rom`, `apps/db`) — `0xasm`, `0xdis`,
   `0xrom`, and `0xdb`, built from the same core and decoder.
5. **Hosts** (`hosts/cli`, `hosts/sdl3`, `hosts/wasm`) — `0xvm`, `0xvm-sdl`,
   and the browser host, with host-backed System/Console devices and optional
   Screen, Controller, and timer ports.
6. **Tests** — VM, assembler, ROM, device, conformance ROM, and disassembler
   checks under `tests/`.

Cells and arithmetic are 64-bit; addresses and `ip` are 16-bit byte addresses.
The VM has a fixed 64 KiB RAM, and no buffer in the core grows at runtime. The
core contains no stdio calls: all I/O passes through host device callbacks.

## 2. Source layout

| File | Role |
| --- | --- |
| `include/vm.h` | Public types, opcodes, statuses, constants, device API, and declarations |
| `src/vm.c` | ROM/image loading, decoder, interpreter, device dispatch, heap allocator |
| `include/asm.h`, `src/asm.c` | Two-pass assembler, byte encoding, macros, ROM writer |
| `include/dis.h`, `src/dis.c` | Instruction decoder for disassembly output |
| `hosts/cli/main.c` | CLI host: assemble/run sources and ROMs, trace, tick debug prefix |
| `hosts/sdl3/main.c` | SDL3 host: window, framebuffer, keyboard, quit flag, timer |
| `hosts/wasm/main.c`, `hosts/wasm/library.js` | Browser host: canvas, keyboard, timer, and Console bridge |
| `hosts/wasm/index.html`, `hosts/wasm/app.js` | Browser host page and UI glue |
| `apps/common/tools.c` | File/ROM helpers shared by tools and hosts |
| `apps/common/host.c` | Shared System/Console devices and port-range dispatcher |
| `apps/test/main.c` | Test executable entry point |
| `tests/*.c`, `tests/tests.h` | VM, assembler, ROM, device, host, conformance, and disassembler suites |
| `tests/conformance.asm` | Executable v2 conformance ROM source |
| `stdlib/*.asm` | Opt-in macro library (ports, arithmetic, locals, Console, Screen, Controller, timer) |
| `examples/*.asm` | Example programs |
| `Makefile` | Builds all apps and provides `make test` |

## 3. Machine state

```c
typedef struct {
    void *ctx;
    VmDeviceRead read;   /* VmStatus(void *, VmState *, uint8_t, int64_t *) */
    VmDeviceWrite write; /* VmStatus(void *, VmState *, uint8_t, int64_t) */
} VmDevice;

typedef struct {
    uint16_t ret_ip;   /* byte address to resume at after RET */
    uint16_t prev_fp;  /* caller's frame pointer */
} VmFrame;

typedef struct {
    uint16_t addr;
    uint16_t size;     /* allocated size, rounded up to 8 */
    uint8_t  live;     /* 1 = in use, 0 = on the free list */
} VmBlock;

typedef struct {
    uint16_t ip;       /* byte address of the next instruction in mem */
    uint16_t sp;       /* live cells on the operand stack */
    uint16_t fp;       /* index of the current frame's first local */
    uint16_t fsp;      /* live call frames */
    uint16_t hp;       /* allocator bump pointer; starts at 0x4000 */
    uint64_t ticks;    /* successfully executed instructions */

    int64_t  stack[256];
    uint8_t  mem[65536];
    VmFrame  frames[64];
    VmBlock  blocks[64];
    uint16_t block_count;
    VmDevice dev;      /* host device callbacks; preserved across resets */
} VmState;
```

`ip` is a byte address into `mem`, not an instruction number. `sp` counts live
stack cells; `fp` is the index of the current frame's first local; `fsp` counts
live frames; `hp` is the allocator bump pointer. `ticks` counts successful
steps, including `HALT` and device-driven halts. All VM state is contained in
the struct: no field owns heap memory. The device's opaque `ctx` belongs to the
host and is not interpreted by the core.

`vm_init()` zeroes the state, then sets `hp = VM_HEAP_START` (`0x4000`) and
clears device callbacks. A host installs callbacks with `vm_set_device()` before
running. `vm_free()` resets the machine to the same post-init state but keeps
the installed callbacks; image loading preserves them as well.

### Memory map

```
0x0000 ─────────────── zero page (addressable RAM)
0x0100 ─────────────── image origin (VM_IMAGE_START)
0x3FFF ─────────────── end of image region
0x4000 ─────────────── heap start (initial hp)
0xFFFF ─────────────── end of 64 KiB RAM

device page: 256 ports, outside RAM, addressed only by DEI/DEO
```

The image payload is limited to `VM_IMAGE_MAX` (`0x3F00`) bytes. Loads and
stores may access any byte of `mem`; the full access width must fit below
`0x10000`. `ALLOC`/`FREE` are the only operations constrained to the heap
region. Multi-byte RAM values use big-endian byte order. Device ports are not
RAM addresses and cannot be reached by load/store instructions.

## 4. Instruction encoding

Every instruction starts with one opcode byte. Opcodes are assigned explicitly
because retired I/O opcode values remain reserved:

- `0x00..0x40` contains the current core opcodes, with `0x0E` unassigned.
- `0x41..0x45` are unassigned (retired v1 I/O opcodes).
- `0x46..0x7F` are reserved.
- `0x80` is `DEI` (device input), `0x81` is `DEO` (device output).
- `0x82..0xFF` are reserved.

| Immediate | Opcodes | Total bytes | Encoding |
| --- | --- | ---: | --- |
| none | all others | 1 | — |
| 1 byte | `ENTER` (0x2E), `LOCAL_GET` (0x2F), `LOCAL_SET` (0x30), `DEI` (0x80), `DEO` (0x81) | 2 | unsigned index/port 0..255 |
| 2 bytes | `JMP` (0x09), `JMP_Z` (0x0A), `CALL` (0x2B) | 3 | big-endian absolute byte address |
| 8 bytes | `PUSH` (0x02), `PUSHF` (0x32) | 9 | big-endian `int64` / raw double bits |

`vm_inst_size()` returns the encoded byte count for an opcode. `CALL_I` and
`JMP_I` are single-byte opcodes that read their byte target from the stack.

Fetching decodes `mem[ip]`, checks that the whole instruction fits in RAM,
advances `ip` by `1 + immediate width`, and reads the immediate from `mem`.
Direct jump/call targets are absolute RAM byte addresses, not instruction
indices. `vm_opcode_name()` returns `RESERVED` for unassigned values.

## 5. Instruction semantics

Notation: stack contents are bottom-to-top, so the rightmost value is the top.
For binary ops, `b` is popped first and `a` is the value below it.

### Core and control flow

| Opcode | Effect | Description |
| --- | --- | --- |
| `HALT` | `[...] → [...]` | Stop with `VM_HALTED`; `ip` stays on the opcode |
| `NOP` | `[...] → [...]` | Advance `ip` and `ticks` |
| `PUSH x` | `[...] → [..., x]` | Push a big-endian `int64` immediate |
| `POP` | `[..., x] → [...]` | Discard the top |
| `DUP` | `[..., x] → [..., x, x]` | Duplicate the top |
| `ADD`/`SUB`/`MUL` | `[..., a, b] → [..., a op b]` | Wrapping 64-bit arithmetic |
| `CMP_LT` | `[..., a, b] → [..., 0\|1]` | Signed less-than |
| `JMP target` | `[...] → [...]` | Set `ip` to an absolute byte address |
| `JMP_Z target` | `[..., c] → [...]` | Pop `c`; jump if zero |
| `JMP_I` | `[..., target] → [...]` | Pop target and jump |

Arithmetic uses unsigned operations and casts back to `int64_t`, avoiding
undefined behavior on signed overflow. Comparisons and logical ops push `1` or
`0`, so they feed `JMP_Z` directly.

### Arithmetic, comparisons, bitwise and logical

| Opcode | Effect | Description |
| --- | --- | --- |
| `DIV` | `[..., a, b] → [..., a/b]` | Truncated toward zero; `INT64_MIN / -1` wraps |
| `MOD` | `[..., a, b] → [..., a%b]` | Sign of `a`; `INT64_MIN % -1` is `0` |
| `CMP_EQ`/`CMP_NE` | `[..., a, b] → [..., 0\|1]` | Equality / inequality |
| `CMP_LE`/`CMP_GT`/`CMP_GE` | `[..., a, b] → [..., 0\|1]` | Signed comparisons |
| `AND`/`OR`/`XOR` | `[..., a, b] → [..., a op b]` | 64-bit bitwise ops |
| `NOT` | `[..., a] → [..., ~a]` | Bitwise complement |
| `SHL`/`SHR` | `[..., a, b] → [..., a shift b]` | Count masked to 0..63; `SHR` is logical |
| `LNOT` | `[..., a] → [..., !a]` | Logical NOT of booleanized operand |
| `LAND`/`LOR` | `[..., a, b] → [..., a && b \| a \|\| b]` | Logical AND/OR |

Zero divisors return `VM_ERR_DIV_ZERO` without changing the stack.

### Stack shuffling

| Opcode | Effect | Description |
| --- | --- | --- |
| `SWAP` | `[..., a, b] → [..., b, a]` | Exchange the top two |
| `OVER` | `[..., a, b] → [..., a, b, a]` | Copy the second item to the top |
| `ROT` | `[..., a, b, c] → [..., b, c, a]` | Rotate the top three |
| `PICK` | `[..., x, n] → [..., x_n]` | Pop `n`, copy `stack[sp-2-n]`; `n=0` duplicates the new top |

### RAM access and allocation

| Opcode | Effect | Description |
| --- | --- | --- |
| `LOAD8`/`LOAD16` | `[..., addr] → [..., value]` | Unsigned byte / 16-bit read |
| `LOAD32` | `[..., addr] → [..., value]` | Signed 32-bit read, sign-extended |
| `LOAD32U` | `[..., addr] → [..., value]` | Unsigned 32-bit read, zero-extended |
| `LOAD64` | `[..., addr] → [..., bits]` | Raw 64-bit read |
| `STORE8`/`STORE16`/`STORE32`/`STORE64` | `[..., val, addr] → [...]` | Write 1/2/4/8 bytes; **address on top** |
| `ALLOC` | `[..., size] → [..., addr]` | Allocate a heap block, reusing free blocks |
| `FREE` | `[..., addr] → [...]` | Mark a live block dead for reuse |

`ALLOC` requires a positive size, rounds it up to a multiple of 8, and checks
that the rounded size fits from `hp` to the end of RAM. First-fit reuse splits a
larger dead block into a live prefix and a dead suffix; otherwise `hp` is
bumped. `FREE` requires a live block's start address. The operand is replaced
in place by the returned RAM address.

### Procedures and locals

| Opcode | Operand | Effect | Description |
| --- | --- | --- | --- |
| `CALL` | target | `[...] → [...]` | Push `{return byte address, fp}`, set `fp = sp`, jump |
| `CALL_I` | — | `[..., target] → [...]` | Pop target, then call |
| `RET` | — | `[..., result] → [..., result]` | Unwind one frame, leave result for caller |
| `ENTER` | n | `[...] → [..., 0 × n]` | Reserve `n` zeroed locals at `[fp, fp+n)` |
| `LOCAL_GET` | index | `[...] → [..., local]` | Push `stack[fp + index]` |
| `LOCAL_SET` | index | `[..., v] → [...]` | Pop `v` into `stack[fp + index]` |
| `JMP_I` | — | `[..., target] → [...]` | Pop target and jump |

`CALL` saves the byte address after the call instruction. Locals live on the
same operand stack below new pushes, so frames and locals share the 256-cell
stack while frame metadata uses the fixed 64-entry array. `LOCAL_GET`/`LOCAL_SET`
validate the index against the current frame's local region; the usual order is
`ENTER n; PUSH v; SET i`. `RET` requires a result on the stack, restores `fp`
and `ip` from the top frame, and leaves the result at the caller's top.

### Floats

| Opcode | Effect | Description |
| --- | --- | --- |
| `PUSHF` | `[...] → [..., bits]` | Push a raw 64-bit double literal |
| `FADD`/`FSUB`/`FMUL`/`FDIV` | `[..., a, b] → [..., bits]` | Double-precision arithmetic |
| `FNEG`/`FABS`/`FSQRT` | `[..., a] → [..., bits]` | Unary double operations |
| `FCMP_LT`/`FCMP_EQ`/`FCMP_LE`/`FCMP_GT`/`FCMP_GE` | `[..., a, b] → [..., 0\|1]` | Double comparisons |
| `FTOI` | `[..., bits] → [..., int]` | Truncating conversion; NaN/overflow saturate |
| `ITOF` | `[..., int] → [..., bits]` | `int64_t` to double |

Floats are raw IEEE-754 bit patterns in ordinary `int64_t` cells; the VM does
not track types. `vm_bits_to_f64()` and `vm_f64_to_bits()` use `memcpy` to avoid
strict-aliasing undefined behavior. Printing a double is host policy, supplied
by the Console device and the optional `FPRINT` stdlib macro.

### Device layer

| Opcode | Effect | Description |
| --- | --- | --- |
| `DEI port` | `[...] → [..., value]` | Check stack room, read a cell from the host, push it |
| `DEO port` | `[..., value] → [...]` | Call the host with the top cell, then pop it |

The core has no I/O opcodes and no stdio calls. It only validates the operand
stack, invokes `vm->dev.read` or `vm->dev.write`, and interprets the returned
status. A callback returns `VM_OK`, `VM_HALTED`, or `VM_ERR_IO`; any other
status is treated as `VM_ERR_IO`. A device halt keeps `ip` on the port
instruction: `DEO` consumes its cell, while `DEI` does not push a result.
`ticks` increments for a successful device halt.

The CLI installs these ports:

| Port | Name | Access | Meaning |
| ---: | --- | --- | --- |
| `0x00` | `VM_SYS_VERSION` | read | ROM format version (`2`) |
| `0x01` | `VM_SYS_DEBUG` | read/write | tick-prefix flag |
| `0x02` | `VM_SYS_HALT` | write | halt with `VM_HALTED` |
| `0x03` | `VM_SYS_CAPS` | read | capability bitmask (`0x01` console, `0x02` screen, `0x04` controller, `0x08` timer) |
| `0x10` | `VM_CONSOLE_PUTC` | write | low byte to stdout |
| `0x11` | `VM_CONSOLE_GETC` | read | one byte from stdin (`0` on EOF) |
| `0x12` | `VM_CONSOLE_PUTI` | write | signed decimal + newline |
| `0x13` | `VM_CONSOLE_PUTF` | write | double bits + newline |
| `0x14` | `VM_CONSOLE_PUTS_ADDR` | write | RAM address of the string to write |
| `0x15` | `VM_CONSOLE_PUTS_LEN` | write | length of the string to write |
| `0x16` | `VM_CONSOLE_PUTS` | write | string + newline (value ignored) |
| `0x17` | `VM_CONSOLE_GETS_ADDR` | write | RAM address for input |
| `0x18` | `VM_CONSOLE_GETS_MAX` | write | maximum input length |
| `0x19` | `VM_CONSOLE_GETS` | write | read a line into RAM (value ignored) |
| `0x1A` | `VM_CONSOLE_GETS_LEN` | read | length from the last `GETS` |
| `0x1B` | `VM_CONSOLE_GETI` | read | one integer from stdin |

`PUTS` and `GETS` validate the configured RAM range before touching `mem`.
`GETS` strips a trailing newline and records its length for `GETS_LEN`.
Read ports are synchronous and blocking; there is no ready/status port. `GETC`
reports EOF as `0`, `GETI` maps EOF/bad input/overflow to `0`, and `GETS` maps
EOF to an empty line; stream errors return `VM_ERR_IO`. `-D` sets
`VM_SYS_DEBUG`; the Console callback then prefixes formatted output with
`[vm tick=N]`. Another host can install different callbacks without changing
the core or the ROM.

The following source-level mnemonics are not opcodes. They are opt-in macros
in `stdlib/console.asm` (which includes `stdlib/ports.asm` for the constants):

| Macro | Expansion |
| --- | --- |
| `PRINT_CHAR` | `DEO 0x10` |
| `PRINT` | `DEO 0x12` |
| `FPRINT` | `DEO 0x13` |
| `READ_INT` | `DEI 0x1B` |
| `PRINT_STR` | `DEO 0x15`, `DEO 0x14`, `PUSH 0`, `DEO 0x16` |
| `READ_STR` | `DEO 0x18`, `DEO 0x17`, `PUSH 0`, `DEO 0x19`, `DEI 0x1A` |

## 6. Execution model

`vm_step(vm)` executes exactly one instruction:

- Fetch and decode from `vm->ip`; a truncated immediate or out-of-RAM address is
  `VM_ERR_BAD_IP`, and an unassigned opcode is `VM_ERR_BAD_OP`.
- Normally `ip` becomes the address after the instruction.
- `JMP`/`JMP_Z`, `JMP_I`, `CALL`/`CALL_I`, and `RET` assign a new `ip`.
- `HALT` or a callback returning `VM_HALTED` stops execution and leaves `ip` on
  the current instruction.

`ticks` increments only after a successful step. `vm_run(vm, max_steps)` repeats
`vm_step` until a non-`VM_OK` status or the step budget is exhausted;
`max_steps == 0` means unlimited, and exhaustion returns `VM_OK` so the caller
can resume later.

### Atomic error handling

Every instruction validates its preconditions before mutating state. For
example, binary ops check stack depth first; `DIV`/`MOD` check the divisor
before popping; `JMP_Z` and `JMP_I` validate targets before consuming the
condition or target; `ALLOC` checks size, rounding, stack room, heap room, and
metadata room before changing `hp`, `sp`, or the block list; loads/stores
validate stack depth and address range before touching RAM; `LOCAL_SET` checks
the slot before writing; and `RET` checks the frame and result before
unwinding. `DEI` checks stack room and a callback exists before invoking the
host; `DEO` checks stack depth before reading the value. A failed step leaves
`ip`, `sp`, `fp`, `fsp`, `hp`, `ticks`, RAM, stack, frames, and block metadata
exactly as they were.

A device callback is host code. If it returns `VM_ERR_IO`, the core has made no
state change; callbacks must avoid mutating VM state unless they can complete
successfully. Side effects outside `VmState` (stdout, stdin, files, host
buffers) belong to the host and are not rolled back.

Status codes: `VM_OK`, `VM_HALTED`, `VM_ERR_BAD_IP`, `VM_ERR_BAD_OP`,
`VM_ERR_STACK_OVERFLOW`, `VM_ERR_STACK_UNDERFLOW`, `VM_ERR_HEAP_OVERFLOW`,
`VM_ERR_BAD_ADDR`, `VM_ERR_DIV_ZERO`, `VM_ERR_IO`, `VM_ERR_BAD_FRAME`, and
`VM_ERR_BAD_LOCAL`. Use `vm_status_str()` and `vm_opcode_name()` for printable
names.

## 7. Assembler

`src/asm.c` is a line-oriented, two-pass assembler.

- One instruction per line; mnemonics are case-insensitive.
- Comments begin with `;`, `#`, or `//` and are ignored inside quoted strings.
- Labels are `name:` (letters/digits/underscore, starting with a letter or `_`)
  and may share a line with an instruction or data directive.
- `JMP`, `JMP_Z`, and `CALL` accept a label or an integer expression.
- Operand kinds are none, integer expression, float literal, jump target,
  local index, and device port; missing/illegal operands are reported with line
  numbers. `DEI`/`DEO` ports must be expressions in `0..255`.

### Expressions and constants

`PUSH`, `ENTER`, `LOCAL_GET`/`SET`, `CALL`, jump targets, `DEI`/`DEO` ports, and
numeric data values accept integer expressions: decimal or `0x` hex literals,
unary `+ - ~`, binary `+ - * / % << >> & ^ | == != < <= > >=` (C-like
precedence), parentheses, and previously defined constants. `EQU name expr`
defines a constant. Arithmetic wraps in `uint64_t`; division by zero is an
error; shifts of 64 or more yield zero; and `-9223372036854775808` is accepted
as `INT64_MIN`.

### Includes, conditionals, and macros

These directives are expanded entirely in the assembler; they emit no VM
opcode and require no changes to the ROM header, RAM map, or device ports.

- `INCLUDE "path"` inserts another source file. Relative paths resolve against
  the including file's directory; absolute paths are used as-is. Includes may
  nest up to 32 levels, and cycles are rejected. Diagnostics inside included
  files use `file:line:` prefixes.
- `IF expr` / `IFDEF name` / `IFNDEF name`, with optional `ELIF expr` and
  `ELSE`, are closed by `ENDIF`. `IF`/`ELIF` expressions use the same
  constant-only expression grammar as operands; `IFDEF`/`IFNDEF` test whether
  a constant or macro name is defined. Inactive branches are skipped but still
  parsed for balanced directives, so they cannot define constants, labels, or
  macros.
- `MACRO name p1, p2, ...` ... `ENDMACRO` (or `ENDM`) defines a reusable body.
  Parameters are referenced as `%1`, `%2`, ... or `%p1`, `%p2`, ...; `%%`
  expands to a unique prefix, which makes macro-local labels safe to repeat:
  `%%done:` and `JMP %%done`. Macro names are case-insensitive and may not
  shadow instructions or directives. Invocation is `name arg1,
  arg2, ...` with exactly the declared number of arguments; commas inside
  quotes or parentheses stay within one argument. Nesting is limited to 32
  levels, and recursion is rejected by that limit. Zero-parameter macros are
  allowed; the stdlib uses them for `INC`, `PRINT_CHAR`, and friends. Macro
  names may not shadow instructions or directives, but may replace stdlib
  macros.

`AsmProgram.lines` records the line of the original call site for instructions
and data emitted from a macro, and the real line inside an included file for
included source. Errors report the innermost source path when one is
available.

### Image layout and data directives

The first pass records instructions, data directives, and labels. The second
pass computes every label's absolute RAM address and emits bytes with this
layout:

```
VM_IMAGE_START + 0:  JMP <code_start>       (3-byte entry stub)
VM_IMAGE_START + 3:  static data from DB/DW/DQ/STR/ALIGN
VM_IMAGE_START + n:  encoded instructions
```

`DB` (bytes), `DW` (big-endian 16-bit), `DQ` (big-endian 64-bit), `STR`
(quoted string with `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, `\xNN` escapes), and
`ALIGN n` (1..4096) append bytes to the static-data area. `ALIGN` pads relative
to the data area's own offset. Labels on data directives resolve to their
absolute RAM addresses, so programs use `LOAD*`/`STORE*` to access them; the
image is not copied into the heap. The total payload must fit in `VM_IMAGE_MAX`.

### Stdlib macros, aliases, and line map

The assembler has no built-in pseudo-ops or source aliases. `stdlib/arith.asm`
defines `INC`, `DEC`, and `NEG` as multi-instruction macros;
`stdlib/locals.asm` defines `GET` and `SET` as aliases for `LOCAL_GET` and
`LOCAL_SET`; `stdlib/console.asm` defines the device-port sequences in section
5. `stdlib/all.asm` includes all three. Each file is guarded with `IFNDEF`, so
including it more than once is harmless. A program opts in with
`INCLUDE "all.asm"` (or an individual file) and may override any stdlib name
with its own `MACRO` definition. `INCLUDE` searches the including file's
directory, then the `-I` directories passed to `0xasm`, then the built-in
stdlib directory compiled into the assembler.

`AsmProgram.lines` maps every payload byte to its source line (0 for
generated bytes); `asm_line_of(prog, addr)` takes an absolute RAM byte address,
which is what the CLI's `-t` trace prints. Macro-generated bytes report the
invocation line.

`asm_parse_string()` and `asm_parse_file()` return 0 on success or -1 on
failure with a line-numbered message in `prog.error` and `prog.error_line`
(unknown mnemonics, invalid operands or ports, duplicate/undefined labels,
duplicate constants, bad escapes, out-of-range jumps/targets, oversized
images, and file I/O errors). `asm_program_free()` releases the emitted payload
buffers while preserving the diagnostic fields. The assembler may grow its
source-side scratch buffers, but the emitted payload and the VM itself are
fixed.

## 8. ROM format and loading

`asm_make_rom()` encodes a 16-byte header plus payload; `asm_write_rom()`
writes those bytes to a file. The header is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `56 4D 49 4D` (`"VMIM"`) |
| 4 | 1 | Format version, currently `2` |
| 5 | 1 | Flags, must be `0` |
| 6 | 2 | Reserved, must be `0` |
| 8 | 2 | Payload length, big-endian, `0..0x3F00` |
| 10 | 2 | Entry point, big-endian, normally `0x0100` |
| 12 | 4 | Reserved, must be `0` |

`vm_load_rom()` checks the header, requires the buffer length to be exactly
header plus payload, then calls `vm_load_image()`. That zeroes all RAM and
metadata, copies the payload to `0x0100`, sets `hp = 0x4000` and `ip` to the
entry point, and leaves `ticks = 0`. The entry must lie inside the loaded
payload. Malformed images return `VM_ERR_BAD_IP` without changing the state.

The tools support both paths:

```sh
./build/0xvm -f examples/counter.asm     # assemble, wrap, load, run
./build/0xasm -f examples/counter.asm    # write counter.rom
./build/0xvm -f counter.rom              # load and run the ROM
./build/0xdis -f counter.rom             # decode the ROM payload
./build/0xrom -f counter.rom             # inspect and validate the header
```

`dis` starts at the entry point, follows the entry `JMP` past static data, and
decodes linearly to the end of the payload. Reserved opcode bytes are printed as
`RESERVED (0xNN)` instead of being treated as errors; immediates that cross the
payload end are reported as decode errors.

## 9. Building and testing

```sh
make all                 # build lib0xvm, the 0x* tools, and 0xtest
make test                # build and run build/0xtest
make shared              # build only the shared library
make type=RELEASE test   # release build (no sanitizers)
```

The Makefile discovers every `tests/*.c`, compiles it, and links the objects
into `build/0xtest` with `build/lib0xvm.a`. DEBUG builds enable
AddressSanitizer and UndefinedBehaviorSanitizer.

- The **VM suite** covers stack ops and shuffling, arithmetic/wraparound,
  division edges, comparisons/bitwise/logical ops, both jump paths and indirect
  jumps, all RAM widths and bounds, allocation/free-list reuse, calls/locals,
  floats, and error atomicity.
- The **device suite** covers fake host reads and writes, argument passing,
  device errors and atomicity, device halts, retired opcode values, and opcode
  names.
- The **assembler suite** covers parsing, comments, labels, expressions and
  constants, every mnemonic, opt-in stdlib macros, data directives
  and image layout, byte-address line mapping, file loading, running parsed
  programs, and parse errors.
- The **ROM suite** checks header encoding, load validation, and writing a v2
  ROM that can be reloaded and run.
- The **conformance suite** assembles `tests/conformance.asm` and runs it from
  both an in-memory ROM and a generated `build/conformance-v2.rom`. It checks
  the exact transcript in `tests/conformance.expected`, final stack state, and
  device-halt status. `make conformance-check` runs the same artifact through
  `0xvm` with input `B-7\nhey\n` and diffs the CLI output.
- The **negative conformance cases** build raw images for reserved/retired
  opcodes, stack overflow/underflow, division by zero, out-of-range loads and
  jump targets, bad frames/locals, heap overflow, and device errors. Each case
  requires the expected status and atomic state on the failing step; a
  truncated immediate is placed at the RAM boundary to test decode bounds.
- The **disassembler suite** checks every immediate shape, reserved bytes, RAM
  and payload truncation, and linear decoding of a loaded image.

For example:

```sh
./build/0xvm -f examples/counter.asm
```

prints the loop's `0..5` and the final `60` before halting. `gcd.asm` prints
`21`; `features.asm` demonstrates data labels, constants, locals, floats, and
Console I/O together; `statistics.asm` reads a count and values from stdin and
prints descriptive statistics.

## 10. Current limitations

- Floats use raw bit patterns with no runtime type tag: mixing integer and float
  ops on the same value is the program's responsibility.
- `0xvm` supplies System and Console; `0xvm-sdl` and the browser host additionally
  supply Screen, Controller, and timer. Other hosts may assign other ports.
- There is no interrupt or event-vector mechanism; those ports remain reserved.
- The image region has no relocation or protection model beyond the loader's
  payload-length check.
- `INCLUDE` resolves through the local filesystem; ROMs are self-contained and
  do not carry included source.
