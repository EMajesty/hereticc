-- Deterministic four-track drum generator for an empty timeline.
local project = neurogenic.v1.timeline.current()
local rng = neurogenic.random(12345)
project:set_seed(12345)

local kick = project:create_track(1, "Kick", 3840)
local snare = project:create_track(2, "Snare", 3840)
local hat = project:create_track(3, "Closed hat", 1920)
local accent = project:create_track(4, "Accent", 2880)

kick:generate_euclidean(16, 5, 0, 36, 1.0, 120)
snare:add_note { tick = 960, pitch = 38, velocity = 0.9, duration = 120 }
snare:add_note { tick = 2880, pitch = 38, velocity = 0.9, duration = 120 }
hat:generate_euclidean(8, 7, 0, 42, 0.55, 60)

for step = 0, 11 do
    if rng:chance(0.35) then
        accent:add_note {
            tick = step * 240,
            pitch = rng:choose { 42, 46 },
            velocity = 0.45,
            duration = 60
        }
    end
end

project:commit()
