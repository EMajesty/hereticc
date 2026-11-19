-- Mutate track 1 of the current timeline; all other tracks remain unchanged.
local project = neurogenic.v1.timeline.current()
local rng = neurogenic.random(project:seed())
local track = project:track(1)

track:rotate(240)
track:humanize(rng, 12, 0.05)
track:scale_velocities(0.95)
project:commit()
