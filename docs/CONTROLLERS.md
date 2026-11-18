# Controller mappings

Map an incoming channel/note pair to a stable action ID on the control thread:

```cpp
mapper.map_button(0, 36, "pattern.generate", generate_rhythm);
```

Use `MidiMapper::receive` with a `MidiMessage` for tests or headless use. A
libremidi adapter translates native messages into this same type. Run
`neurogenic --list-devices` and pass a full name or substring with `--midi`.
Mappings must name actions, not UI widgets or hardware addresses. The current
demo maps channel 1 note 36 to `pattern.generate`; every note-on also triggers
the loaded sample.
