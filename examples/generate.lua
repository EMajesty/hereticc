-- Intended neurogenic.v1 example; pattern changes commit at the next bar.
local p = neurogenic.v1.pattern.new()
for step = 1, 16 do
  if step == 1 or step == 5 or step == 9 or step == 11 or step == 15 then
    p:set_step(step, { note = 60, velocity = 1.0 })
  end
end
neurogenic.v1.pattern.queue(p)
