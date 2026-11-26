-- Blob: OBSPETS's own starter character, a slime drawn from code (no third-party art, so it
-- ships with the plugin). It has no sounds of its own, so it plays OBSPETS's default set
-- (data/sounds/default/); list files under `sounds` to give it its own (see ../README.md). To change it,
-- copy this folder into your own packs folder first.
return {
	name = "Blob",
	-- for chat moods (the plugin's AI reactions)
	describe = "a small, round, mint-green pixel-art slime",
	pronouns = "it",
	personality = "cheerful, curious and easily delighted: it wobbles with joy at any attention, "
		.. "loves bouncing about and showing off, is a little clingy, sulks when ignored but forgives "
		.. "fast, and is wary of sponges, salt and vacuum cleaners",
	sheet = "sheet.png",
	cell = { 32, 32 },
	faces = "right",

	anims = {
		idle = { row = 0, frames = 4, fps = 4 },
		look = { row = 1, frames = 4, fps = 3 },
		clean = { row = 2, frames = 4, fps = 6 },
		clean2 = { row = 3, frames = 4, fps = 6 },
		walk = { row = 4, frames = 6, fps = 9 },
		run = { row = 5, frames = 6, fps = 14 },
		sleep = { row = 6, frames = 4, fps = 2 },
		paw = { row = 7, frames = 6, fps = 9, whoosh = 2, hit = 3 },
		scared = { row = 9, frames = 4, fps = 14 },
	},
	poses = {
		crouch = { row = 8, col = 0 },
		rise = { row = 8, col = 1 },
		fall = { row = 8, col = 2 },
		land = { row = 8, col = 3 },
	},
	body = { half = 10, top = 15, top_asleep = 11, head = 3, paw = { 12, 7 }, shadow = 8 },

	sounds = {},

	colors = {
		heart = "#ff6b8b", glint = "#ffffff", bang = "#ffd166", anger = "#ef476f",
		mark = "#f1faee", note = "#ffd166", note2 = "#f1faee", spark = "#f1faee", z = "#f1faee",
	},
}
