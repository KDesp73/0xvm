#include "dis.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "vm.h"

static uint64_t read_be(const uint8_t *p, unsigned width)
{
    uint64_t v = 0u;
    for (unsigned i = 0u; i < width; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

unsigned vm_disassemble_range(const uint8_t *mem, uint16_t addr, uint32_t end,
                              char *out, size_t out_cap)
{
    if (!mem || !out || out_cap == 0u || end > VM_RAM_SIZE ||
        (uint32_t)addr >= end) return 0u;

    uint8_t op = mem[addr];
    unsigned size = vm_inst_size(op);
    if ((uint32_t)addr + size > end) return 0u;

    const char *name = vm_opcode_name(op);
    if (strcmp(name, "RESERVED") == 0) {
        snprintf(out, out_cap, "0x%04X: RESERVED (0x%02X)",
                 (unsigned)addr, (unsigned)op);
        return size;
    }

    switch ((VmOpcode)op) {
        case OP_PUSH:
            snprintf(out, out_cap, "0x%04X: PUSH %" PRId64,
                     (unsigned)addr, (int64_t)read_be(mem + addr + 1u, 8u));
            break;
        case OP_PUSHF:
            snprintf(out, out_cap, "0x%04X: PUSHF %.17g", (unsigned)addr,
                     vm_bits_to_f64((int64_t)read_be(mem + addr + 1u, 8u)));
            break;
        case OP_JMP:
        case OP_JMP_Z:
        case OP_CALL:
            snprintf(out, out_cap, "0x%04X: %s 0x%04X", (unsigned)addr, name,
                     (unsigned)read_be(mem + addr + 1u, 2u));
            break;
        case OP_ENTER:
        case OP_LOCAL_GET:
        case OP_LOCAL_SET:
            snprintf(out, out_cap, "0x%04X: %s %u", (unsigned)addr, name,
                     (unsigned)mem[addr + 1u]);
            break;
        case OP_DEI:
        case OP_DEO:
            snprintf(out, out_cap, "0x%04X: %s 0x%02X", (unsigned)addr, name,
                     (unsigned)mem[addr + 1u]);
            break;
        default:
            snprintf(out, out_cap, "0x%04X: %s", (unsigned)addr, name);
            break;
    }
    return size;
}

unsigned vm_disassemble(const uint8_t *mem, uint16_t addr,
                        char *out, size_t out_cap)
{
    return vm_disassemble_range(mem, addr, VM_RAM_SIZE, out, out_cap);
}
