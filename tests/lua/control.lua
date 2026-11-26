-- Control flow and source-map fixture
local total = 0
local i = 1
while i <= 4 do
    if i == 2 then
        i = i + 1
        continue
    elseif i == 3 then
        total = total + 10
        break
    else
        total = total + i
    end
    i = i + 1
end
for j = 1, 3 do
    if j == 2 then
        continue
    end
    total = total + j
end
print(total)
