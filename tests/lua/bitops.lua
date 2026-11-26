-- Bitwise coverage for 0xluac: hex literals, shifts, AND/OR/XOR, unary NOT,
-- and C-like precedence relative to arithmetic and comparisons.
print(0x30 & 0x3C)
print(0xF0 | 0x0F)
print(0xF0 ~ 0x0F)
print(~0)
print(1 << 4)
print(16 >> 2)
print(0xFFC040)

local x = 3
local y = 5
local packed = (x << 16) | y
print(packed)
print((packed >> 16) & 0xFF)
print(packed & 0xFFFF)

print(1 + 2 << 3)
print(1 << 3 + 2)
print(0x10 == 16)
print(0x10 ~= 16)
