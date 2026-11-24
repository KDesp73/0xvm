# Machine Contract (draft v1)

> Status: **proposed, not yet implemented**. This document defines the observable
> machine that future assemblers, emulators, and ROMs must agree on. It is the
> first step toward an Uxn-shaped platform: a frozen core, a binary image format,
> and a thin host layer.

## 0. Decisions

| Question | Decision |
| --- | --- |
| Base architecture | Keep the 64-bit core: 64-bit cells, 64-bit arithmetic, floats, `CALL`/`ENTER`/`LOCAL_*` |
| Address space | 64 KiB, byte-addressed, 16-bit addresses |
| Instruction encoding | 8-bit opcode with fixed, per-opcode immediate widths |

Non-goals for this contract: migration, networking, state serialization, dynamic
memory growth, and host-specific I/O policy. The machine is local and has no
serialized-state ABI.

## 1. Design principles

1. **Implementation-first.** A feature is only added if the cost of implementing
   it in a second emulator remains small.
2. **Frozen ABI.** Opcode numbers, memory map, and image format do not change
   casually. This is what lets ROMs outlive their assembler.
3. **ROM is the distribution unit.** Programs are shipped as byte images, not as
   text that must be re-parsed by every host.
4. **Thin host boundary.** The core knows only bytes, stacks, and RAM. Screens,
   keyboards, and files belong to a device layer added in a later step.

## 2. Numbers and representation

- A **cell** is 64 bits (`int64_t`). Two's-complement wrapping arithmetic is
  used for `ADD`, `SUB`, `MUL`, shifts, and bitwise ops.
- Floats are **raw IEEE-754 bit patterns** in cells; the VM has no type tags.
- Addresses and instruction pointers are **16-bit unsigned** (`uint16_t`).
- All multi-byte values — immediates and heap values — are **big-endian**.
- `ticks` is a 64-bit count of successfully executed instructions.

## 3. Machine state

The state consists of registers, one operand stack, call-frame metadata,
allocator metadata, and addressable RAM. Stacks and metadata live **outside**
addressable RAM, as in Uxn; only `mem` is reachable by load/store instructions.

| Field | Width | Meaning |
| --- | --- | --- |
| `ip` | 16-bit | Byte address of the next instruction in `mem` |
| `sp` | 16-bit | Number of live cells on the operand stack (0..256) |
| `fp` | 16-bit | Index in the stack of the current frame's first local |
| `fsp` | 16-bit | Number of live call frames (0..64) |
| `hp` | 16-bit | Allocator bump pointer into the heap region; initial value `0x4000` |
| `ticks` | 64-bit | Successfully executed instructions |

Fixed capacities:

- operand stack: **256 cells** (2048 bytes outside RAM)
- call frames: **64** entries (`ret_ip`, `prev_fp`)
- allocator blocks: **64** entries (`addr`, `size`, `live`)
- addressable RAM: **65536 bytes** (`mem[0x0000..0xFFFF]`)

No buffer grows at runtime. Overflow, underflow, and exhausted metadata are
errors, not wrap-around behaviour.

## 4. Memory map

```
0x0000 ─────────────── zero page (addressable, 256 bytes)
0x0100 ─────────────── ROM image: code + static data
0x3FFF ─────────────── end of image region
0x4000 ─────────────── heap start (hp initial value)
0xFFFF ─────────────── end of RAM
```

- The image loader writes the ROM payload at `0x0100`. Maximum payload size is
  `0x3F00` bytes (16128).
- `0x0000..0x00FF` is ordinary addressable RAM, intended for frequently used
  variables. It is not pre-filled by the ROM.
- `ALLOC` returns addresses in `0x4000..0xFFFF`; the heap grows upward from
  `0x4000`. Allocation sizes are rounded up to a multiple of 8.
- The **device page** (256 ports, 16 devices × 16 ports) is reserved outside
  addressable RAM for the later device step. It is not part of this contract's
  implementation yet.

## 5. Instruction encoding

Every instruction starts with one opcode byte. Opcodes `0x00..0x45` are assigned
to the current instruction set in `VmOpcode` order; `0x46..0x7F` are reserved;
`0x80..0xFF` are reserved for device/vector operations to be defined later.

Immediate widths are fixed per opcode:

| Immediate | Opcodes | Total bytes | Encoding |
| --- | --- | ---: | --- |
| none | all others | 1 | — |
| 1 byte | `ENTER` (0x2E), `LOCAL_GET` (0x2F), `LOCAL_SET` (0x30) | 2 | unsigned index 0..255 |
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
- `PRINT`, `PRINT_STR`, `PRINT_CHAR`, `READ_INT`, and `READ_STR` remain core
  opcodes **temporarily**. They are transitional: step 3 of the roadmap moves
  them onto the Console device as port reads/writes.

## 7. Reset and halt

On load:

1. all of `mem` is zeroed;
2. stacks, frames, and block metadata are zeroed; `sp = fp = fsp = 0`;
3. `hp = 0x4000`;
4. `ip` is set to the image entry point (normally `0x0100`);
5. `ticks = 0`.

Execution proceeds until `HALT` or an error. There is no interrupt mechanism in
this contract; event vectors are introduced with the device layer.

## 8. Image format

ROM files use a 16-byte header followed by the payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `56 4D 49 4D` (`"VMIM"`) |
| 4 | 1 | Format version, currently `1` |
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
overflow/underflow, bad addresses, and allocation failures return the existing
`VM_ERR_*` statuses. A failed instruction leaves all state unchanged (atomic
error handling), which is required by the current test suite and is valuable for
debuggers. Uxn's "any byte sequence is a valid program" property is deliberately
not adopted.

## 10. Host boundary

The core depends on no platform headers and performs no direct I/O except the
transitional opcodes listed in §6. A host provides:

- a ROM image to load;
- a step/run loop;
- (later) device callbacks for console, screen, controller, and files.

Printing with tick prefixes (`-D`) and instruction tracing (`-t`) are host
policies, not machine state.

## 11. Implementation impact

To conform, the current codebase will need these changes:

1. Replace the `VmInst[]` program with bytes in `mem`; `vm_step` fetches and
   decodes from `ip`.
2. Replace growable buffers with fixed arrays in `VmState`; remove `realloc`
   from the core.
3. Change `ip`/jump targets from instruction indices to byte addresses.
4. Make the assembler emit a `.rom` image with the header above, and key its line
   map by byte address instead of instruction index. Static data can stay in the
   image region and be addressed directly; the current prologue that copies data
   into the heap is no longer required.
5. Add a ROM loader to `vm-cli`; rebuild the examples as ROMs.
6. Adapt tests to byte-encoded programs, then add a conformance image that any
   emulator implementation must pass.

Until step 3 of the roadmap lands, `PRINT`/`READ_*` stay as core opcodes so
existing programs continue to run.
