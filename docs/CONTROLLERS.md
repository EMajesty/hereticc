# Controller mappings

Map an incoming channel/note pair to a stable action ID on the control thread:

```cpp
mapper.map_button(0, 36, "pattern.generate", generate_rhythm);
```

Use `MidiMapper::receive` with a `MidiMessage` for tests or headless use. A
future libremidi adapter will translate device messages into this same type.
Mappings must name actions, not UI widgets or hardware addresses.
