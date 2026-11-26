-- Mochi: one of the six cats from LuizMelo's Pet Cats Pack (CC0), bundled with OBSPETS.
-- See LICENSE.txt. To change it, open it in the pet editor (a change saves your own copy).
return {
	name = "Mochi",
	describe = "a small Siamese pixel-art cat",
	pronouns = "they",
	personality = "chatty and nosy, has an opinion on everything, follows the streamer around, dramatic when ignored",
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
		stretch = { row = 7, frames = 13, fps = 9 },
		-- no swat, beg, scared or angry frames: the reactions that need them stay off unless one stands in
	},
	-- jumps: crouched low in the stretch, then frames from the run
	poses = {
		crouch = { row = 7, col = 6 },
		rise = { row = 5, col = 2 },
		fall = { row = 5, col = 4 },
		land = { row = 5, col = 7 },
	},
	body = { half = 9, top = 17, top_asleep = 9, head = 7, paw = { 10, 6 }, shadow = 8 },

	sound_set = "cat",
}
