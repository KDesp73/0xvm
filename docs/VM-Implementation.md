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
   interpreter, RAM allocator metadata.
2. **Assembler** (`include/asm.h`, `src/asm.c`) — source text to byte payload
   and ROM, with a per-byte source-line map.
3. **CLI** (`apps/cli/main.c`) — local `run`/`build` commands, tracing, and tick
   prefixes.
4. **Tests** (`tests/test_vm.c`, `tests/test_asm.c`, `tests/test_rom.c`) — VM,
   assembler, and ROM conformance checks.

Cells and arithmetic are 64-bit; addresses and `ip` are 16-bit byte addresses.
The VM has a fixed 64 KiB RAM and no buffer in the core grows at runtime.

## 2. Source layout

| File | Role |
| --- | --- |
| `include/vm.h` | Public types, opcodes, statuses, constants, and API declarations |
| `src/vm.c` | ROM/image loading, decoder, interpreter, heap allocator |
| `include/asm.h`, `src/asm.c` | Two-pass assembler, byte encoding, ROM writer |
| `apps/cli/main.c` | CLI: assemble, build ROMs, run sources/ROMs, trace |
| `apps/test/main.c` | Test executable entry point |
| `tests/*.c`, `tests/tests.h` | VM, assembler, and ROM test suites |
| `examples/*.asm` | Example programs |
| `Makefile` | Builds all apps and provides `make test` |

## 3. Machine state

```c
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
} VmState;
```

`ip` is a byte address into `mem`, not an instruction number. `sp` counts live
stack cells; `fp` is the index of the current frame's first local; `fsp` counts
live frames; `hp` is the allocator bump pointer. `ticks` counts successful
steps, including `HALT`. All state is contained in the struct: no field owns
heap memory.

`vm_init()` zeroes the state, then sets `hp = VM_HEAP_START` (`0x4000`).
`vm_free()` resets it to the same post-init state; it exists for host symmetry
and is safe to call repeatedly.

### Memory map

```
0x0000 ─────────────── zero page (addressable RAM)
0x0100 ─────────────── image origin (VM_IMAGE_START)
0x3FFF ─────────────── end of image region
0x4000 ─────────────── heap start (initial hp)
0xFFFF ─────────────── end of 64 KiB RAM
```

The image payload is limited to `VM_IMAGE_MAX` (`0x3F00`) bytes. Loads and
stores may access any byte of `mem`; the full access width must fit below
`0x10000`. `ALLOC`/`FREE` are the only operations constrained to the heap
region. Multi-byte RAM values use big-endian byte order.

## 4. Instruction encoding

Every instruction starts with one opcode byte. Opcodes `0x00..0x45` are assigned
in `VmOpcode` order; `0x46..0x7F` are reserved, and `0x80..0xFF` are reserved
for a later device/vector layer.

| Immediate | Opcodes | Total bytes | Encoding |
| --- | --- | ---: | --- |
| none | all others | 1 | — |
| 1 byte | `ENTER` (0x2E), `LOCAL_GET` (0x2F), `LOCAL_SET` (0x30) | 2 | unsigned index |
| 2 bytes | `JMP` (0x09), `JMP_Z` (0x0A), `CALL` (0x2B) | 3 | big-endian absolute byte address |
| 8 bytes | `PUSH` (0x02), `PUSHF` (0x32) | 9 | big-endian `int64` / raw double bits |

`vm_inst_size()` returns the encoded byte count for an opcode (1 for unknown
values). `CALL_I` and `JMP_I` are single-byte opcodes that read their byte
target from the stack.

Fetching decodes `mem[ip]`, checks that the whole instruction fits in RAM,
advances `ip` by `1 + immediate width`, and reads the immediate from `mem`.
Direct jump/call targets are absolute RAM byte addresses, not instruction
indices.

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
| `FPRINT` | `[..., bits] → [...]` | Print the double |

Floats are raw IEEE-754 bit patterns in ordinary `int64_t` cells; the VM does
not track types. `vm_bits_to_f64()` and `vm_f64_to_bits()` use `memcpy` to avoid
strict-aliasing undefined behavior.

### Host I/O

| Opcode | Effect | Description |
| --- | --- | --- |
| `PRINT` | `[..., v] → [...]` | Print `v` as a signed integer |
| `FPRINT` | `[..., bits] → [...]` | Print the double with `%.17g` |
| `PRINT_STR` | `[..., addr, len] → [...]` | Print `len` RAM bytes (`len` on top) |
| `PRINT_CHAR` | `[..., codepoint] → [...]` | Print the low byte |
| `READ_INT` | `[...] → [..., value]` | Read an integer from stdin (`0` on bad input) |
| `READ_STR` | `[..., addr, max] → [..., len]` | Read a line into RAM; replace `addr` with length |

`PRINT`, `PRINT_STR`, and `FPRINT` add `[vm tick=N] ` when `-D` is enabled;
that is host output policy, not machine state. `READ_INT` accepts up to 127
input characters. `READ_STR` strips the trailing newline, requires `max > 0`
and a valid RAM range, and pushes the resulting length. I/O failures return
`VM_ERR_IO`. These opcodes are transitional; a later device layer will replace
them with port reads/writes.

## 6. Execution model

`vm_step(vm)` executes exactly one instruction:

- Fetch and decode from `vm->ip`; a truncated immediate or out-of-RAM address is
  `VM_ERR_BAD_IP`, and an unassigned opcode is `VM_ERR_BAD_OP`.
- Normally `ip` becomes the address after the instruction.
- `JMP`/`JMP_Z`, `JMP_I`, `CALL`/`CALL_I`, and `RET` assign a new `ip`.
- `HALT` leaves `ip` on the halt opcode and returns `VM_HALTED`.

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
unwinding. A failed step leaves `ip`, `sp`, `fp`, `fsp`, `hp`, `ticks`, RAM,
stack, frames, and block metadata exactly as they were. Tests assert this for
each error class.

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
- Operand kinds are none, integer expression, float literal, jump target, and
  local index; missing/illegal operands are reported with line numbers.

### Expressions and constants

`PUSH`, `ENTER`, `LOCAL_GET`/`SET`, `CALL`, jump targets, and numeric data
values accept integer expressions: decimal or `0x` hex literals, `+ - ~`,
`+ - * / %`, parentheses, and previously defined constants. `EQU name expr`
defines a constant. Arithmetic wraps in `uint64_t`; division by zero is an
error; and `-9223372036854775808` is accepted as `INT64_MIN`.

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

### Pseudo-ops, macros, and line map

`INC`, `DEC`, and `NEG` expand to several real instructions. `GET` and `SET`
are aliases for `LOCAL_GET` and `LOCAL_SET`. `AsmProgram.lines` maps every
payload byte to its source line (0 for generated bytes);
`asm_line_of(prog, addr)` takes an absolute RAM byte address, which is what the
CLI's `-t` trace prints.

`asm_parse_string()` and `asm_parse_file()` return 0 on success or -1 on
failure with a line-numbered message in `prog.error` and `prog.error_line`
(unknown mnemonics, invalid operands, duplicate/undefined labels, duplicate
constants, bad escapes, out-of-range jumps/targets, oversized images, and file
I/O errors). `asm_program_free()` releases the emitted payload buffers while
preserving the diagnostic fields. The assembler may grow its source-side
scratch buffers, but the emitted payload and the VM itself are fixed.

## 8. ROM format and loading

`asm_make_rom()` encodes a 16-byte header plus payload; `asm_write_rom()`
writes those bytes to a file. The header is:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `56 4D 49 4D` (`"VMIM"`) |
| 4 | 1 | Format version, currently `1` |
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

The CLI supports both paths:

```sh
./build/vm-cli run -f examples/counter.asm     # assemble, wrap, load, run
./build/vm-cli build -f examples/counter.asm   # write counter.rom
./build/vm-cli run -f counter.rom              # load and run the ROM
```

## 9. Building and testing

```sh
make all                 # build vm-cli and vm-test
make test                # build and run build/vm-test
make type=RELEASE test   # release build (no sanitizers)
```

The Makefile discovers every `tests/*.c`, compiles it, and links the objects
into `build/vm-test` with the core VM sources. DEBUG builds enable
AddressSanitizer and UndefinedBehaviorSanitizer.

- The **VM suite** covers stack ops and shuffling, arithmetic/wraparound,
  division edges, comparisons/bitwise/logical ops, both jump paths and indirect
  jumps, all RAM widths and bounds, allocation/free-list reuse, calls/locals,
  floats, I/O, and error atomicity.
- The **assembler suite** covers parsing, comments, labels, expressions and
  constants, every mnemonic, pseudo-ops, data directives and image layout,
  byte-address line mapping, file loading, running parsed programs, and parse
  errors.
- The **ROM suite** checks header encoding, load validation, and writing a ROM
  that can be reloaded and run.

For example:

```sh
./build/vm-cli run -f examples/counter.asm
```

prints the loop's `0..5` and the final `60` before halting. `gcd.asm` prints
`21`; `features.asm` demonstrates data labels, constants, locals, floats, and
I/O together; `statistics.asm` reads a count and values from stdin and prints
descriptive statistics.

## 10. Current limitations

- Floats use raw bit patterns with no runtime type tag: mixing integer and float
  ops on the same value is the program's responsibility.
- `PRINT`/`READ_*` remain core opcodes; a later device layer will move them out
  of the core.
- The image region has no relocation or protection model beyond the loader's
  payload-length check.
- The assembler has no conditional assembly, include files, or true macro
  definition facility; its "macros" are fixed pseudo-op expansions.
- The device page and event vectors described in the roadmap are not implemented
  yet.
