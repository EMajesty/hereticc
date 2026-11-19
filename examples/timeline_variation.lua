-- Change this seed for a new reproducible variation from the same algorithm.
local seed = 20261119
local project = neurogenic.v1.timeline.current()
local rng = neurogenic.random(seed)
project:set_seed(seed)
local track = project:create_track(1, "Seeded variation", 3840)
local pool = { 36, 38, 42, 46 }

for step = 0, 15 do
    if rng:chance(0.65) then
        track:add_note {
            tick = step * 240,
            pitch = rng:choose(pool),
            velocity = rng:integer(55, 100) / 100,
            duration = 90
        }
    end
end
project:commit()
