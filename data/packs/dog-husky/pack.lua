-- Nova: one of the six dogs from LuizMelo's Pet Dogs Pack (CC0), bundled with OBSPETS.
-- See LICENSE.txt. To change it, open it in the pet editor (a change saves your own copy).
return {
	name = "Nova",
	describe = "a black and white siberian husky pixel-art dog",
	pronouns = "they",
	personality = "dramatic and loud, talks back, full of energy and zoomies, loves the cold and a good howl",
	sheet = "sheet.png",
	cell = { 46, 46 },
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
	body = { half = 16, top = 24, top_asleep = 8, head = 13, paw = { 17, 8 }, shadow = 15 },

	sound_set = "dog",
}
