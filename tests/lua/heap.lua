-- Heap coverage for 0xluac: allocation, typed indexing, pointer arithmetic,
-- runtime sizes, and explicit release.
local text = alloc_bytes(4)
text[0] = 65
text[3] = 66
print(text[0])
print(text[3])

local i = 1
text[i] = 7
print(text[1])

local words = alloc_words(3)
words[0] = 1000
words[2] = -2
print(words[0])
print(words[2])

local tail = text + 2
tail[0] = 99
print(text[2])
print(tail[0])

local word_tail = words + 8
word_tail[0] = 1234
print(words[2])

local n = 2
local dynamic = alloc_bytes(n)
dynamic[1] = 88
print(dynamic[1])

free(text)
free(words)
free(dynamic)
