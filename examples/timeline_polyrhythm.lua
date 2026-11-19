-- Three tracks whose loop lengths meet only after several cycles.
local project = neurogenic.v1.timeline.current()
local three = project:create_track(1, "Three", 720)
local five = project:create_track(2, "Five", 1200)
local seven = project:create_track(3, "Seven", 1680)

three:generate_euclidean(3, 2, 0, 36, 1.0, 90)
five:generate_euclidean(5, 2, 1, 38, 0.8, 90)
seven:generate_euclidean(7, 3, 0, 42, 0.6, 60)
project:commit()
