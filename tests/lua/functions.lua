-- SPDX-License-Identifier: MIT
-- Function coverage for 0xluac: forward calls, recursion, nested calls,
-- statement calls, and function-local variables.
print(forty_two())
print(add(2, 3))
print(add(add(1, 2), 4))
print(factorial(5))
print(twice(10))
print(with_local(7))

local base = 100
noop(1, 2, 3)
print(base + add(1, 2))

function forty_two()
    return 42
end

function add(a, b)
    return a + b
end

function factorial(n)
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end

function twice(x)
    local doubled = x * 2
    return doubled
end

function with_local(x)
    local y = x + 5
    return y * 2
end

function noop(a, b, c)
    return 0
end
