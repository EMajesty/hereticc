math.randomseed(neurogenic.v1.transport.frame())
local p = neurogenic.v1.pattern.new()
for step = 1, 16 do
  local enabled = step == 1 or step == 9 or math.random() < 0.28
  p:set_step(step, enabled, 60, enabled and (0.55 + math.random() * 0.45) or 0.8)
end
neurogenic.v1.pattern.queue(p)
