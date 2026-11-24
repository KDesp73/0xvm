#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>

/* Fixed machine geometry (see docs/Machine-Contract.md). */
#define VM_RAM_SIZE        0x10000u  /* 64 KiB, byte addresses 0x0000..0xFFFF */
#define VM_ZERO_PAGE_SIZE  0x0100u
#define VM_IMAGE_START     0x0100u
#define VM_IMAGE_MAX       0x3F00u   /* 0x4000 - 0x0100 */
#define VM_HEAP_START      0x4000u
#define VM_STACK_SIZE      256u      /* cells, outside addressable RAM */
#define VM_FRAME_COUNT     64u
#define VM_BLOCK_COUNT     64u

/* ROM header: 16 bytes, big-endian multi-byte fields. */
#define VM_ROM_HEADER_SIZE 16u
#define VM_ROM_VERSION     1u

typedef enum {
    OP_HALT = 0,
    OP_NOP,
    OP_PUSH,      /* 8-byte immediate: int64_t value */
    OP_POP,
    OP_DUP,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_CMP_LT,
    OP_JMP,       /* 2-byte immediate: absolute byte address */
    OP_JMP_Z,     /* 2-byte immediate: absolute byte address */
    OP_ALLOC,     /* pops size (int64_t), pushes heap address (uint16_t) */
    OP_LOAD32,    /* pops addr, pushes sign-extended int32 from RAM */
    OP_STORE32,   /* pops addr (top), pops val, writes low 32 bits */
    OP_PRINT,     /* pops val (int64_t), prints it */

    /* Arithmetic extension. All binary ops pop b (top) then a. */
    OP_DIV,       /* a / b, truncated toward zero; INT64_MIN / -1 wraps */
    OP_MOD,       /* a % b, sign of a; INT64_MIN % -1 is 0 */
    OP_CMP_EQ,    /* a == b */
    OP_CMP_NE,    /* a != b */
    OP_CMP_LE,    /* a <= b */
    OP_CMP_GT,    /* a > b */
    OP_CMP_GE,    /* a >= b */
    OP_AND,       /* bitwise 64-bit AND */
    OP_OR,        /* bitwise 64-bit OR */
    OP_XOR,       /* bitwise 64-bit XOR */
    OP_NOT,       /* bitwise 64-bit complement (unary) */
    OP_SHL,       /* a << (b & 63), unsigned shift */
    OP_SHR,       /* a >> (b & 63), logical unsigned shift */
    OP_LNOT,      /* logical NOT: 0 -> 1, nonzero -> 0 (unary) */
    OP_LAND,      /* logical AND: (a != 0) && (b != 0) */
    OP_LOR,       /* logical OR: (a != 0) || (b != 0) */

    /* Stack shuffling */
    OP_SWAP,      /* [..., a, b] -> [..., b, a] */
    OP_OVER,      /* [..., a, b] -> [..., a, b, a] */
    OP_ROT,       /* [..., a, b, c] -> [..., b, c, a] */
    OP_PICK,      /* pops n (top), pushes stack[sp-1-n]; n = 0 duplicates top */

    /* RAM access. All accessors honor [0, 0x10000) for their width. */
    OP_LOAD8,     /* pops addr, pushes unsigned byte */
    OP_STORE8,    /* pops addr (top), pops val, writes low 8 bits */
    OP_LOAD16,    /* pops addr, pushes unsigned 16-bit value */
    OP_STORE16,   /* pops addr (top), pops val, writes low 16 bits */
    OP_LOAD32U,   /* pops addr, pushes zero-extended uint32 */
    OP_LOAD64,    /* pops addr, pushes int64_t (raw bits) */
    OP_STORE64,   /* pops addr (top), pops val, writes all 64 bits */

    /* Heap reuse. Allocator metadata lives in VmState. */
    OP_FREE,      /* pops addr; returns its block to the free list */

    /* Procedures. CALL pushes {return byte address, frame pointer}. */
    OP_CALL,      /* 2-byte immediate: absolute byte address */
    OP_CALL_I,    /* pops target byte address from the stack */
    OP_RET,       /* pops result, unwinds frame, pushes result */
    OP_ENTER,     /* 1-byte immediate: reserve n zeroed locals */
    OP_LOCAL_GET, /* 1-byte immediate: push stack[fp + index] */
    OP_LOCAL_SET, /* 1-byte immediate: pop value into stack[fp + index] */

    OP_JMP_I,     /* pops target byte address and jumps */

    /* IEEE-754 doubles: the stack stays int64_t, values are raw bit patterns. */
    OP_PUSHF,     /* 8-byte immediate: raw 64-bit double pattern */
    OP_FADD,
    OP_FSUB,
    OP_FMUL,
    OP_FDIV,
    OP_FNEG,
    OP_FABS,
    OP_FSQRT,
    OP_FCMP_LT,
    OP_FCMP_EQ,
    OP_FCMP_LE,
    OP_FCMP_GT,
    OP_FCMP_GE,
    OP_FTOI,      /* double -> int64_t (truncating; NaN/overflow saturates) */
    OP_ITOF,      /* int64_t -> double */
    OP_FPRINT,    /* pops raw bits, prints the double */

    /* Host I/O. Transitional until the Console device layer lands. */
    OP_PRINT_STR, /* pops len (top), pops addr; prints RAM bytes [addr, addr+len) */
    OP_PRINT_CHAR,/* pops codepoint and prints one byte */
    OP_READ_INT,  /* reads one integer from stdin and pushes it (0 on bad input) */
    OP_READ_STR,  /* pops max (top), pops addr; reads a line into RAM, pushes len */

    OP_COUNT      /* sentinel: number of opcodes; values 0x46..0x7F are reserved */
} VmOpcode;

typedef enum {
    VM_OK = 0,
    VM_HALTED,
    VM_ERR_BAD_IP,
    VM_ERR_BAD_OP,
    VM_ERR_STACK_OVERFLOW,
    VM_ERR_STACK_UNDERFLOW,
    VM_ERR_HEAP_OVERFLOW,
    VM_ERR_BAD_ADDR,
    VM_ERR_DIV_ZERO,
    VM_ERR_IO,
    VM_ERR_BAD_FRAME,
    VM_ERR_BAD_LOCAL
} VmStatus;

typedef struct {
    uint16_t ret_ip;   /* byte address to resume at after RET */
    uint16_t prev_fp;  /* caller's frame pointer */
} VmFrame;

typedef struct {
    uint16_t addr;
    uint16_t size;     /* allocated size, rounded up to 8 */
    uint8_t  live;     /* 1 = in use, 0 = on the free list */
} VmBlock;

/* The whole machine lives in this struct. No field owns heap memory. */
typedef struct {
    uint16_t ip;       /* byte address of the next instruction */
    uint16_t sp;       /* live cells on the operand stack */
    uint16_t fp;       /* index of the current frame's first local */
    uint16_t fsp;      /* live call frames */
    uint16_t hp;       /* allocator bump pointer; starts at VM_HEAP_START */
    uint64_t ticks;    /* successfully executed instructions */

    int64_t  stack[VM_STACK_SIZE];
    uint8_t  mem[VM_RAM_SIZE];
    VmFrame  frames[VM_FRAME_COUNT];
    VmBlock  blocks[VM_BLOCK_COUNT];
    uint16_t block_count;
} VmState;

void vm_init(VmState *vm);
/* Reset to the post-init state. No owned buffers exist; kept for host symmetry. */
void vm_free(VmState *vm);

/* Load a raw payload at VM_IMAGE_START and jump to `entry` (usually VM_IMAGE_START).
 * `entry` must be inside the payload. Malformed images return VM_ERR_BAD_IP. */
VmStatus vm_load_image(VmState *vm, const uint8_t *payload,
                       size_t payload_len, uint16_t entry);

/* Load a 16-byte VMIM header plus payload. The payload length must fit
 * VM_IMAGE_MAX and the total buffer must be exactly header+payload bytes. */
VmStatus vm_load_rom(VmState *vm, const uint8_t *rom, size_t rom_len);

VmStatus vm_step(VmState *vm);
VmStatus vm_run(VmState *vm, uint64_t max_steps);

/* Total encoded size (opcode + immediates) of an opcode, or 1 if unknown. */
unsigned vm_inst_size(uint8_t op);

const char *vm_status_str(VmStatus status);
const char *vm_opcode_name(uint8_t op);

/* When enabled, PRINT/FPRINT/PRINT_STR prefix program output with the current
 * tick count. This is host output policy only. */
void vm_set_debug_ticks(int enabled);

/* IEEE-754 bit conversion helpers (memcpy-based, no strict-aliasing UB). */
double  vm_bits_to_f64(int64_t bits);
int64_t vm_f64_to_bits(double value);

#endif /* VM_H */
