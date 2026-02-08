local function nested(x)
  local y = 10
  return function(z)
    return x + y + z
  end
end

local f = nested(5)
print(f(20)) -- 35

local t = {
  a = 1,
  b = 2,
  ["c"] = 3,
  4, 5, 6
}

for k, v in pairs(t) do
  print(k, v)
end

local function varargs(...)
  local args = {...}
  for i = 1, #args do
    print(args[i])
  end
end

varargs("va", "rb", "args")

local i = 0
while i < 3 do
  print("while", i)
  i = i + 1
end

if i == 3 then
  print("if branch")
else
  print("else branch")
end
