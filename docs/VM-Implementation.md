# VM Implementation

This document describes the virtual machine in this repository: its state model,
instruction set, execution rules, migration format, assembler, and test setup.
It is a map of the **wire v3** implementation, not a proposal for future work.

## 1. Overview

The project implements a small, single-threaded **stack VM** for
process-migration experiments. The core constraint is that every piece of
mutable execution state must be capturable in a compact byte buffer so execution
can be paused at an instruction boundary, moved, and resumed elsewhere.

The VM has three layers:

1. **Core VM** (`include/vm.h`, `src/vm.c`) — state, interpreter, serialization.
2. **Assembler** (`include/asm.h`, `src/asm.c`) — source text to instruction arrays.
3. **Runtime/CLI** (`apps/cli/main.c`, `include/net.h`, `src/net.c`) — runs programs
   and moves state between processes.

Program code is *not* migratable state. Both peers load the same `VmInst` array;
only `VmState` crosses the wire. Values and heap addresses are 64-bit, and all
dynamic buffers (operand stack, call frames, allocator metadata, heap) grow on
demand, so there is no fixed memory ceiling below the host's own limits.

## 2. Source layout

| File | Role |
| --- | --- |
| `include/vm.h` | Public types, opcodes, statuses, and API declarations |
| `src/vm.c` | Interpreter, heap helpers, full/diff serialization |
| `include/asm.h`, `src/asm.c` | Assembler (two passes, expressions, data directives, line map) |
| `include/net.h`, `src/net.c` | TCP transport: framing, state transfer, graceful halt |
| `apps/cli/main.c` | CLI commands, local run, multi-hop migration, tracing |
| `apps/test/main.c` | Test executable entry point |
| `tests/test_vm.c`, `tests/test_asm.c`, `tests/test_net.c` | Test suites |
| `examples/*.asm` | Example programs |
| `Makefile` | Builds all apps and provides `make test` |

## 3. Data model

```c
typedef struct { uint8_t op; int64_t arg; } VmInst;

typedef struct { uint32_t ret_ip; uint32_t prev_fp; } VmFrame;
typedef struct { uint64_t addr, size; uint8_t live; } VmBlock;

typedef struct {
    uint32_t ip, sp, fp, ticks;
    uint64_t hp, heap_cap;
    int64_t  *stack;    uint32_t stack_cap;
    uint8_t  *heap;
    VmFrame  *frames;   uint32_t fsp, frame_cap;
    VmBlock  *blocks;   uint32_t block_count, block_cap;
} VmState;
```

`ip` is the next instruction index; `sp` is one past the operand-stack top;
`fp` is the index of the current frame's first local (always `<= sp`). `hp` is
one past the last used heap byte and `heap_cap >= hp` is the allocated buffer.
`ALLOC` addresses are heap offsets, not pointers. `ticks` counts successfully
executed instructions.

`VmFrame` records a return address and the caller's `fp`; `VmBlock` is
out-of-band allocator metadata (`addr`, rounded `size`, `live` flag). Keeping
metadata outside the heap means `ALLOC` addresses stay simple bump offsets while
all of the metadata is still serialized.

`vm_init()` zero-initializes a state. `vm_free()` releases all owned buffers and
resets it; it is safe on a zeroed/freed state. `vm_clone()` makes a deep copy,
and requires its destination to be initialized first. Deserialization releases
any buffers held by its output before installing the restored state.

## 4. Instruction set

Notation: stack contents are bottom-to-top, so the rightmost value is the top.
For binary ops, `b` is popped first. `arg` is used by `PUSH`, `JMP`, `JMP_Z`,
`CALL`, `ENTER`, `LOCAL_GET`, `LOCAL_SET`, and `PUSHF`; other opcodes ignore it.

### Core

| Opcode | Operand | Effect | Description |
| --- | --- | --- | --- |
| `HALT` | — | `[...] → [...]` | Stop with `VM_HALTED`; `ip` unchanged |
| `NOP` | — | `[...] → [...]` | Advance `ip` and `ticks` |
| `PUSH` | int64 | `[...] → [..., x]` | Push an immediate |
| `POP` | — | `[..., x] → [...]` | Discard the top |
| `DUP` | — | `[..., x] → [..., x, x]` | Duplicate the top |
| `ADD`/`SUB`/`MUL` | — | `[..., a, b] → [..., a op b]` | Wrapping 64-bit arithmetic |
| `CMP_LT` | — | `[..., a, b] → [..., a<b]` | Signed compare; pushes 1 or 0 |
| `JMP` | target | `[...] → [...]` | Set `ip` to an absolute instruction index |
| `JMP_Z` | target | `[..., c] → [...]` | Pop `c`; jump if zero |
| `ALLOC` | — | `[..., size] → [..., addr]` | Allocate heap bytes, reusing free blocks when possible |
| `LOAD32` | — | `[..., addr] → [..., value]` | Read a big-endian `int32`, sign-extended |
| `STORE32` | — | `[..., val, addr] → [...]` | Write low 32 bits big-endian; **address on top** |
| `PRINT` | — | `[..., v] → [...]` | Print `v` (tick prefix only with `--debug`) |
| `MIGRATE` | — | `[...] → [...]` | Advance `ip` and yield `VM_YIELD_MIGRATE` |

### Arithmetic, comparisons, bitwise and logical

| Opcode | Effect | Description |
| --- | --- | --- |
| `DIV` | `[..., a, b] → [..., a/b]` | Truncated toward zero; `INT64_MIN / -1` wraps to `INT64_MIN` |
| `MOD` | `[..., a, b] → [..., a%b]` | Sign of `a`; `INT64_MIN % -1` is `0` |
| `CMP_EQ`/`CMP_NE` | `[..., a, b] → [..., 0\|1]` | Equality / inequality |
| `CMP_LE`/`CMP_GT`/`CMP_GE` | `[..., a, b] → [..., 0\|1]` | Signed comparisons |
| `AND`/`OR`/`XOR` | `[..., a, b] → [..., a op b]` | 64-bit bitwise ops |
| `NOT` | `[..., a] → [..., ~a]` | Bitwise complement |
| `SHL`/`SHR` | `[..., a, b] → [..., a << (b&63)]` | Shift count masked to 0..63; `SHR` is logical |
| `LNOT` | `[..., a] → [..., !a]` | Logical NOT of booleanized operand |
| `LAND`/`LOR` | `[..., a, b] → [..., a && b \| a \|\| b]` | Logical AND/OR of booleanized operands |

### Stack shuffling

| Opcode | Effect | Description |
| --- | --- | --- |
| `SWAP` | `[..., a, b] → [..., b, a]` | Exchange the top two |
| `OVER` | `[..., a, b] → [..., a, b, a]` | Copy the second item to the top |
| `ROT` | `[..., a, b, c] → [..., b, c, a]` | Rotate the top three |
| `PICK` | `[..., x, n] → [..., x_n]` | Pop `n`, push `stack[sp-2-n]`; `n=0` duplicates the new top |

### Heap access and reuse

| Opcode | Effect | Description |
| --- | --- | --- |
| `LOAD8`/`LOAD16` | `[..., addr] → [..., value]` | Unsigned byte / 16-bit read, zero-extended |
| `LOAD32U` | `[..., addr] → [..., value]` | Unsigned 32-bit read, zero-extended |
| `LOAD64` | `[..., addr] → [..., bits]` | Raw 64-bit read |
| `STORE8`/`STORE16`/`STORE32`/`STORE64` | `[..., val, addr] → [...]` | Write 1/2/4/8 bytes big-endian; address on top |
| `FREE` | `[..., addr] → [...]` | Return a live block to the free list |

All accesses honor `[0, hp)` for the access width; out-of-range or
misaligned-size requests return `VM_ERR_BAD_ADDR`. `LOAD32` sign-extends while
`LOAD32U`, `LOAD8`, and `LOAD16` zero-extend.

### Procedures and indirect control flow

| Opcode | Operand | Effect | Description |
| --- | --- | --- | --- |
| `CALL` | target | `[...] → [...]` | Push `{return ip, fp}`, set `fp = sp`, jump to target |
| `CALL_I` | — | `[..., target] → [...]` | Pop target, then call |
| `RET` | — | `[..., result] → [..., result]` | Unwind one frame and leave the result for the caller |
| `ENTER` | n | `[...] → [..., 0 × n]` | Reserve `n` zeroed locals in the current frame |
| `LOCAL_GET` | index | `[...] → [..., local]` | Push `stack[fp + index]` (`GET` alias) |
| `LOCAL_SET` | index | `[..., v] → [...]` | Pop `v` into `stack[fp + index]` (`SET` alias) |
| `JMP_I` | — | `[..., target] → [...]` | Pop target and jump |

### Floats (raw IEEE-754 bit patterns on the integer stack)

| Opcode | Effect | Description |
| --- | --- | --- |
| `PUSHF` | `[...] → [..., bits]` | Push a 64-bit double literal's raw bits |
| `FADD`/`FSUB`/`FMUL`/`FDIV` | `[..., a, b] → [..., a op b]` | Double-precision arithmetic |
| `FNEG`/`FABS`/`FSQRT` | `[..., a] → [..., result]` | Unary double operations |
| `FCMP_LT`/`FCMP_EQ`/`FCMP_LE`/`FCMP_GT`/`FCMP_GE` | `[..., a, b] → [..., 0\|1]` | Double comparisons |
| `FTOI` | `[..., bits] → [..., int]` | Double to `int64_t`; NaN/overflow saturates |
| `ITOF` | `[..., int] → [..., bits]` | `int64_t` to double |
| `FPRINT` | `[..., bits] → [...]` | Print the double (tick prefix only with `--debug`) |

`vm_bits_to_f64()` and `vm_f64_to_bits()` are `memcpy`-based helpers that avoid
strict-aliasing undefined behavior. Floats occupy the same 64-bit stack slots as
integers; the type is tracked by the program, not by the VM.

### Host I/O

| Opcode | Effect | Description |
| --- | --- | --- |
| `PRINT_STR` | `[..., addr, len] → [...]` | Print `len` heap bytes (`len` on top) |
| `PRINT_CHAR` | `[..., codepoint] → [...]` | Print one byte |
| `READ_INT` | `[...] → [..., value]` | Read an integer from stdin (`0` on bad input) |
| `READ_STR` | `[..., addr, max] → [..., len]` | Read a line into the heap, push its length |

Host resources (file descriptors, buffered input) are deliberately *not* part of
migratable state. I/O failures return `VM_ERR_IO`.

Details worth calling out:

- **Arithmetic wraps deliberately.** `ADD`, `SUB`, `MUL`, and the assembler's
  expression evaluator compute in `uint64_t` and cast back, avoiding undefined
  behavior on signed overflow.
- **Division avoids C's overflow cases**, and zero divisors return
  `VM_ERR_DIV_ZERO` without modifying the stack.
- **Comparisons and logical ops push `1` or `0`**, feeding `JMP_Z` directly.
- **Stores take the address on top**: `PUSH val; PUSH addr; STORE32`.

## 5. Execution model

`vm_step(vm, prog, prog_len)` executes exactly one instruction:

- Normally `ip` advances to `ip + 1`.
- `JMP`/`JMP_Z`, `JMP_I`, `CALL`/`CALL_I`, and `RET` assign a new `ip`.
- `HALT` leaves `ip` at the halt instruction and returns `VM_HALTED`.
- `MIGRATE` advances `ip` past itself and returns `VM_YIELD_MIGRATE`.

`ticks` increments only on success. `vm_run(vm, prog, prog_len, max_steps)`
repeats `vm_step` until a non-`VM_OK` status or the step budget is exhausted;
`max_steps == 0` means unlimited, and exhaustion returns `VM_OK` so the caller
can resume later.

### Atomic error handling

Every instruction validates its preconditions **before** mutating state. For
example, binary ops check stack depth first; `DIV`/`MOD` check the divisor before
popping; `JMP_Z` and `JMP_I` validate targets before consuming the condition or
target; `ALLOC` checks size, overflow, stack room, heap growth, and metadata room
before changing `hp`, `sp`, or the block list; loads/stores validate stack depth,
address range, and result room before touching the heap; `LOCAL_SET` checks the
local index and `RET` checks the frame and result before unwinding. An invalid
opcode or out-of-range `ip` is rejected with no side effects.

A failed step leaves `ip`, `sp`, `fp`, `hp`, `ticks`, all buffers, and all
metadata exactly as they were. Tests assert this for each error class.

### Status codes

`VM_OK`, `VM_HALTED`, `VM_YIELD_MIGRATE`, `VM_ERR_BAD_IP`, `VM_ERR_BAD_OP`,
`VM_ERR_STACK_OVERFLOW`, `VM_ERR_STACK_UNDERFLOW`, `VM_ERR_HEAP_OVERFLOW`,
`VM_ERR_BAD_ADDR`, `VM_ERR_DIV_ZERO`, `VM_ERR_IO`, `VM_ERR_BAD_FRAME`,
`VM_ERR_BAD_LOCAL`, and `VM_ERR_PAYLOAD` (malformed wire data). Use
`vm_status_str()` and `vm_opcode_name()` for printable names.

## 6. Heap model and allocator

`ALLOC` pops a positive size, rounds it up to a multiple of four (checking
overflow), and first searches the free list for a block that fits. A reused
block is split if it is larger than requested; otherwise `hp` is bumped and the
buffer grows. `heap_grow()` starts at `VM_HEAP_INIT_CAP` (4096) and doubles as
needed, so 4096 is an initial chunk, not a ceiling. `FREE` requires a live block
address and marks it dead for later reuse.

Allocator metadata (`VmBlock` array) lives in `VmState` and is serialized along
with everything else, so the free-list shape survives migration. Reachable heap
bytes are exactly `[0, hp)`, which is the region written by `vm_serialize()`.
Multi-byte values use the same big-endian helpers as the wire format, so a heap
integer is represented identically on both sides of a migration.

## 7. Procedures, locals, floats, and I/O notes

`CALL` captures `next_ip` (not the call instruction) and the caller's `fp`, then
sets `fp = sp`. Any values the caller pushed before the call stay below `fp`;
the callee reserves its own locals with `ENTER n`, which pushes `n` zeros at
`[fp, fp+n)`. `LOCAL_GET`/`LOCAL_SET` index only within that reserved region,
and `SET` requires the value being stored to be above the slot (the normal
`ENTER n; PUSH v; SET i` order). `RET` requires a result on the stack, resets
`sp` to `fp`, restores the caller's `fp` and `ip`, and leaves the result on the
caller's stack. `CALL_I` pops its target before pushing the frame; `JMP_I`
validates its target before popping it.

Frames and locals live on the same growable operand stack, so no separate fixed
call stack exists. `FP`, `SP`, and the frame array are all serialized, which is
what makes migration lossless in the middle of a procedure call.

Floats are raw bit patterns in `int64_t` slots; no parallel float stack exists,
so there is no extra state to serialize. Host I/O is intentionally excluded from
the state boundary: after migration, input is read from the new host's stdin.

## 8. Migration and wire format (v3)

Migration is cooperative: a program executes `MIGRATE`, the interpreter yields,
the runtime captures the state, and a fresh VM resumes from the already advanced
`ip`.

### Full snapshot

`vm_serialize()` writes a **32-byte header** followed by the live payload:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | Magic `0x564D4D47` (`"VMMG"`) |
| 4 | 1 | Version (`VM_VERSION` = 3) |
| 5 | 1 | Flags (0 for a full snapshot) |
| 6 | 2 | Reserved (0) |
| 8 | 4 | `ip` (big-endian) |
| 12 | 4 | `sp` (big-endian) |
| 16 | 8 | `hp` (big-endian `uint64`) |
| 24 | 4 | `ticks` (big-endian) |
| 28 | 4 | `fp` (big-endian) |
| 32 | `8*sp` | Operand stack, bottom-to-top, big-endian `int64` |
| `32+8*sp` | 4 | Frame count `fsp` |
| | `8*fsp` | Frames: `ret_ip` (4), `prev_fp` (4) |
| | 4 | Block count |
| | `17*blocks` | Blocks: `addr` (8), `size` (8), `live` (1) |
| | `hp` | Heap bytes `[0, hp)` |

Total size is `32 + 8*sp + 4 + 8*fsp + 4 + 17*blocks + hp`. Calling
`vm_serialize(vm, NULL, 0)` returns the required size without writing; a
non-NULL buffer returns the byte count or `0` if it is too small (or the state's
bounds are invalid).

### Page-level diff

`vm_serialize_diff(vm, base, ...)` writes the same header and non-heap payload,
but sets `VM_FLAG_DIRTY` (`0x01`) and replaces the raw heap bytes with:

| Size | Field |
| ---: | --- |
| 4 | Number of changed pages (big-endian `uint32`) |
| per page: 4 | Page index |
| per page: up to 4096 | Page bytes `[index*4096, min(hp, (index+1)*4096))` |

A page is dirty when it differs from `base` (or `base` is shorter there). All
registers, the stack, frames, and allocator metadata are always included, so
only heap traffic is reduced. `VM_PAGE_SIZE` is 4096.

### Deserialization

`vm_deserialize()` and `vm_deserialize_diff()` share one implementation. They
validate magic, version, flags (only `VM_FLAG_DIRTY` is accepted, and only with a
base), reserved bytes, `fp <= sp`, a sanity cap on `hp` (1 GiB), size arithmetic,
and payload length. The result is assembled in a temporary state and only
installed on success, so a malformed payload leaves the output untouched and
unused stack/heap slots are deterministic.

### Local migration loop

`execute_program()` in `apps/cli/main.c` implements the in-process simulation:
run from a fresh state, and on each `VM_YIELD_MIGRATE`, size, serialize,
deserialize back into the same state, and continue. Each migration logs `ip`,
`sp`, `fp`, `hp`, `frames`, `blocks`, `ticks`, and wire size. A program may yield
zero or many times.

## 9. TCP transport

`include/net.h` and `src/net.c` provide a minimal reliable transport.

| Function | Purpose |
| --- | --- |
| `net_listen(host, port, &listener)` | Passive socket (`port = 0` picks an ephemeral port) |
| `net_listener_port()` | Read the bound port |
| `net_accept()`, `net_connect()` | Accept/establish a connection |
| `net_send_state()`, `net_recv_state()` | Full framed state transfer |
| `net_send_state_diff()`, `net_recv_state_diff()` | Page-level incremental transfer |
| `net_send_halt()` | Send a zero-length frame meaning "peer halted" |
| `net_set_timeout()` | Set `SO_RCVTIMEO`/`SO_SNDTIMEO` (default 10 s) |
| `net_close()`, `net_close_listener()`, `net_last_error()` | Cleanup and diagnostics |

Each frame is a 4-byte big-endian length prefix followed by exactly one wire
payload. Size checks cap frames at `NET_MAX_STATE_BYTES` (64 MiB), and `recv`
loops tolerate partial reads. Sockets use `SO_NOSIGPIPE`/`MSG_NOSIGNAL` where
available so a closed peer produces an error instead of terminating the process.

A **multi-hop** exchange repeats this framing: each peer runs until `MIGRATE` or
`HALT`. On `MIGRATE` it sends its state and then waits to receive the next; on
`HALT` it sends a zero-length frame (`net_send_halt()`), and the receiver gets
`NET_RECV_HALTED` and exits cleanly. With `-d/--diff`, the first frame is a full
snapshot and each peer keeps a clone of the last sent/received state as the base
for subsequent diffs.

Both processes must load the same program. Run the destination first:

```sh
./build/vm-cli -P 43210 -f examples/transform-migrate.asm listen
./build/vm-cli -P 43210 -f examples/transform-migrate.asm connect   # other terminal
```

`connect` and `listen` are the source and destination roles; `run` executes
locally. `-S/--socket` sets the address (default `127.0.0.1`), `-P/--port` is
required for the networked commands, `-t/--trace` annotates each instruction
with its source line, `-D/--debug` adds tick prefixes to program output, and
`-d/--diff` enables incremental heap transfer. `vm_set_debug_ticks()` controls
the same host-side print policy from C; it is deliberately not part of
`VmState` or the wire format.

## 10. Assembler

`src/asm.c` is a line-oriented, two-pass assembler.

- One instruction per line; mnemonics are case-insensitive.
- Comments begin with `;`, `#`, or `//` and extend to end of line, but are ignored
  inside quoted strings.
- Labels are `name:` (letters/digits/underscore, starting with a letter or `_`)
  and may share a line with an instruction.
- `JMP`, `JMP_Z`, and `CALL` accept a label or an expression.
- Operand kinds are none, integer expression, float literal, jump target, and
  local index; missing/illegal operands are reported with line numbers.

### Expressions and constants

`PUSH`, `ENTER`, `LOCAL_GET`/`SET`, `CALL`, jump targets, and numeric data values
accept integer expressions: decimal or `0x` hex literals, `+ - ~`, `+ - * / %`,
parentheses, and previously defined constants. `EQU name expr` defines a
constant. Arithmetic wraps in `uint64_t`; division by zero is an error; and
`-9223372036854775808` is accepted as `INT64_MIN`.

### Data directives and prologue

`DB` (bytes), `DW` (big-endian 16-bit), `DQ` (big-endian 64-bit), `STR` (quoted
string with `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, `\xNN` escapes), and `ALIGN n`
(1..4096) append bytes to a data section. When data is present, the parser
prepends a prologue that allocates `len` bytes at heap address 0 and stores each
byte: three header instructions plus three per data byte. Source instruction
indices are shifted by the prologue length, so labels and numeric jump/CALL
targets still resolve correctly. Programs without data directives produce
exactly the instructions written.

### Pseudo-ops, macros, and line map

`INC`, `DEC`, and `NEG` expand to several real instructions. `GET` and `SET` are
aliases for `LOCAL_GET` and `LOCAL_SET`. Every emitted instruction carries its
source line in the parallel `AsmProgram.lines` array; `asm_line_of(prog, ip)`
returns it (0 when unknown), which is what the CLI's `-t` trace prints.

`asm_parse_string()` and `asm_parse_file()` return 0 on success or -1 with a
human-readable, line-numbered message in `prog.error` (unknown mnemonics,
invalid operands, duplicate/undefined labels, duplicate constants, bad escapes,
out-of-range jumps/targets, file I/O errors). `asm_program_free()` releases the
instruction and line arrays.

## 11. Building and testing

```sh
make all                 # build vm-cli and vm-test
make test                # build and run build/vm-test
make type=RELEASE test   # release build (no sanitizers)
```

The Makefile discovers every `tests/*.c`, compiles it, and links the objects
into `build/vm-test` with the core VM and network sources. DEBUG builds enable
AddressSanitizer and UndefinedBehaviorSanitizer.

- The **VM suite** covers stack ops and shuffling, arithmetic/wraparound,
  division edges, comparisons/bitwise/logical ops, both jump paths and indirect
  jumps, all heap widths and bounds, growth past 4 KiB, free-list reuse,
  calls/locals, floats, I/O, error atomicity, migrate/serialize/deserialize
  round trips, full sizes, and malformed payloads.
- The **assembler suite** covers parsing, comments, labels, expressions and
  constants, every mnemonic, pseudo-ops, data directives and their prologue,
  line mapping, file loading, running parsed programs, and parse errors.
- The **network suite** opens a real loopback listener, transfers small and
  1 MiB states, checks restored states remain executable, tests diff round
  trips and the halt handshake, and exercises bad prefixes, truncated frames,
  invalid versions, and connection failures.

For example:

```sh
./build/vm-cli run -f examples/counter.asm
```

prints the loop's `0..5`, one migration line, and the final `60` before halting.
`examples/features.asm` demonstrates data directives, constants, locals, floats,
and I/O together. `examples/statistics.asm` is a migration-free end-to-end tool
that reads integers from stdin, computes descriptive statistics in a heap array
using two procedures with locals, and selects its report format through an
indirect `CALL_I` dispatch table.

## 12. Current limitations

- TCP migration has no discovery, multiplexing, or application-level retry; it
  does wait through socket timeout wake-ups so slow input is safe. Both peers
  must load the same program and be started in the right order.
- Floats use raw bit patterns with no runtime type tag: mixing integer and float
  ops on the same value is the program's responsibility.
- `LOAD`/`STORE` use big-endian order and require addresses in `[0, hp)`; there
  is no virtual memory or protection model beyond that check.
- Host I/O state (stdin position, open streams) is not part of `VmState`, so a
  program reading input must tolerate resuming on a different host.
- `ip` is 32-bit, bounding the instruction stream; the heap's 1 GiB wire cap is
  a deserialization safety limit rather than a VM-level ceiling.
- The assembler has no conditional assembly, include files, or true macro
  definition facility; its "macros" are fixed pseudo-op expansions.
