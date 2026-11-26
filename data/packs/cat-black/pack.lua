-- Midnight: one of the six cats from LuizMelo's Pet Cats Pack (CC0), bundled with OBSPETS.
-- See LICENSE.txt. To change it, open it in the pet editor (a change saves your own copy).
return {
	name = "Midnight",
	describe = "a small black pixel-art cat with yellow eyes",
	pronouns = "they",
	personality = "mysterious and a little aloof, secretly very affectionate, loves the dark and a good stare",
	sheet = "sheet.png",
	cell = { 32, 32 },
	faces = "right",

	anims = {
		idle = { row = 0, frames = 10, fps = 7 },
		look = { row = 1, frames = 4, fps = 6 },
		clean = { row = 2, frames = 5, fps = 6 },
		clean2 = { row = 3, frames = 5, fps = 6 },
		walk = { row = 4, frames = 8, fps = 10 },
		run = { row = 5, frames = 8, fps = 13 },
		sleep = { row = 6, frames = 1, fps = 1 },
		paw = { row = 7, frames = 2, fps = 6, whoosh = 0, hit = 1 },
	},
	-- jumps: crouched low in the stretch, then frames from the run
	poses = {
		crouch = { row = 8, col = 6 },
		rise = { row = 5, col = 2 },
		fall = { row = 5, col = 4 },
		land = { row = 5, col = 7 },
	},
	body = { half = 9, top = 17, top_asleep = 9, head = 7, paw = { 10, 6 }, shadow = 8 },

	sound_set = "cat",
}
