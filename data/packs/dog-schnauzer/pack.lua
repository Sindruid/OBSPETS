-- Pepper: one of the six dogs from LuizMelo's Pet Dogs Pack (CC0), bundled with OBSPETS.
-- See LICENSE.txt. To change it, open it in the pet editor (a change saves your own copy).
return {
	name = "Pepper",
	describe = "a grey schnauzer pixel-art dog with a beard",
	pronouns = "they",
	personality = "bossy and alert, barks at everything new, a proud little guard, softens instantly for praise",
	sheet = "sheet.png",
	cell = { 38, 44 },
	faces = "right",

	anims = {
		idle = { row = 0, frames = 10, fps = 7 },
		look = { row = 1, frames = 3, fps = 8 },
		clean = { row = 2, frames = 4, fps = 6 },
		clean2 = { row = 3, frames = 4, fps = 6 },
		walk = { row = 4, frames = 8, fps = 10 },
		run = { row = 5, frames = 8, fps = 13 },
		sleep = { row = 6, frames = 1, fps = 1 },
		paw = { row = 7, frames = 2, fps = 6, whoosh = 0, hit = 1 },
		scared = { row = 1, frames = 3, fps = 12 },   -- barking hard
	},
	-- jumps: the low bow from the stretch, then frames from the run
	poses = {
		crouch = { row = 8, col = 6 },
		rise = { row = 5, col = 2 },
		fall = { row = 5, col = 4 },
		land = { row = 5, col = 7 },
	},
	body = { half = 13, top = 23, top_asleep = 7, head = 10, paw = { 14, 7 }, shadow = 12 },

	sound_set = "dog",
}
