# Lua API

The public namespace is versioned as `neurogenic.v1`. The intended native API:

```lua
local p = neurogenic.v1.pattern.new()
p:set_step(1, { note = 60, velocity = 1.0 })
neurogenic.v1.pattern.queue(p) -- commits at the next bar
```

Lua runs only on the control thread. Scripts produce validated pattern
snapshots; they never access voices or callback storage. In the dependency-free
build the same path accepts `pattern x...x...x.x...x.` for deterministic tests.
Native sol2 registration and sandbox/resource limits are still stubbed.
