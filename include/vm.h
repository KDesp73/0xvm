#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>

struct VmState;

/* Fixed machine geometry (see docs/Machine-Contract.md). */
#define VM_RAM_SIZE        0x10000u  /* 64 KiB, byte addresses 0x0000..0xFFFF */
#define VM_ZERO_PAGE_SIZE  0x0100u
#define VM_IMAGE_START     0x0100u
#define VM_IMAGE_MAX       0x3F00u   /* 0x4000 - 0x0100 */
#define VM_HEAP_START      0x4000u
#define VM_STACK_SIZE      256u      /* cells, outside addressable RAM */
#define VM_FRAME_COUNT     64u
#define VM_BLOCK_COUNT     64u
#define VM_DEVICE_COUNT    0x100u    /* 256 ports, outside addressable RAM */

/* ROM header: 16 bytes, big-endian multi-byte fields. */
#define VM_ROM_HEADER_SIZE 16u
#define VM_ROM_VERSION     2u

/* Device 0: System. */
#define VM_SYS_VERSION     0x00u     /* read: ROM format version */
#define VM_SYS_DEBUG       0x01u     /* read/write: host tick-prefix flag */
#define VM_SYS_HALT        0x02u     /* write: halt with VM_HALTED */

/* Device 1: Console. */
#define VM_CONSOLE_PUTC    0x10u     /* write: low byte to stdout */
#define VM_CONSOLE_GETC    0x11u     /* read: one byte from stdin (0 on EOF) */
#define VM_CONSOLE_PUTI    0x12u     /* write: signed decimal + newline */
#define VM_CONSOLE_PUTF    0x13u     /* write: double bits + newline */
#define VM_CONSOLE_PUTS_ADDR 0x14u  /* write: RAM address of PUTS string */
#define VM_CONSOLE_PUTS_LEN  0x15u  /* write: length of PUTS string */
#define VM_CONSOLE_PUTS      0x16u  /* write: string + newline (value ignored) */
#define VM_CONSOLE_GETS_ADDR 0x17u  /* write: RAM address for GETS input */
#define VM_CONSOLE_GETS_MAX  0x18u  /* write: maximum GETS length */
#define VM_CONSOLE_GETS      0x19u  /* write: read a line (value ignored) */
#define VM_CONSOLE_GETS_LEN  0x1Au  /* read: length from the last GETS */
#define VM_CONSOLE_GETI      0x1Bu  /* read: one integer from stdin */

/*
 * Opcode numbers are explicit because retired I/O opcodes stay reserved.
 * 0x0E and 0x41..0x45 are permanently unassigned; 0x80/0x81 are the device
 * layer. All other unassigned values are reserved.
 */
typedef enum {
    OP_HALT = 0x00,
    OP_NOP = 0x01,
    OP_PUSH = 0x02,      /* 8-byte immediate: int64_t value */
    OP_POP = 0x03,
    OP_DUP = 0x04,
    OP_ADD = 0x05,
    OP_SUB = 0x06,
    OP_MUL = 0x07,
    OP_CMP_LT = 0x08,
    OP_JMP = 0x09,       /* 2-byte immediate: absolute byte address */
    OP_JMP_Z = 0x0A,     /* 2-byte immediate: absolute byte address */
    OP_ALLOC = 0x0B,     /* pops size (int64_t), pushes heap address (uint16_t) */
    OP_LOAD32 = 0x0C,    /* pops addr, pushes sign-extended int32 from RAM */
    OP_STORE32 = 0x0D,   /* pops addr (top), pops val, writes low 32 bits */

    OP_DIV = 0x0F,       /* a / b, truncated toward zero; INT64_MIN / -1 wraps */
    OP_MOD = 0x10,       /* a % b, sign of a; INT64_MIN % -1 is 0 */
    OP_CMP_EQ = 0x11,
    OP_CMP_NE = 0x12,
    OP_CMP_LE = 0x13,
    OP_CMP_GT = 0x14,
    OP_CMP_GE = 0x15,
    OP_AND = 0x16,
    OP_OR = 0x17,
    OP_XOR = 0x18,
    OP_NOT = 0x19,
    OP_SHL = 0x1A,       /* a << (b & 63), unsigned shift */
    OP_SHR = 0x1B,       /* a >> (b & 63), logical unsigned shift */
    OP_LNOT = 0x1C,
    OP_LAND = 0x1D,
    OP_LOR = 0x1E,

    OP_SWAP = 0x1F,
    OP_OVER = 0x20,
    OP_ROT = 0x21,
    OP_PICK = 0x22,

    OP_LOAD8 = 0x23,
    OP_STORE8 = 0x24,
    OP_LOAD16 = 0x25,
    OP_STORE16 = 0x26,
    OP_LOAD32U = 0x27,
    OP_LOAD64 = 0x28,
    OP_STORE64 = 0x29,
    OP_FREE = 0x2A,

    OP_CALL = 0x2B,      /* 2-byte immediate: absolute byte address */
    OP_CALL_I = 0x2C,    /* pops target byte address from the stack */
    OP_RET = 0x2D,
    OP_ENTER = 0x2E,     /* 1-byte immediate: reserve n zeroed locals */
    OP_LOCAL_GET = 0x2F, /* 1-byte immediate: push stack[fp + index] */
    OP_LOCAL_SET = 0x30, /* 1-byte immediate: pop value into stack[fp + index] */
    OP_JMP_I = 0x31,     /* pops target byte address and jumps */

    OP_PUSHF = 0x32,     /* 8-byte immediate: raw 64-bit double pattern */
    OP_FADD = 0x33,
    OP_FSUB = 0x34,
    OP_FMUL = 0x35,
    OP_FDIV = 0x36,
    OP_FNEG = 0x37,
    OP_FABS = 0x38,
    OP_FSQRT = 0x39,
    OP_FCMP_LT = 0x3A,
    OP_FCMP_EQ = 0x3B,
    OP_FCMP_LE = 0x3C,
    OP_FCMP_GT = 0x3D,
    OP_FCMP_GE = 0x3E,
    OP_FTOI = 0x3F,
    OP_ITOF = 0x40,

    OP_DEI = 0x80,       /* 1-byte immediate: port; pushes a cell */
    OP_DEO = 0x81        /* 1-byte immediate: port; pops a cell */
} VmOpcode;

#define VM_OPCODE_MAX 0x81u

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

/*
 * Device callbacks are host code, not VM code. Return VM_OK, VM_HALTED, or
 * VM_ERR_IO. A callback must not touch VM state unless it can complete
 * successfully; VM_ERR_IO leaves all VM state unchanged.
 */
typedef VmStatus (*VmDeviceRead)(void *ctx, struct VmState *vm,
                                 uint8_t port, int64_t *out);
typedef VmStatus (*VmDeviceWrite)(void *ctx, struct VmState *vm,
                                  uint8_t port, int64_t value);

typedef struct {
    void *ctx;
    VmDeviceRead read;
    VmDeviceWrite write;
} VmDevice;

/* The whole machine lives in this struct. No field owns heap memory. */
typedef struct VmState {
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
    VmDevice dev;      /* host device callbacks; preserved across resets */
} VmState;

void vm_init(VmState *vm);
/* Reset to the post-init state, preserving device callbacks. */
void vm_free(VmState *vm);

/* Install (or clear, with NULL) the host device callbacks. */
void vm_set_device(VmState *vm, const VmDevice *dev);

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

/* IEEE-754 bit conversion helpers (memcpy-based, no strict-aliasing UB). */
double  vm_bits_to_f64(int64_t bits);
int64_t vm_f64_to_bits(double value);

#endif /* VM_H */
