# vm

> A small, standalone C stack VM in the spirit of Uxn.
> Written by KDesp73 (Konstantinos Despoinidis)

The machine is frozen: 64-bit cells, 64 KiB of byte-addressed RAM, 16-bit byte
addresses, one-byte opcodes with fixed immediates, and a `VMIM` binary image
format. See the [Machine Contract](docs/Machine-Contract.md) for the ABI.

## Build

```sh
make all         # build build/vm-cli and build/vm-test
make test        # build and run the test suite (ASan/UBSan in DEBUG builds)
```

## Run programs

`vm-cli run` accepts either assembly source or a `VMIM` ROM:

```sh
./build/vm-cli run -f examples/counter.asm
./build/vm-cli run -f counter.rom
./build/vm-cli -t run -f examples/gcd.asm     # -t traces address/line/opcode
./build/vm-cli -D run -f examples/counter.asm # -D prefixes output with tick numbers
```

`vm-cli build` assembles source into a ROM. With no `-o`, `.asm` is replaced by
`.rom`:

```sh
./build/vm-cli build -f examples/counter.asm -o counter.rom
```

Flags: `-f/--file` selects the input file, `-o/--output` sets the ROM output
path for `build`, `-t/--trace` prints each executed instruction with its source
line, `-D/--debug` prefixes program output with `[vm tick=N]`, `-v/--version`
prints the version, and `-h/--help` prints usage. Use `run` or `build` as the
command.

## Assembly model

One instruction per line; mnemonics are case-insensitive. Labels are written
`name:` (optionally followed by an instruction on the same line) and resolve to
absolute 16-bit **byte addresses** in VM RAM. Comments start with `;`, `#`, or
`//` and are ignored inside quoted strings. `JMP`, `JMP_Z`, and `CALL` accept a
label or an absolute byte address.

Instructions are encoded as a one-byte opcode plus a fixed immediate: 0 bytes
for most opcodes, 1 byte for locals/`ENTER`, 2 bytes for direct jumps/calls, or
8 bytes for `PUSH`/`PUSHF`. Multi-byte immediates and RAM values are
**big-endian**.

Operands are **integer expressions**: decimal, `0x`-prefixed hex, the unary
operators `+ - ~`, the binary operators `+ - * / %`, parentheses, and constants
defined with `EQU name expr`. For example:

```asm
EQU N 4
PUSH N*2 + (BASE-8)
```

`PUSHF` takes a floating-point literal and emits the raw IEEE-754 bit pattern.

Data directives append bytes to the static-data area of the image, immediately
after a generated three-byte `JMP` over the data:

| Directive | Meaning |
| --- | --- |
| `DB 1, 2, 0x41` | one byte per expression |
| `DW expr, ...` | big-endian 16-bit words |
| `DQ expr, ...` | big-endian 64-bit words |
| `STR "hi\n"` | quoted string with `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, `\xNN` escapes |
| `ALIGN 8` | pad the data area to the requested offset (1..4096) |

Labels may name data addresses, and `LOAD*`/`STORE*` can read or write the
image region directly. The image is not copied into the heap. The image starts
at `0x0100` and is limited to `0x3F00` bytes; `0x0000..0x00FF` is the zero
page, and `0x4000..0xFFFF` is the heap region.

Pseudo-ops expand to real instructions: `INC` / `DEC` / `NEG`, and `GET` / `SET`
are aliases for `LOCAL_GET` / `LOCAL_SET`. `asm_line_of()` maps an absolute RAM
byte address back to its source line, which is what `-t` uses in its trace.

### Instruction groups

- **Core:** `HALT`, `NOP`, `PUSH`, `POP`, `DUP`, `ADD`, `SUB`, `MUL`, `CMP_LT`,
  `JMP`, `JMP_Z` (absolute byte target), `ALLOC`, `LOAD32`, `STORE32`, `PRINT`.
- **Arithmetic & logic:** `DIV`, `MOD`, all `CMP_*`, `AND`/`OR`/`XOR`/`NOT`,
  `SHL`/`SHR`, `LNOT`/`LAND`/`LOR`.
- **Stack shuffling:** `SWAP`, `OVER`, `ROT`, `PICK`.
- **RAM:** `LOAD8`/`STORE8`, `LOAD16`/`STORE16`, `LOAD32U`, `LOAD64`/`STORE64`.
- **Heap:** `ALLOC` and `FREE` for fixed-capacity block reuse.
- **Procedures:** `CALL` (label/expression), `CALL_I` (indirect), `RET`,
  `ENTER n` (reserve locals), `LOCAL_GET`/`LOCAL_SET` (`GET`/`SET`), `JMP_I`.
- **Floats:** `PUSHF`, `FADD`/`FSUB`/`FMUL`/`FDIV`, `FNEG`/`FABS`/`FSQRT`,
  `FCMP_*`, `FTOI`/`ITOF`, `FPRINT`.
- **Host I/O:** `PRINT_STR`, `PRINT_CHAR`, `READ_INT`, `READ_STR` (transitional
  until the device layer lands).

`ALLOC` returns a RAM address starting at `0x4000`. Loads and stores may address
any byte in RAM; only `ALLOC`/`FREE` are constrained to the heap. The machine
has fixed capacities: 256 stack cells, 64 call frames, 64 allocator blocks, and
65536 bytes of RAM. Nothing in the core grows at runtime, and a failed
instruction leaves all VM state unchanged.

`./build/vm-cli run -f examples/features.asm` exercises data directives,
constants, procedures/locals, floats, and I/O in one program.

`examples/statistics.asm` is a complete tool: it reads a count, then that many
integers, then a report mode (`0` short, `1` full), and prints min, max, sum,
mean, variance, and standard deviation. It uses a heap array, free-list reuse,
two procedures with locals, floating-point arithmetic, and an indirect `CALL_I`
dispatch table:

```sh
printf '5\n10\n20\n30\n40\n50\n1\n' | ./build/vm-cli run -f examples/statistics.asm
```

See [VM Implementation](docs/VM-Implementation.md) for instruction semantics,
execution rules, and error-atomicity guarantees. The implemented machine ABI —
memory map, instruction encoding, and ROM format — is in the
[Machine Contract](docs/Machine-Contract.md).
