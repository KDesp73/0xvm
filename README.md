# process-migration

> A small C stack VM built to study lossless execution migration.
> Written by KDesp73 (Konstantinos Despoinidis)

## Build

```sh
make all         # build build/vm-cli and build/vm-test
make test        # build and run the test suite (ASan/UBSan in DEBUG builds)
```

## Running an assembly program

```sh
./build/vm-cli run -f examples/counter.asm
./build/vm-cli -t run -f examples/gcd.asm     # -t traces ip/source line/opcode
```

One instruction per line; mnemonics are case-insensitive. Labels are written
`name:` (optionally followed by an instruction on the same line) and are
resolved to instruction indices. Comments start with `;`, `#`, or `//` and are
ignored inside quoted strings. `JMP`, `JMP_Z`, and `CALL` accept either a label
or an absolute instruction index.

Operands are **integer expressions**: decimal, `0x`-prefixed hex, the unary
operators `+ - ~`, the binary operators `+ - * / %`, parentheses, and constants
defined with `EQU name expr`. For example:

```asm
EQU N 4
PUSH N*2 + (BASE-8)
```

`PUSHF` takes a floating-point literal and emits the raw IEEE-754 bit pattern.

Data directives append bytes to a data section that is loaded into the heap at
address `0` by an automatically generated prologue (only emitted when data is
used):

| Directive | Meaning |
| --- | --- |
| `DB 1, 2, 0x41` | one byte per expression |
| `DW expr, ...` | big-endian 16-bit words |
| `DQ expr, ...` | big-endian 64-bit words |
| `STR "hi\n"` | quoted string with `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, `\xNN` escapes |
| `ALIGN 8` | pad the data section with zero bytes to a multiple of N (1..4096) |

Pseudo-ops expand to real instructions: `INC` / `DEC` / `NEG`, and `GET` / `SET`
are aliases for `LOCAL_GET` / `LOCAL_SET`. `asm_line_of()` maps an instruction
index back to its source line, which is what `-t` uses in its trace output.

### Instruction groups

- **Core:** `HALT`, `NOP`, `PUSH`, `POP`, `DUP`, `ADD`, `SUB`, `MUL`, `CMP_LT`,
  `JMP`, `JMP_Z`, `ALLOC`, `LOAD32`, `STORE32`, `PRINT`, `MIGRATE`.
- **Arithmetic & logic:** `DIV`, `MOD`, all `CMP_*`, `AND`/`OR`/`XOR`/`NOT`,
  `SHL`/`SHR`, `LNOT`/`LAND`/`LOR`.
- **Stack shuffling:** `SWAP`, `OVER`, `ROT`, `PICK`.
- **Heap:** `LOAD8`/`STORE8`, `LOAD16`/`STORE16`, `LOAD32U`, `LOAD64`/`STORE64`,
  and `FREE` for block reuse.
- **Procedures:** `CALL` (label/expression), `CALL_I` (indirect), `RET`,
  `ENTER n` (reserve locals), `LOCAL_GET`/`LOCAL_SET` (`GET`/`SET`), `JMP_I`.
- **Floats:** `PUSHF`, `FADD`/`FSUB`/`FMUL`/`FDIV`, `FNEG`/`FABS`/`FSQRT`,
  `FCMP_*`, `FTOI`/`ITOF`, `FPRINT`.
- **Host I/O:** `PRINT_STR`, `PRINT_CHAR`, `READ_INT`, `READ_STR`.

All heap addresses are 64-bit offsets; multi-byte heap values use big-endian
byte order. `ALLOC` returns a heap offset and the heap grows on demand. The
operand stack and call-frame stacks grow on demand too.

`./build/vm-cli -f examples/features.asm run` exercises data directives,
constants, procedures/locals, floats, and I/O in one program.

`examples/statistics.asm` is a complete, migration-free tool: it reads a count,
then that many integers, then a report mode (`0` short, `1` full), and prints
min, max, sum, mean, variance, and standard deviation. It uses a heap array,
free-list reuse, two procedures with locals, floating-point arithmetic, and an
indirect `CALL_I` dispatch table:

```sh
printf '5\n10\n20\n30\n40\n50\n1\n' | ./build/vm-cli -f examples/statistics.asm run
```

## Migration

`MIGRATE` yields at a clean instruction boundary. The runtime serializes the
**entire** mutable VM state — registers, operand stack, call frames, allocator
metadata, and live heap bytes — so execution resumes losslessly. Program code is
not part of the state; both sides load the same source file.

| Command | Role |
| --- | --- |
| `run` | Run locally; `MIGRATE` is handled as an in-process serialize/deserialize round trip |
| `listen` | Wait for a peer, restore the initial state, and keep exchanging until the VM halts |
| `connect` | Connect to a listener, run until `MIGRATE`, and hand off state |

The TCP handoff is **multi-hop**: each `MIGRATE` after a restore is forwarded to
the peer, so peers alternate execution until `HALT`. A zero-length frame is a
graceful "peer halted" signal so the idle side exits cleanly.

```sh
# Terminal 1 (destination)
./build/vm-cli -P 43210 -f examples/transform-migrate.asm listen

# Terminal 2 (source)
./build/vm-cli -P 43210 -f examples/transform-migrate.asm connect
```

Flags: `-S/--socket` sets the bind/connect address (default `127.0.0.1`),
`-P/--port` is required for `listen`/`connect`, `-t/--trace` prints each
executed instruction with its source line, `-D/--debug` prefixes program output
with `[vm tick=N]` (off by default), and `-d/--diff` sends full state on the
first hop and page-level (4 KiB) heap diffs thereafter. The serialized
format is wire **v3**: a 32-byte header followed by the live stack, frame count
and frames, block count and allocator blocks, and heap bytes. Diff payloads set
`VM_FLAG_DIRTY` and carry only changed heap pages.

See [VM Implementation](docs/VM-Implementation.md) for the exact layout,
instruction semantics, and error-atomicity rules.
