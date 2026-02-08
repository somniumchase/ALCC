local t = {1, 2, 3, key="value"}
local function foo(self, a)
  return self.x + a
end
local obj = {x=10, foo=foo}
local res = obj:foo(5)
if res > 10 and res < 20 then
  print("Between 10 and 20")
else
  print("Out of range")
end
local a = "hello" .. " " .. "world"
