# Machine Contract (v2)

> Status: **implemented and frozen**. This document defines the observable
> machine that the assembler, emulator, ROMs, and hosts agree on. ABI v2 replaces
> the transitional core I/O opcodes of v1 with a device page and two port
> opcodes. ROMs from v1 that contain `PRINT*`/`READ*` opcodes are not compatible.

## 0. Decisions

| Question | Decision |
| --- | --- |
| Base architecture | Keep the 64-bit core: 64-bit cells, 64-bit arithmetic, floats, `CALL`/`ENTER`/`LOCAL_*` |
| Address space | 64 KiB, byte-addressed, 16-bit addresses |
| Instruction encoding | 8-bit opcode with fixed, per-opcode immediate widths |
| I/O | Host device callbacks through `DEI`/`DEO`; no I/O opcodes in the core |

Non-goals for this contract: moving a running machine between hosts, dynamic
memory growth, and host-specific I/O policy in the core. The machine is local;
ROMs are its distribution unit.

## 1. Design principles

1. **Implementation-first.** A feature is only added if the cost of implementing
   it in a second emulator remains small.
2. **Frozen ABI.** Opcode numbers, memory map, image format, and device ports do
   not change casually. This is what lets ROMs outlive their assembler.
3. **ROM is the distribution unit.** Programs are shipped as byte images, not as
   text that must be re-parsed by every host.
4. **Thin host boundary.** The core knows only bytes, stacks, RAM, and two port
   opcodes. Screens, keyboards, and files belong to host devices.

## 2. Numbers and representation

- A **cell** is 64 bits (`int64_t`). Two's-complement wrapping arithmetic is
  used for `ADD`, `SUB`, `MUL`, shifts, and bitwise ops.
- Floats are **raw IEEE-754 bit patterns** in cells; the VM has no type tags.
- Addresses and instruction pointers are **16-bit unsigned** (`uint16_t`).
- All multi-byte values — immediates and RAM values — are **big-endian**.
- `ticks` is a 64-bit count of successfully executed instructions, including
  `HALT` and device-driven halts.

## 3. Machine state

The state consists of registers, one operand stack, call-frame metadata,
allocator metadata, addressable RAM, and host device callbacks. Stacks and
metadata live **outside** addressable RAM, as in Uxn; only `mem` is reachable by
load/store instructions.

| Field | Width | Meaning |
| --- | --- | --- |
| `ip` | 16-bit | Byte address of the next instruction in `mem` |
| `sp` | 16-bit | Number of live cells on the operand stack (0..256) |
| `fp` | 16-bit | Index in the stack of the current frame's first local |
| `fsp` | 16-bit | Number of live call frames (0..64) |
| `hp` | 16-bit | Allocator bump pointer into the heap region; initial value `0x4000` |
| `ticks` | 64-bit | Successfully executed instructions |
| `dev` | pointers | Host read/write callbacks plus an opaque context |

Device callbacks are host wiring, not machine state: `vm_free()` and image
loading preserve callbacks installed after `vm_init()`. A fresh `vm_init()`
starts with no device callbacks, so a host installs them before running a ROM.

Fixed capacities:

- operand stack: **256 cells** (2048 bytes outside RAM)
- call frames: **64** entries (`ret_ip`, `prev_fp`)
- allocator blocks: **64** entries (`addr`, `size`, `live`)
- addressable RAM: **65536 bytes** (`mem[0x0000..0xFFFF]`)
- device ports: **256** entries outside RAM

No buffer grows at runtime. Overflow, underflow, and exhausted metadata are
errors, not wrap-around behaviour.

## 4. Memory map

```
0x0000 ─────────────── zero page (addressable, 256 bytes)
0x0100 ─────────────── ROM image: code + static data
0x3FFF ─────────────── end of image region
0x4000 ─────────────── heap start (hp initial value)
0xFFFF ─────────────── end of RAM

device page: 256 ports, outside RAM, addressed only by DEI/DEO
```

- The image loader writes the ROM payload at `0x0100`. Maximum payload size is
  `0x3F00` bytes (16128).
- `0x0000..0x00FF` is ordinary addressable RAM, intended for frequently used
  variables. It is not pre-filled by the ROM.
- `ALLOC` returns addresses in `0x4000..0xFFFF`; the heap grows upward from
  `0x4000`. Allocation sizes are rounded up to a multiple of 8.
- Ports are **not** RAM addresses: `DEI`/`DEO` immediates select host devices,
  and no load/store instruction can reach them.

## 5. Instruction encoding

Every instruction starts with one opcode byte. Assigned opcodes are explicit
because retired I/O opcodes remain reserved:

- `0x00..0x40`, `0x46..0x7F`: current core opcodes and reserved slots.
- `0x0E` and `0x41..0x45`: permanently unassigned (retired v1 I/O opcodes).
- `0x80`: `DEI` (device input).
- `0x81`: `DEO` (device output).
- `0x82..0xFF`: reserved for future devices/vectors.

Immediate widths are fixed per opcode:

| Immediate | Opcodes | Total bytes | Encoding |
| --- | --- | ---: | --- |
| none | all others | 1 | — |
| 1 byte | `ENTER` (0x2E), `LOCAL_GET` (0x2F), `LOCAL_SET` (0x30), `DEI` (0x80), `DEO` (0x81) | 2 | unsigned index/port 0..255 |
| 2 bytes | `JMP` (0x09), `JMP_Z` (0x0A), `CALL` (0x2B) | 3 | big-endian absolute byte address |
| 8 bytes | `PUSH` (0x02), `PUSHF` (0x32) | 9 | big-endian `int64` / raw 64-bit double |

Fetching an instruction means reading `mem[ip]`, determining its immediate
width from this table, advancing `ip` by `1 + width`, and decoding the immediate
bytes from `mem`. Jump and call targets are absolute byte addresses, not
instruction indices.

`CALL_I` (0x2C) and `JMP_I` (0x31) are single-byte opcodes that take their target
from the stack.

## 6. Execution semantics

- `HALT` (0x00) leaves `ip` on the halt instruction and returns `VM_HALTED`.
- `RET` (0x2D) pops a result, restores `ip` and `fp` from the top frame, and
  leaves the result on the caller's stack.
- `CALL` (0x2B) pushes `{return_ip, fp}` and jumps; `ENTER n` reserves `n` zeroed
  locals at `[fp, fp+n)`; `LOCAL_GET`/`LOCAL_SET` index only that region.
- `ALLOC` pops a positive size, rounds to 8, reuses a dead block when one fits,
  otherwise bumps `hp`; `FREE` marks a live block dead.
- Loads and stores may address **any** byte in RAM: the full access width must
  fit in `0x0000..0xFFFF`. This lets programs read static data directly from the
  image region. `hp` constrains only `ALLOC`/`FREE`, not ordinary loads/stores.
  Multi-byte values use big-endian byte order.
- `DEI port` checks stack room, reads one cell from the host, then pushes it.
- `DEO port` requires a cell, writes it to the host, then pops it. A callback may
  return `VM_OK`, `VM_HALTED`, or `VM_ERR_IO`.

`PRINT`, `FPRINT`, `PRINT_STR`, `PRINT_CHAR`, `READ_INT`, and `READ_STR` are
**assembler pseudo-ops**, not opcodes. They expand to device port accesses in
the payload.

## 7. Reset and halt

On load:

1. all of `mem` is zeroed;
2. stacks, frames, and block metadata are zeroed; `sp = fp = fsp = 0`;
3. `hp = 0x4000`;
4. `ip` is set to the image entry point (normally `0x0100`);
5. `ticks = 0`;
6. installed device callbacks are preserved.

Execution proceeds until `HALT`, a device halt, or an error. There is no
interrupt mechanism in this contract; event vectors are reserved for a future
device extension.

## 8. Image format

ROM files use a 16-byte header followed by the payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `56 4D 49 4D` (`"VMIM"`) |
| 4 | 1 | Format version, currently `2` |
| 5 | 1 | Flags, must be `0` |
| 6 | 2 | Reserved, must be `0` |
| 8 | 2 | Payload length, big-endian, `0..0x3F00` |
| 10 | 2 | Entry point, big-endian, normally `0x0100` |
| 12 | 4 | Reserved, must be `0` |

The loader copies exactly `payload length` bytes to `mem[0x0100..]`, zeroes the
remainder of RAM, and sets `ip` to the entry point. The entry point must lie
inside the loaded payload; otherwise loading fails.

## 9. Errors

The machine is **not** total: invalid opcodes, bad jump targets, stack
overflow/underflow, bad addresses, allocation failures, and device failures
return the existing `VM_ERR_*` statuses. A failed instruction leaves all VM
state unchanged (atomic error handling), which is required by the test suite and
is valuable for debuggers. Uxn's "any byte sequence is a valid program" property
is deliberately not adopted.

A device callback that changes host state is a side effect outside `VmState`;
callbacks must not mutate VM state unless they can complete successfully. On a
device halt, `ip` stays on the `DEI`/`DEO` instruction and `ticks` increments;
`DEO` consumes its cell, while `DEI` does not push a result.

## 10. Host boundary

The core depends on no platform headers and performs no I/O itself. A host
provides:

- a ROM image to load;
- a step/run loop;
- device callbacks with this shape:

```c
VmStatus read(void *ctx, VmState *vm, uint8_t port, int64_t *out);
VmStatus write(void *ctx, VmState *vm, uint8_t port, int64_t value);
```

Callbacks return `VM_OK`, `VM_HALTED`, or `VM_ERR_IO`. The callback context and
all formatting/input policy belong to the host. The CLI supplies these ports:

| Port | Name | Access | Meaning |
| ---: | --- | --- | --- |
| `0x00` | `VM_SYS_VERSION` | read | ROM format version |
| `0x01` | `VM_SYS_DEBUG` | read/write | tick-prefix flag |
| `0x02` | `VM_SYS_HALT` | write | halt with `VM_HALTED` |
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

Other hosts may assign different meanings to other ports; a ROM that depends on
a port must also depend on the host providing it.

Read ports are synchronous and blocking: `GETC`, `GETI`, and `GETS` wait for host
input and have no ready/status port. `GETC` reports EOF as `0`; `GETI` reports
EOF, unparseable input, and overflow as `0`; `GETS` reports EOF as an empty
line. Stream errors return `VM_ERR_IO`.

Instruction tracing (`-t`) and the `-D` tick prefix are host policies, not
machine state. `-D` enables `VM_SYS_DEBUG`, which the Console device reads when
formatting output.

## 11. Implementation status

The current codebase conforms to this contract:

1. Programs are bytes in `mem`; `vm_step` fetches and decodes from `ip`.
2. `VmState` contains fixed arrays for RAM, stack, frames, and block metadata;
   the core never calls `realloc` and contains no stdio calls.
3. `ip`, jump/call targets, `CALL` return addresses, and assembler labels are
   absolute 16-bit byte addresses.
4. The assembler emits a v2 `VMIM` ROM and maps source lines by absolute byte
   address. Static data stays in the image region; the image is not copied into
   the heap.
5. Core I/O opcodes are gone. `DEI`/`DEO` are the only I/O instructions, and
   `PRINT*`/`READ*` are assembler pseudo-ops that expand to Console ports.
6. `vm-cli run` accepts assembly source or a ROM, `vm-cli build` writes a ROM,
   `vm-cli dis` decodes source or a ROM (labelling reserved bytes and reporting
   truncated immediates), and the CLI installs System/Console device callbacks.
7. Tests cover byte-encoded programs, ROM loading, device calls, device errors,
   device halts, retired opcode behavior, an executable conformance ROM, and
   disassembler decoding, including reserved and truncated encodings.
