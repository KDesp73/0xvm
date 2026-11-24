#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Wire v3. The v2 format (28-byte header, stack, heap) is retired by this
 * prototype; vm_deserialize() accepts only VM_VERSION. Every piece of mutable
 * state -- including the operand stack, call frames, and allocator metadata --
 * is serialized, so migration remains lossless.
 */
#define VM_VERSION          3u
#define VM_MAGIC            0x564D4D47u /* "VMMG" */
#define VM_WIRE_HEADER_BYTES 32u
#define VM_FLAG_DIRTY       0x01u       /* heap payload is a page-level diff */
#define VM_PAGE_SIZE        4096u

#define VM_STACK_INIT_CAP   256u
#define VM_STACK_CAP        VM_STACK_INIT_CAP /* legacy alias for the initial capacity */
#define VM_FRAME_INIT_CAP   64u
#define VM_BLOCK_INIT_CAP   64u
#define VM_HEAP_INIT_CAP    4096u   /* initial growth chunk; not a hard limit */

typedef enum {
    OP_HALT = 0,
    OP_NOP,
    OP_PUSH,      /* operand: int64_t value */
    OP_POP,
    OP_DUP,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_CMP_LT,
    OP_JMP,       /* operand: int64_t target_ip (absolute instruction index) */
    OP_JMP_Z,     /* operand: int64_t target_ip */
    OP_ALLOC,     /* pops size (int64_t), pushes heap offset (int64_t); grows heap */
    OP_LOAD32,    /* pops addr, pushes sign-extended int32 from heap */
    OP_STORE32,   /* pops addr (top), pops val, writes low 32 bits */
    OP_PRINT,     /* pops val (int64_t), prints it */
    OP_MIGRATE,   /* yields VM_YIELD_MIGRATE at clean instruction boundary */

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

    /* Stack shuffling: no wire-format cost beyond the live stack slots. */
    OP_SWAP,      /* [..., a, b] -> [..., b, a] */
    OP_OVER,      /* [..., a, b] -> [..., a, b, a] */
    OP_ROT,       /* [..., a, b, c] -> [..., b, c, a] */
    OP_PICK,      /* pops n (top), pushes stack[sp-1-n]; n = 0 duplicates top */

    /* Wider/narrower heap access. All accessors honor [0, hp). */
    OP_LOAD8,     /* pops addr, pushes unsigned byte */
    OP_STORE8,    /* pops addr (top), pops val, writes low 8 bits */
    OP_LOAD16,    /* pops addr, pushes unsigned 16-bit value */
    OP_STORE16,   /* pops addr (top), pops val, writes low 16 bits */
    OP_LOAD32U,   /* pops addr, pushes zero-extended uint32 */
    OP_LOAD64,    /* pops addr, pushes int64_t (raw bits) */
    OP_STORE64,   /* pops addr (top), pops val, writes all 64 bits */

    /* Heap reuse. Allocator metadata lives in VmState and is serialized. */
    OP_FREE,      /* pops addr; returns its block to the free list */

    /* Procedures. CALL pushes {return ip, frame pointer}; RET pops it. */
    OP_CALL,      /* operand: target_ip */
    OP_CALL_I,    /* pops target_ip from the stack */
    OP_RET,       /* pops result, unwinds frame, pushes result */
    OP_ENTER,     /* operand: n; reserves n zeroed locals in the current frame */
    OP_LOCAL_GET, /* operand: local index; pushes stack[fp + index] */
    OP_LOCAL_SET, /* operand: local index; pops value into stack[fp + index] */

    OP_JMP_I,     /* pops target_ip and jumps; target validated before pop */

    /* IEEE-754 doubles: the stack stays int64_t, values are raw bit patterns. */
    OP_PUSHF,     /* operand: raw 64-bit double pattern */
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

    /* Host I/O. Host-owned resources are deliberately not part of the state. */
    OP_PRINT_STR, /* pops len (top), pops addr; prints heap bytes [addr, addr+len) */
    OP_PRINT_CHAR,/* pops codepoint and prints one byte */
    OP_READ_INT,  /* reads one integer from stdin and pushes it (0 on bad input) */
    OP_READ_STR,  /* pops max (top), pops addr; reads a line into heap, pushes length */

    OP_COUNT      /* sentinel: number of opcodes */
} VmOpcode;

typedef struct {
    uint8_t op;
    int64_t arg;
} VmInst;

typedef enum {
    VM_OK = 0,
    VM_HALTED,
    VM_YIELD_MIGRATE,
    VM_ERR_BAD_IP,
    VM_ERR_BAD_OP,
    VM_ERR_STACK_OVERFLOW,
    VM_ERR_STACK_UNDERFLOW,
    VM_ERR_HEAP_OVERFLOW,
    VM_ERR_BAD_ADDR,
    VM_ERR_DIV_ZERO,
    VM_ERR_IO,
    VM_ERR_BAD_FRAME,
    VM_ERR_BAD_LOCAL,
    VM_ERR_PAYLOAD     /* malformed wire payload */
} VmStatus;

typedef struct {
    uint32_t ret_ip;   /* instruction to resume at after RET */
    uint32_t prev_fp;  /* caller's frame pointer */
} VmFrame;

typedef struct {
    uint64_t addr;
    uint64_t size;     /* allocated size (already rounded up to 4) */
    uint8_t live;      /* 1 = in use, 0 = on the free list */
} VmBlock;

typedef struct {
    uint32_t ip;
    uint32_t sp;
    uint32_t fp;         /* index in stack of the current frame's first local */
    uint64_t hp;         /* used heap bytes */
    uint64_t heap_cap;   /* allocated heap capacity (>= hp) */
    uint32_t ticks;

    int64_t  *stack;     /* dynamically grown; NULL when stack_cap == 0 */
    uint32_t  stack_cap;
    uint8_t  *heap;      /* dynamically grown; NULL when hp == 0 */
    VmFrame  *frames;    /* call frames; NULL when frame_cap == 0 */
    uint32_t  fsp;
    uint32_t  frame_cap;
    VmBlock  *blocks;    /* allocator metadata; NULL when block_cap == 0 */
    uint32_t  block_count;
    uint32_t  block_cap;
} VmState;

void vm_init(VmState *vm);
/* Releases every owned buffer and resets the state. Safe on a zeroed/freed state. */
void vm_free(VmState *vm);
/* Deep copy of all live state. Returns 0 on success, -1 on allocation failure. */
int  vm_clone(const VmState *src, VmState *out);

VmStatus vm_step(VmState *vm, const VmInst *prog, size_t prog_len);
VmStatus vm_run(VmState *vm, const VmInst *prog, size_t prog_len, uint32_t max_steps);

/* Full-snapshot wire format (flags = 0): header, stack, frames, blocks, heap.
 * If buf is NULL, returns the required buffer size without writing. Returns 0
 * on invalid state or insufficient capacity. */
size_t vm_serialize(const VmState *vm, uint8_t *buf, size_t buf_cap);
int    vm_deserialize(VmState *out, const uint8_t *buf, size_t buf_len);

/* Page-level diff against `base`. Only changed 4 KiB heap pages are carried,
 * with VM_FLAG_DIRTY set in the header. All other state is always included. */
size_t vm_serialize_diff(const VmState *vm, const VmState *base,
                         uint8_t *buf, size_t buf_cap);
int    vm_deserialize_diff(VmState *out, const VmState *base,
                           const uint8_t *buf, size_t buf_len);

const char *vm_status_str(VmStatus status);
const char *vm_opcode_name(uint8_t op);

/* When enabled, PRINT/FPRINT/PRINT_STR prefix program output with the current
 * tick count. This is host output policy, not migratable VM state. */
void vm_set_debug_ticks(int enabled);

/* IEEE-754 bit conversion helpers (memcpy-based, no strict-aliasing UB). */
double  vm_bits_to_f64(int64_t bits);
int64_t vm_f64_to_bits(double value);

#endif /* VM_H */
