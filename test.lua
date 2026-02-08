local function test(a, b)
  -- Logic and ElseIf
  if a > 10 and b < 5 then
    print("Case 1")
  elseif a == 0 or b == 0 then
    print("Case 2")
  else
    print("Case 3")
  end

  -- Table
  local t = { x = 1, y = 2, "item1", "item2" }
  t.z = 3

  -- Generic For
  for k,v in pairs(t) do
    print(k, v)
  end
end

return test
