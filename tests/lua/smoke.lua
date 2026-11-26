-- Exercises the first supported compiler subset.
local a = 2
local b = 3
print(a + b)          -- 5
print(2 + 3 * 4)      -- precedence: 14
print("sum")

if a < b then
    print(1)
else
    print(0)
end

local i = 0
while i < 3 do
    print(i)
    i = i + 1
end

for j = 1, 2 do
    print(j * 10)
end

print("done")
