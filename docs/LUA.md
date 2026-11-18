# Lua API

Lua 5.4 runs through sol2 on the MIDI/control thread, never in the audio
callback. The versioned API currently contains:

```lua
local frame = neurogenic.v1.transport.frame()
local p = neurogenic.v1.pattern.new()
p:set_step(1, true, 36, 1.0) -- note selects the engine's mapped sample slot
neurogenic.v1.pattern.queue(p) -- validates and commits at the next bar
```

`--script FILE` selects the file executed by mapped MIDI note 36. Script errors,
invalid values, and a full pattern queue are reported without stopping audio.
Lua API compatibility is unchanged; pattern note values now resolve through the
same note-to-slot mapping used by MIDI input.
The working randomized example is [`examples/generate.lua`](../examples/generate.lua).

Only base, math, table, and string libraries are opened. Scripts cannot perform
file I/O through the Lua standard library. The API is intentionally small and
will evolve by adding members under versioned namespaces.
