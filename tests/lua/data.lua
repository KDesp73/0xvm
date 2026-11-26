-- Static data coverage for 0xluac: EQU constants, initialized byte/word/quad
-- tables, big-endian layout, and constant expressions.
const FRAME_MS = 66
const BASE = 0x100
const MASK = 0xFF
const SECOND = 1
const COLOR = 0xFFC040

const row = bytes { 0x80, 0x40, 0x20 }
const offsets = words { BASE, BASE + 2, -1 }
const glyphs = quads { 0x0102030405060708, -1 }

print(FRAME_MS)
print(BASE + 1)
print(MASK & 0x0F)
print(COLOR >> 16)

print(row[0])
print(row[SECOND])
print(row[2])

print(offsets[0])
print(offsets[1])
print(offsets[2])

print(glyphs[0] >> 56)
print(glyphs[0] & 0xFF)
print(glyphs[1])
