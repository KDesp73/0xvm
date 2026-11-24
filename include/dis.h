#ifndef DIS_H
#define DIS_H

#include <stddef.h>
#include <stdint.h>

/* Enough room for one formatted instruction line. */
#define VM_DIS_LINE_MAX 96u

/*
 * Decode one instruction from a RAM image at absolute byte address `addr`.
 * `end` is an exclusive absolute byte limit; pass VM_RAM_SIZE for a full image.
 * Returns the encoded size, or 0 if `addr >= end`, `end > VM_RAM_SIZE`, or the
 * instruction's immediate crosses `end`. Reserved opcode values are decoded as
 * "RESERVED" and advance by one byte.
 */
unsigned vm_disassemble_range(const uint8_t *mem, uint16_t addr, uint32_t end,
                              char *out, size_t out_cap);

/* Convenience wrapper over the full 64 KiB RAM. */
unsigned vm_disassemble(const uint8_t *mem, uint16_t addr,
                        char *out, size_t out_cap);

#endif /* DIS_H */
