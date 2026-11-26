-- An OBSPETS character pack. Copy this folder to %APPDATA%\obs-studio\plugin_config\obspet\packs,
-- rename it (names starting with "_" or "." are skipped), add your sprite sheet and sounds, and
-- fill this in. Pick it under Character and click "Reload character" after each edit. Problems
-- show under the list and in OBS's log.
--
-- Only `sheet` and `anims.idle` are required. A missing animation borrows another (look -> idle,
-- run -> walk, ...), so you can start with one row of idle frames.
--
-- This file is data only. Paths are relative to this folder.
return {
	-- The name in "viewer pet <name>" and in the channel-point reward ("Pet <name>"). A new
	-- Pet starts with it; its properties can change it per source.
	name = "Template",

	-- Optional, for chat moods (the Pet's AI reactions to chat). Leave them out for a generic
	-- "small pet" that is affectionate but proud. A personality set on sindruid.com/streamtools/obspets
	-- replaces this one. They only describe the pet: the AI still just picks one of its reactions.
	-- describe = "a small orange fox",              -- what it is, up to 160 characters
	-- pronouns = "she",                            -- "he", "she", "they" or "it" (default "they")
	-- personality = "playful and nosy, a show-off, easily bored, and scared of thunder",
	--                                              -- how it behaves, up to 500 characters

	-- The sprite sheet: a PNG with every frame in a grid of equal cells.
	sheet = "sheet.png",
	cell = { 32, 32 },     -- cell width and height in pixels. Motion scales with the height:
	                       -- a 64 px tall pet jumps twice as high as a 32 px one.
	faces = "right",       -- which way the drawings face ("right" or "left"); the Pet mirrors them
	feet = 0,              -- empty pixels between the feet and the bottom of the cell
	-- hue = 0,            -- degrees to turn the sheet's colors around the color wheel (-180..180)

	-- Animations. Each is one of:
	--   { row = 0, frames = 4, fps = 5 }            frames left to right along a row (row 0 = top)
	--   { row = 0, col = 4, frames = 4, fps = 5 }   ... starting at column 4
	--   { cells = { {0, 0}, {1, 0}, {0, 1} }, fps = 5 }   any cells, as {column, row}, in order
	-- Columns and rows count from 0.
	anims = {
		idle = { row = 0, frames = 4, fps = 5 },     -- sitting, breathing (required)
		look = { row = 1, frames = 4, fps = 5 },     -- looking around; also "listening"
		clean = { row = 2, frames = 4, fps = 7 },    -- washing / grooming (contentment)
		clean2 = { row = 3, frames = 4, fps = 7 },   -- a second washing loop
		walk = { row = 4, frames = 8, fps = 10 },
		run = { row = 5, frames = 8, fps = 14 },
		sleep = { row = 6, frames = 4, fps = 3 },    -- lying down, z's float up from its head
		-- a swat. `whoosh`: the frame the swipe sound plays on; `hit`: the frame sparks fly off
		paw = { row = 7, frames = 6, fps = 9, whoosh = 2, hit = 3 },
		scared = { row = 9, frames = 8, fps = 12 },  -- startled, puffed up
	},

	-- Single frames held during jumps, as { row = .., col = .. }. Missing ones fall back:
	-- crouch -> the first idle frame, rise -> crouch, fall -> rise, land -> crouch.
	poses = {
		crouch = { row = 8, col = 4 },   -- crouched before a jump, and the wiggle before a pounce
		rise = { row = 8, col = 1 },     -- going up
		fall = { row = 8, col = 2 },     -- coming down
		land = { row = 8, col = 5 },     -- touching down
	},

	-- Where things sit on the body, in sprite pixels from the middle of the cell's bottom edge
	-- (leave out to scale the defaults, shown here for a 32 px cell, to your cell height):
	body = {
		half = 9,          -- half the body's width: how close it gets to things it walks round
		top = 15,          -- the top of its head: hearts, "!" and "?" go above this
		top_asleep = 10,   -- ... while it sleeps (z's)
		head = 3,          -- how far its head is in front of its middle
		paw = { 9, 8 },    -- where a swat lands: forward, up
		shadow = 6,        -- half the shadow's width
	},

	-- Walking and running speed, in sprite pixels a second (defaults for a 32 px cell).
	speed = { walk = 24, run = 70 },

	-- Sounds, by role. Each is a file, a list of files (one is picked at random, never the same
	-- twice running), the name of another role to share its files, or false for silence. A role
	-- you leave out plays OBSPETS's built-in sound for it, from `sound_set`.
	-- Use .wav files: stereo 48 kHz at about -38 LUFS (voice) and -41 LUFS (foley)
	-- sits well under a stream. (OBSPETS plays only .wav.)
	sound_dir = "sounds",   -- where the files are, relative to this folder (default: right here)
	sound_set = "default",  -- built-in sounds for roles left out: "default" (soft calls that suit
	                        -- any pet), "cat" (meows, a purr, a hiss),
	                        -- "dog" (woofs, whines, a growl) or "none" (silence)
	sounds = {
		-- voice
		pet = "purr.wav",                -- a channel-point pet, love from chat (plays while asleep too)
		notice = { "mew1.wav", "mew2.wav" },   -- a follow, bits, a raid starting, a greeting
		cheer = "happy.wav",             -- a sub, a gift, the raid's welcome
		question = "chirp.wav",          -- curious, startled
		plead = "beg.wav",               -- wants attention
		angry = "hiss.wav",              -- angry or scared
		-- foley (the body)
		swipe = "swipe.wav",             -- each swat
		land = "land.wav",               -- landing a jump
		pounce = "land",                 -- a heavier landing (here: the same files as land)
		fling = "whoosh.wav",            -- a chat message it yanked, flying off
		pop = "pop.wav",                 -- ... and bursting
	},

	-- Colors of what it draws around itself, as "#rrggbb".
	colors = {
		heart = "#c42a1f", glint = "#ece4de",   -- hearts, and the little shine on them
		bang = "#f8c934",                         -- the "!" for alerts
		anger = "#db3329",                        -- the anger mark
		mark = "#ece4de", dots = "#ece4de",      -- "?", and "..." (dots: the same as mark if left out)
		note = "#f8c934", note2 = "#ece4de",      -- music notes (vibing)
		spark = "#ece4de", z = "#ece4de",
	},

	-- The little pixel marks, redrawn if you like: rows of "#" (filled) and "." (empty).
	-- Any you leave out keep the built-in look. Names: z, heart, bang, spark, anger, what, note, dots.
	marks = {
		-- heart = { ".##.##.", "#######", "#######", ".#####.", "..###..", "...#..." },
	},
}
