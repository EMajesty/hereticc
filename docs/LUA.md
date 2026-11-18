# Lua API

Lua 5.4 runs through sol2 on the MIDI/control thread, never in the audio
callback. The versioned API currently contains:

```lua
local frame = neurogenic.v1.transport.frame()
local p = neurogenic.v1.pattern.new()
p:set_step(1, true, 60, 1.0) -- step 1..16, enabled, note 0..127, velocity 0..1
neurogenic.v1.pattern.queue(p) -- validates and commits at the next bar
```

`--script FILE` selects the file executed by mapped MIDI note 36. Script errors,
invalid values, and a full pattern queue are reported without stopping audio.
The working randomized example is [`examples/generate.lua`](../examples/generate.lua).

Only base, math, table, and string libraries are opened. Scripts cannot perform
file I/O through the Lua standard library. The API is intentionally small and
will evolve by adding members under versioned namespaces.
