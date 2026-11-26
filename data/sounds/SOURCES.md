# OBSPETS's built-in pet sounds

A character pack plays these for any sound role it doesn't set. Its `sound_set` picks the set (see `../packs/README.md`, "Sounds"). A set without a role uses the `default` set's.

| Set | What it is |
| --- | --- |
| `default/` | Made to suit any pet: soft, wordless "mm" calls for the voice, and light, clean foley |
| `cat/` | A cat's voice: meows, a trilled chirp, a purr and a hiss (the body sounds come from `default/`) |
| `dog/` | A dog's voice: woofs, a yip, whines, a content grunt and sigh, and a small growl (the body sounds come from `default/`) |

All files are stereo 48 kHz 16-bit `.wav`. Voice sits at about −38 LUFS and foley at −40 to −42 LUFS (the landing thud is quieter, at −48), the loudness `../packs/README.md` recommends. They were processed with `tools/audio/sfx.py` in the stream-tools repo.

These sounds are not covered by OBSPETS's GPL (see `tools/zip/THIRD-PARTY.txt`). The library files are royalty-free and need no attribution. They ship here only as part of OBSPETS, edited.

## `default/` voice: synthesised

`pet/`, `notice/`, `cheer/`, `question/`, `plead/`, `angry/` are made by `tools/render-pet-voice-audio.mjs` in the stream-tools repo. Each one is a hummed "oo" with a breath at the onset and a light vibrato. The pitch contour carries the feeling:

| Role | Sound |
| --- | --- |
| `pet` | A warm hum with a slight purr, or "mm-hmm" |
| `notice` | A quick "mp!" that pops up (3 pitches) |
| `cheer` | A bouncy rising "mi-mi-mii!" or "mi-mii!" |
| `question` | "mm?" turning up, or "m, mm?" |
| `plead` | A soft falling "mmm~", or "mm, mmm~" |
| `angry` | A low, rough grumble, then a huff |

After rendering: a 120 Hz high-pass, then −38 LUFS.

## `default/` foley: from the sound library

| File | Library file | Processing |
| --- | --- | --- |
| `swipe/swipe-1..3.wav` | Fab, Hove Audio – Whooshes SFX Pack LITE, `Extra Fast/Light/MOVEActv_ExtraFastLight_HA_WHOOSH_01` / `_03` / `_06.wav` | First 0.35 s, trimmed, 250 Hz–9 kHz, −42 LUFS |
| `fling/fling-1.wav` | Fab, Hove Audio – Whooshes SFX Pack LITE, `Short/Standard/MOVEActv_Short_HA_WHOOSH_08.wav` | First 0.5 s, trimmed, 300 Hz–9 kHz, −42 LUFS |
| `fling/fling-2.wav` | Sonniss GDC 2019, Airborne Sound – Organic Whooshes, `Whoosh,Organic,Fabric,Glove,Airy,Breathy,Punchy.wav` | First 0.42 s, trimmed, 350 Hz–8.5 kHz, about −39 LUFS |
| `land/land.wav` | A footstep from freesound.org, CC0 | 0.015–0.30 s, 45 Hz–2.5 kHz (drops an MP3 hiss band), 110 ms fade-out, −48 LUFS: a soft, low thud |
| `pounce/pounce-1.wav` | PAT plus Sonniss GDC 2020, Shapeforms – Hit & Punch, `IMPACT_LOW_THUD_10.wav` (first 0.09 s, 90 Hz–1.5 kHz, −11 dB) | Layered, −40 LUFS |
| `pounce/pounce-2.wav` | PAT plus Shapeforms – Hit & Punch, `PUNCH_DESIGNED_LIGHT_78.wav` (0.06–0.18 s, 70 Hz–2.5 kHz, −10 dB) | Layered, −40 LUFS |
| `pop/pop-1..3.wav` | Sonniss GDC 2024, Rogue Waves – Kawaii UI, `UIClick_Hand Pop UI Diminished 1_RogueWaves_KawaiiUI.wav` (its first three pops) | One pop each, 200 Hz–10 kHz, −41 LUFS |
| `pop/pop-4.wav` | Sonniss GDC 2023, SmartSoundFX – Cartoon & Infotainment, `PLOP Mouth Drip Dry.wav` | 0.025–0.16 s, 200 Hz–9 kHz, −41 LUFS |

PAT is one soft pat from Sonniss GDC 2026, InMotionAudio – Foley T-Shirt, `FOLYClth_SinglePats04_InMotionAudio_FoleyT-Shirt.wav`: 0.095–0.30 s, 140 Hz–7 kHz.

## `cat/`: from the sound library

The calls come from two clean cats in a similar register: FIONA is Sonniss GDC 2020, Soundholder – Cat – Meow Library, `animals cat Fiona 31` / `63 meow mono`, and MWCAT is Sonniss GDC 2024, Mechanical Wave – Sound Effects Collection, `ANMLCat_Cat Moew_ 03_MWSFX_SEC.wav`.

| File | Built from |
| --- | --- |
| `pet/purr.wav` | FIONA 63, 3.75–5.6 s: two breaths of a real purr. 85 Hz–9 kHz, −39 LUFS |
| `notice/mew-1.wav`, `cheer/mew.wav` | FIONA 31, 0–0.72 s: a short, bright meow. 380 Hz–12 kHz, −37.5 LUFS |
| `notice/mew-2.wav` | MWCAT 1.33–1.95 s: a short meow. 300 Hz–11 kHz, −37.5 LUFS |
| `notice/mew-3.wav` | MWCAT 0.57–1.12 s: a rising meow. 300 Hz–11 kHz, −37.5 LUFS |
| `cheer/mrrow.wav` | FIONA 63, 0.455–1.08 s: a lower "mrrow". 300 Hz–12 kHz, −37.5 LUFS |
| `question/chirp.wav` | MWCAT 0.13–0.385 s: a trilled "mrrp". 300 Hz–10 kHz, −38.5 LUFS |
| `plead/beg.wav` | MWCAT 2.26–3.19 s: a long "mrrr-meeow". 300 Hz–11 kHz, −37.5 LUFS |
| `plead/meow.wav` | Soundholder – Cat – Meow Library, `animals cat blue meow 14 mono 96kHz.wav` 0.31–0.93 s: a plaintive "meeow", cut before a click. 300 Hz–10 kHz, 120 ms fade-out, −37.5 LUFS |
| `angry/hiss.wav` | Sonniss GDC 2018, Gamemaster Audio – Animal Sounds, `snake_hiss_03.wav` 0.98–1.385 s, shaped like a cat's hiss: 1.5–10 kHz, +3 dB at 3.5 kHz, a 30 ms hit that dies away. −39 LUFS |

## `dog/`: from the sound library

Three recordings: BARKS is Sonniss GDC 2026, 344 Audio – Dog Vocalisations Vol. 1, `ANMLDog_Dog Barks, Multiple, Indoors, Perspective,_344 Audio_Dog Vocalisations_02.wav`; GRUNT is the same pack's `ANMLDog_Dog Shuffle, Grunt, Movement, Lying Down_344 Audio_Dog Vocalisations.wav`; COLLIE is Sonniss GDC 2020, Soundopolis – Dogs, `Dog_Border Collie Mix_Whining and Bark_Fienup_001.wav`; YORKIE is the same pack's `Dog_Yorkshire Terrier_Growl_Fienup_001.wav`. Each cut is one call, with a short fade-out to drop the room.

| File | Built from |
| --- | --- |
| `notice/woof-1.wav` | BARKS 0.24–0.72 s: one bark. 200 Hz–12 kHz, 140 ms fade-out, −39 LUFS |
| `notice/woof-2.wav` | BARKS 1.62–2.12 s: one bark. Same processing |
| `notice/woof-3.wav` | BARKS 3.52–4.05 s: one bark. 150 ms fade-out, −39 LUFS |
| `cheer/woof.wav` | BARKS 4.86–5.40 s: one bark. 150 ms fade-out, −39 LUFS |
| `cheer/yip.wav` | COLLIE 9.10–9.60 s: a bright yip. 250 Hz–12 kHz, 150 ms fade-out, −39 LUFS |
| `question/whine.wav` | COLLIE 0.98–1.42 s: a short rising whine. 300 Hz–12 kHz, −38 LUFS |
| `plead/whine-1.wav` | COLLIE 0.03–0.46 s: a high whine. 300 Hz–12 kHz, about −38 LUFS |
| `plead/whine-2.wav` | COLLIE 3.58–4.10 s: a high whine. 300 Hz–12 kHz, −38 LUFS |
| `pet/grunt.wav` | GRUNT 0.38–0.78 s: a content grunt. 70 Hz–9 kHz, −39 LUFS |
| `pet/sigh.wav` | GRUNT 4.70–5.30 s: a settling sigh. 70 Hz–9 kHz, −39 LUFS |
| `angry/growl.wav` | YORKIE 1.035–1.65 s: a small dog's growl. 90 Hz–9 kHz, −39 LUFS |
