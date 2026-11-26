-- Array coverage for 0xluac: byte/word storage, literal and dynamic indices,
-- signed word values, and compile-time bounds checks.
local bytes = byte_array(4)
bytes[0] = 65
bytes[3] = 66
print(bytes[0])
print(bytes[3])

local i = 1
bytes[i] = 7
print(bytes[1])

local words = word_array(3)
words[0] = 1000
words[2] = -2
print(words[0])
print(words[2])
print(words[0] + words[2])
