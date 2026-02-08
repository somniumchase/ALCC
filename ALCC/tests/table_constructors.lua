local t = {
    key1 = "value1",
    ["key2"] = "value2",
    [10] = "value3",
    nested = { 1, 2, 3 }
}
local x = 1
local y = 2
if x == 1 and y == 2 then
    print("x is 1 and y is 2")
end
