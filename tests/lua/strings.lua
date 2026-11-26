-- String values for 0xluac: (address, length) pairs, bindings, indexing,
-- slicing, copying into a heap buffer, and multi-line text.
local title = "0XVM TYPE"
local notice = "ready\nnext"

print(title)
print(notice)
print(strlen(title))
print(strcmp(title, "0XVM TYPE"))
print(strcmp(title, "0XVM!"))
print(strcmp("abc", "abd"))
print(title[0])
print(title[4])
print(strfind(title, "TYPE"))
print(strchr(title, 86))

local part = slice(title, 5, 4)
print(part)
print(strlen(part))
print_str(part)

local clipped = slice(title, 7, 99)
print(clipped)
print(strlen(clipped))

local missing = slice(title, 99, 4)
print(strlen(missing))
local empty = slice(title, 2, 0)
print(strlen(empty))
print_str(empty)

local buf = alloc_bytes(8)
strcpy(buf, part)
print(buf[0])
print(buf[3])
free(buf)
