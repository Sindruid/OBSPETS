# OBSPETS character packs

The Pet source draws whichever character you pick under **Character** in its properties. Each character is a **pack**: a folder holding a `pack.lua`, a sprite sheet and, optionally, sounds.

## Where packs go

| Folder | What is there |
|---|---|
| `%APPDATA%\obs-studio\plugin_config\obspet\packs\` | **Your own characters.** The Pet's properties have a button that opens this folder. |
| `%ProgramData%\obs-studio\plugins\obspet\data\packs\` | The characters that come with OBSPETS. Installing a new version replaces them, so don't edit them here. |

- Every folder with a `pack.lua` in either place shows up under **Character**.
- Folders whose name starts with `_` or `.` are skipped. That is why `_template` isn't listed.
- If one of your packs has the same folder name as a bundled one, yours is used.

| Bundled pack | What it is |
|---|---|
| `blob/` | A small slime drawn from code. It uses the default sounds. |
| `cat-ginger/`, `cat-black/`, `cat-siamese/`, `cat-sphynx/`, `cat-fluffy/`, `cat-tabby/` | Six cats (Ginger, Midnight, Mochi, Pixel, Snow and Tabby) from [LuizMelo's Pet Cats Pack](https://luizmelo.itch.io/pet-cat-pack), CC0. They use the cat sounds. |
| `dog-retriever/`, `dog-akita/`, `dog-great-dane/`, `dog-schnauzer/`, `dog-saint-bernard/`, `dog-husky/` | Six dogs (Goldie, Kuma, Duke, Pepper, Bernie and Nova) from [LuizMelo's Pet Dogs Pack](https://luizmelo.itch.io/pet-dogs-pack), CC0. They use the dog sounds. |
| `_template/` | A commented `pack.lua` to copy. |

Each bundled character made from someone else's art has a `LICENSE.txt` naming the artist and the licence. If you like one, visit the artist's page and leave a tip or a kind comment.

## Making a character in the editor

The easy way. In the Pet's properties, click **Edit characters** (or open **OBSPETS Settings** and click **Characters** at the top). The editor opens in your browser:

- **+ New**, next to the character picker, starts one. A built-in character can be changed too: the first change saves it as your own copy, and the built-in one stays as it was.
- **A sprite sheet with every animation:** drop it anywhere on the page, or click **Add a sprite sheet…**. You only add it once:
  - Each animation is a colored card along the top, playing its frames. The sheet below shows each card's frames in its color.
  - Each row with drawings starts as a card, and its name is a guess. If a preview does something else, pick the right name under it.
  - Click a card, then click its first and last frame on the sheet, or drag across them. A jump pose takes one click.
  - **+** adds an animation or a jump pose. **×** removes one.
  - Sheets from known asset packs are recognised and set up completely, for example Elthen's 2D Pixel Art Cat. Only the layout is built in, never the art, so you still get the sheet from its artist.
  - The sheet is kept with the character, so **Edit sheet**, **Change frames** and **Pick it from your sheet** open it again any time. **Export** leaves it out: a shared character carries only the frames it uses, never the artist's whole sheet.
- **One animation at a time:** drop an image on a slot, or click it and **Choose an image…**. A PNG strip, an animated GIF or several frame files at once all work. Files named after a slot (`fox_walk.gif`, `Run.png`) dropped anywhere on the page go to that slot.
  - Frames are trimmed to the drawing, stood on the floor and centered, so art from different places lines up. Art saved at 2× or 3× is shrunk back to match.
  - **Position** nudges an animation that still sits a pixel off, with idle shown faintly behind it. **Mirror it** turns one drawn the other way.
  - Right-click a frame to drop it. A slot you leave empty borrows another animation, as the cards say.
- **Sounds:** drop a sound on a role, or click **+ Add**. Any format the browser plays works. It is trimmed, set to a stream-friendly loudness and saved as a 48 kHz `.wav`. Click a sound's name to trim it again or change its volume. **Built-in sounds** at the top picks which of OBSPETS's sound sets plays for roles with no sounds of their own: **Soft** (suits any pet), **Cat**, **Dog**, or **None**. A role with no sounds of its own shows the built-in one (▶ previews it); its list can make it **Silent** instead, or share another role's sounds.
- **Details:** the name, what it is and its personality (for chat reactions), which way the drawings face, the body measurements (measured from the animations unless you set them), and walking speed.
- **Try it in OBS** plays a reaction on the Pets that show this character. **Show on my Pet** switches every Pet to it.
- **⋯ → Export** saves the character as a `.zip` to share; **Import** adds one.

Every change saves by itself, and the Pet in OBS reloads it straight away. The character's `.history/` folder keeps how it looked before each editing session (a session ends after 10 minutes without a change), for its last 10 sessions. A deleted character goes to `.trash/` in your packs folder, and stays there for 30 days.

A character made in the editor is a folder like any other pack, plus the editor's own files:

| File | What |
|---|---|
| `pack.json` | What the editor knows: frames, speed, nudges, sounds, details. |
| `anims/<slot>.png` | One strip per animation, and per jump pose with its own drawing. |
| `sounds/<role>/*.wav` | The sounds. |
| `source/sheet.png` | The sprite sheet the animations were picked from, if they were. `pack.json`'s `source` says which cells each one uses. Not exported. |
| `sheet.png`, `pack.lua` | Made from the files above every time it saves. Don't edit these by hand: the next save writes them again. |

To work on the editor without OBS, run `node tools/editor-dev.mjs` in the plugin's folder and open the address it prints.

## Making a character by hand

1. Copy `_template/` into your own packs folder and rename it, for example to `fox/`. Put your sprite sheet in it.
2. Fill in `pack.lua`. You need at least `sheet` and an `idle` animation.
3. In OBS, open the Pet's properties, pick **fox** under **Character**, and look at the lines under the list. They show what loaded, which animations are borrowing another one, and any problems. The same messages go to OBS's log (**Help → Log Files**).
4. After each edit to the pack or the sheet, click **Reload character**. You don't need to restart OBS.

`pack.lua` holds data only. It runs in a sandbox with no access to files, OS functions, `require`, `load` or Lua's pattern matching (`string.find`, `match`, `gmatch`, `gsub`). It is stopped if it runs too long or uses more than 32 MB, and every number in it is kept within sensible limits. So a pack from someone else can't do any harm. Paths in it are relative to the pack's folder. Absolute paths work too.

### The sprite sheet

- Use one PNG with every frame in a grid of equal cells. Set the cell size with `cell = { w, h }`, or `cell = 32` for a square cell.
- Draw the character **facing one way** (`faces = "right"` or `"left"`). The Pet mirrors it when it turns around.
- Put the **feet on the bottom edge** of the cell. If there is empty space under the feet, give it as `feet = <pixels>`.
- The pet's own colors can be changed without redrawing it. The editor sets these under **Colors → The pet**; the marks keep their `colors`. They apply in this order:
  - `recolor = { ["#ffffff"] = "#e8b25e", ... }` swaps exact colors of the sheet for others.
  - `hue = <degrees>` (-180 to 180) turns the colors around the color wheel, the same way as CSS `hue-rotate()`.
  - `saturation = <n>` (0 grey, 1 as drawn, 2 twice as vivid) and `contrast = <n>` (1 as drawn).
  - `lightness = <n>` (-1 black, 0 as drawn, 1 white).
  - `tint = "#rrggbb", tint_amount = <0..1>` multiplies a color in. This is the one that colors a white or grey pet: white becomes the tint and the shading stays.
- Keep the body centered across the cell, so that turning around doesn't make it jump sideways.
- Size: the Pet's **Size** setting is screen pixels per sprite pixel, so the sprites stay crisp. Movement is tuned for a 32 px tall character and scales with the cell height. A 64 px tall pet jumps twice as high and walks twice as fast as a 32 px one, so it looks the same.

### Animations

| Name | Used for | Missing? |
|---|---|---|
| `idle` | Standing about. **Required.** | |
| `look` | Watching chat, alerts, listening | Borrows `idle`. Curious and Unimpressed are off |
| `walk` | Wandering, going to the chat | Borrows `idle` |
| `run` | Zoomies, raids, dashing to the chat | Borrows `walk`. Zoomies is off |
| `sleep` | Naps, with z's | Borrows `idle`. Naps is off |
| `paw` | A swat. `whoosh` = the frame the swipe sound plays on, `hit` = the frame sparks fly | Borrows `idle`. Plays, Swats at chat and Rips a message out are off |
| `clean` | Content: grooming or panting, after alerts and when petted | Borrows `idle` |
| `clean2` | A second content loop: idling, soaking up hearts | Borrows `clean` |
| `stretch` | Before a nap, turning its back on chat | Borrows `clean` |
| `call` | A bark or meow. Plays only when its voice sound does, so it never calls out silently | Skipped |
| `beg` | Begging chat for attention | Begs is off |
| `scared` | Startled, scared off | Startled and Scared are off |
| `angry` | Cross at chat | Angry is off |

A reaction that needs an animation the character doesn't have stays off, and the Pet's settings say what it needs. A borrowed one would look wrong: a swat that's just idling, a dog that barks without a sound. To use one you do have, name it instead: `angry = "scared"`. The editor calls that a stand-in.

Each animation is written in one of three ways:

- `{ row = 4, frames = 8, fps = 10 }`: frames left to right along a row, counted from 0. Add `col = n` to start further along the row.
- `{ 4, 8, 10 }`: the same thing, shorter.
- `{ cells = { {0, 4}, {1, 4}, {3, 2} }, fps = 10 }`: any cells, as `{column, row}`.

**Poses** are single frames held during jumps, written `{ row = .., col = .. }`:

| Pose | When it shows |
|---|---|
| `crouch` | Before a jump, and the wiggle before a pounce |
| `rise` | Going up |
| `fall` | Coming down |
| `land` | Touching down |

Any pose you leave out falls back to another (land → crouch, fall → rise → crouch, crouch → the first idle frame).

### Body

The `body` measurements are in sprite pixels, from the middle of the cell's bottom edge. They place the hearts and marks over the head, the sparks at the paw, the z's, the label and the shadow, and set how close the character walks to things:

| Field | Meaning |
|---|---|
| `half` | Half the body's width |
| `top` | The top of the head |
| `top_asleep` | The top of the head while sleeping |
| `head` | How far the head is in front of the body's middle |
| `paw` | Where a swat lands: `{ forward, up }` |
| `shadow` | Half the shadow's width |

If you leave `body` out, the defaults are scaled to your cell height. They fit a character about 18 px wide and 15 px tall in a 32 px cell. `speed = { walk = 24, run = 70 }` sets the walking and running speed in sprite pixels a second. Those are the defaults for a 32 px cell, and they also scale with the height.

### Who it is (chat moods)

When you're signed in, the Pet can react to your chat. It sends recent chat to sindruid.com, and an AI picks one of the Pet's reactions. These three optional fields tell the AI who your character is:

| Field | Meaning |
|---|---|
| `describe` | What the pet is, up to 160 characters, for example `"a small white cat"` |
| `pronouns` | `"he"`, `"she"`, `"they"` or `"it"`. The default is `"they"`. |
| `personality` | How it behaves and what it reacts to, up to 500 characters, for example `"proud, a little dramatic, always up for a nap, scared of dogs"` |

If you leave them out, the pet is "a small pet" that is affectionate but proud. These fields only describe the pet: the AI still only ever picks one of the Pet's reactions, so nothing written here ends up on screen.

### Sounds

Sounds are given by **role**, so the same behavior works for a cat, a dog or a slime. Each role can be:

- one file,
- a list of files (one is picked at random, never the same one twice in a row),
- the name of another role, to share its files,
- or `false`, to keep it silent.

**Built-in sounds.** A role you leave out plays OBSPETS's own sound for it, so a character with no sounds still has some. `sound_set` picks which set:

| `sound_set` | What it is |
|---|---|
| `"default"` (or left out) | Soft, wordless "mm" calls and light, clean foley, made to suit any pet |
| `"cat"` | Meows, a trilled chirp, a purr and a hiss. Its body sounds are the default ones. |
| `"dog"` | Woofs, a yip, whines, a content grunt and a small growl. Its body sounds are the default ones. |
| `"none"` | No built-in sounds: a role you leave out is silent |

A role whose files are all missing plays the built-in sound too, and the Pet's properties list the missing files. The sets live in the plugin's `data/sounds/<set>/<role>/`; a set without a role uses the default set's.

Files are looked for in `sound_dir`, which is relative to the pack's folder. If you don't set it, they are looked for in the pack's folder itself.

| Role | Kind | When it plays |
|---|---|---|
| `pet` | voice | A channel-point pet, and love from chat. It also plays while the pet is asleep. |
| `notice` | voice | A follow, bits, the start of a raid, zoomies, a greeting |
| `cheer` | voice | A sub, a gift, the raid's welcome hop, after a pounce |
| `question` | voice | Curious, a hello, startled |
| `plead` | voice | Begging for attention |
| `angry` | voice | Angry or scared |
| `swipe` | foley | Each swat |
| `land` | foley | Landing a jump |
| `pounce` | foley | A heavier landing |
| `fling` | foley | A yanked chat message flying off |
| `pop` | foley | ... and bursting |

- **Format:** use `.wav` files, ideally stereo 48 kHz. 16-bit, 24-bit and 32-bit float all work, at any sample rate. The OBSPETS plugin plays only `.wav`; the Pet's properties list any other file as a problem.
- **Loudness:** about −38 LUFS for voice and −41 LUFS for foley sits nicely under a stream. The OBSPETS source plays its sounds itself, so its fader in OBS's Audio Mixer sets the overall volume. By default you and your stream both hear every sound, like an alert sound; the settings page can change that for each reaction.
- **Voice and foley** can overlap: a swat doesn't cut a meow short.

### Colors and marks

- **Colors:** `colors` sets the color of everything the pet draws around itself, as `"#rrggbb"`: `heart` and its `glint`, the alert "!" (`bang`), the `anger` mark, "?" (`mark`), "..." (`dots`, the same as `mark` unless set), music notes (`note`, `note2`), `spark` and `z`.
- **Marks:** `marks` redraws any of the little pixel marks as rows of `#` (filled) and `.` (empty). The marks are `z`, `heart`, `bang`, `spark`, `anger`, `what`, `note` and `dots`.

## Sharing a pack

To share a pack, zip its folder. Only share art and sounds whose license allows it. Many asset-store licenses let you use the art on your own stream but not pass the files on to others.
