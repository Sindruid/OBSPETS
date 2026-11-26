-- Kuma: one of the six dogs from LuizMelo's Pet Dogs Pack (CC0), bundled with OBSPETS.
-- See LICENSE.txt. To change it, open it in the pet editor (a change saves your own copy).
return {
	name = "Kuma",
	describe = "a small tan and white akita pixel-art dog",
	pronouns = "they",
	personality = "calm and dignified, quietly devoted, unimpressed by most things, secretly loves a good fuss",
	sheet = "sheet.png",
	cell = { 28, 28 },
	faces = "right",

	anims = {
		idle = { row = 0, frames = 10, fps = 7 },
		look = { row = 1, col = 2, frames = 1, fps = 1 },   -- head up, mouth shut (frames 0-1 are the bark)
		clean = { row = 2, frames = 4, fps = 6 },     -- tongue out, panting happily
		clean2 = { row = 3, frames = 4, fps = 6 },    -- sitting, panting
		walk = { row = 4, frames = 8, fps = 10 },
		run = { row = 5, frames = 8, fps = 13 },
		sleep = { row = 6, frames = 1, fps = 1 },
		paw = { row = 7, frames = 2, fps = 6, whoosh = 0, hit = 1 },
		-- a play-bow stretch: down, a moment at the bottom, back up
		stretch = { cells = { { 0, 8 }, { 1, 8 }, { 2, 8 }, { 3, 8 }, { 4, 8 }, { 5, 8 }, { 6, 8 }, { 7, 8 }, { 8, 8 }, { 9, 8 }, { 9, 8 }, { 9, 8 }, { 9, 8 }, { 9, 8 }, { 8, 8 }, { 7, 8 }, { 6, 8 }, { 5, 8 }, { 4, 8 }, { 3, 8 }, { 2, 8 }, { 1, 8 }, { 0, 8 } }, fps = 11 },
		call = { cells = { { 2, 1 }, { 0, 1 }, { 1, 1 }, { 0, 1 }, { 2, 1 } }, fps = 10 },   -- one bark
		beg = { row = 7, frames = 2, fps = 3 },        -- sitting up nicely
		scared = { row = 1, frames = 3, fps = 12 },    -- no cowering frames: it barks at the fright
		angry = { row = 1, frames = 3, fps = 12 },     -- barking hard
	},
	-- jumps: the low bow from the stretch, then frames from the run
	poses = {
		crouch = { row = 8, col = 6 },
		rise = { row = 5, col = 2 },
		fall = { row = 5, col = 4 },
		land = { row = 5, col = 7 },
	},
	body = { half = 9, top = 15, top_asleep = 6, head = 6, paw = { 10, 5 }, shadow = 8 },

	sound_set = "dog",
}
