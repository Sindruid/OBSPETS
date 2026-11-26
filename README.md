# OBSPETS plugin

OBSPETS is an OBS Studio plugin (Windows only) that adds a pet to your scene. It reacts to follows, subs, bits, raids, channel points and chat on Twitch and Kick.

It adds:

- **OBSPETS** (`obspet`): the pet source.
- **OBSPETS Chat** (`obspet_chat`): an on-screen Twitch and Kick chat the pet can sit on and pull messages out of.
- Two items in the **Tools** menu: **OBSPETS: Add a pet to this scene** and **OBSPETS: Settings**. The settings page has two views: **Sources** (each OBSPETS and OBSPETS Chat source) and **Characters** (the character editor).

Characters are packs of a `pack.lua`, a sprite sheet and optional sounds (see `data/packs/README.md`).

Sign-in (Twitch, Kick or both) and the chat reactions go through the Stream Tools backend at sindruid.com/streamtools, which is a separate service and not part of this source. Twitch events come over EventSub. Kick events come over Kick's public chat socket (Pusher), which carries chat, subs, gifts, hosts, Kicks and channel-point redemptions but not follows.

The build is based on [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate), with only its Windows parts kept. Two changes from the template:

- `cmake/windows/helpers.cmake` installs `obspet.dll` straight into `obspet/` instead of `obspet/bin/64bit/`, which OBS 33 marks as legacy.
- `cmake/common/osconfig.cmake` refuses to configure on anything but Windows.

## Layout

| Path | What |
|---|---|
| `src/` | The plugin. `plugin-main` registers everything. `pet-source.cpp`, `pet.cpp` and `pet-render.cpp` are the pet source, its behavior and its drawing. `chat-source.cpp` is OBSPETS Chat, and `chat-server.cpp` is the loopback HTTP server its page talks back to. `editor.cpp` is the character editor's API. `updater.cpp` is the Update card. `pack.cpp` loads packs. `hub.cpp` holds the account, Twitch EventSub, Kick's chat socket and chat reactions. `net.cpp` does HTTP and WebSockets over OBS's libcurl. |
| `deps/lua/` | Lua 5.4.7's library (MIT), built as a static library and used only to read `pack.lua` in a sandbox. Don't edit it. |
| `data/` | Installed to `obspet/data/`: `locale/`, OBSPETS Chat's page (`chat/`), the settings page (`settings/`), the character editor that is its Characters view (`editor/`: its script and styles, which the settings page loads), the Update card (`update/`), the bundled packs (`packs/`) and the built-in sounds (`sounds/`, see `data/sounds/SOURCES.md`). |
| `tools/package.py` | Builds the installer and the zip. |
| `tools/fake-updater.mjs` | A pretend update for the dev servers, to work on the Update card (`data/update/`) without OBS: `FAKE_UPDATE=9.9.0 node tools/settings-dev.mjs` (see the file for failures and the other states). |
| `tools/editor-dev.mjs` | Serves the settings page's Characters view with a fake editor API, to work on `data/editor/` without OBS (`node tools/editor-dev.mjs`). |
| `tools/installer/` | The Inno Setup installer (`obspet.iss`) and its art (`make-art.py`). |
| `tools/zip/` | The text files that ship in the zip. |

## How the chat and the pet work together

| Chat | Pet sits on the newest message | Pet rips a message out |
|---|---|---|
| **OBSPETS Chat** | Yes | Yes: the real line comes out of the chat, in the chat's own style |
| Any other chat overlay | Off by default. "Sit on top of this chat anyway" finds the text by reading the overlay's pixels 4 times a second. A chat with a solid background reads as its top edge. | Off by default. "Rip messages out of this chat anyway" flings a copy, in the pet's font. The real line stays. |

- "Find it by itself" prefers a visible OBSPETS Chat, then a source with "chat" in its name or URL.
- Auto mod deletes spam on the platform (Twitch Helix or Kick's public API) as soon as it's caught, so it doesn't depend on the overlay. An OBSPETS Chat is told first and keeps the line crossed out for the pet to rip out, then drops it after 20 s if no pet does.
- **Auto mod** has its own page in a Pet's settings. Its on/off switch and categories belong to the account (sindruid.com keeps them; OBS saves them with `POST /streamtools/api/pet/config`), plus a per-source switch. It runs whether or not the pet reacts to chat: with chat reactions off, the model is offered only "none" and "yank", and nothing is asked when Auto mod can't delete anything. **On stream** picks whether a pet rips a deleted line out of the chat overlay (crossed out) or the line just drops out (`automod_show`). Either way the message is deleted on the platform at once. Before 0.6, "Auto mod only" hid the pet: a source with it set now has chat reactions off and quiet deletes.

OBSPETS Chat is the page in `data/chat/`, loaded in a private browser source.

- It reads Twitch chat anonymously over IRC and Kick chat over Kick's public socket, in one list. Each channel defaults to the signed-in account's.
- Chatters it hides are also ignored by its pet.
- It has the usual overlay options: fonts, colors, outline, readable name colors, badges, pronouns (pronouns.alejo.io), 7TV/BTTV/FFZ emotes, highlights, filters, history on load (recent-messages.robotty.de), animations and custom CSS.
- **Plugin to page:** obs-browser's `javascript_event`, with the events `obspet_config`, `obspet_yank`, `obspet_yank_done`, `obspet_strike` and `obspet_line`.
- **Page to plugin:** a browser source can't call into a plugin, so `chat-server.cpp` listens on `127.0.0.1` on a free port. It serves `data/chat/`, `data/settings/` and `data/editor/`, and takes `POST /chat/api/report` (where the messages are, and a picture of the line the pet is about to yank). API calls need the per-run key the page gets in its URL.

## Pet properties

- **Sign-in status** at the top, with the next step. If a sign-in is missing a permission added later, it says so and offers to sign in again (`Hub::check_scopes`).
- **Who hears it:** you and your stream, only your stream, or nobody. This is the source's audio monitoring. On OBS 32 and newer there's no echo to worry about: when Desktop Audio captures the device you monitor on, OBS gives the stream the pet once. On OBS 31 the page also offers "only you" and warns about the echo.
- **Try a reaction** plays reactions for testing. "Someone says hi in chat" goes through the real chat model and uses a little credit.
- **Moved or resized:** the pet source is meant to fill the canvas. If its box is moved or scaled, it still maps the chat and other sources into its own pixels, and offers **Fit it to the screen**.
- **How often it reacts to chat:** Rarely, Sometimes or Often (at most every 60, 15 or 8 s).
- **Pets:** up to 6 in one source, each with its own character, name, size and chance, sharing the rest of the settings.
  - **Who reacts** to an alert or channel points: take turns, random, or all of them.
  - **On screen at once:** with fewer places than pets, they swap every N minutes and/or after an alert. The one leaving runs off and the next jumps on from that side.
  - **Reactions:** a per-pet list to turn individual reactions off.
  - Settings keys: `pets`, `who`, `at_once`, `swap_min`, `swap_alert`, `next_in`. Pet 1 uses `pack`/`name`/`scale`/`weight`, pet 2 `pack2`/`name2`/…, and so on.
- With several OBSPETS sources, chat reactions go to one that's visible in the program.

Settings saved by versions before 0.5 are migrated when a pet loads.

## Updating from inside OBS

The settings page shows an **Update available** card when sindruid.com has a newer OBSPETS (`src/updater.cpp`, `data/update/update.js`). The plugin asks `GET /streamtools/api/pet/update` soon after OBS starts and every 6 hours.

Clicking the card explains what happens. **Update and restart OBS** then:

1. Saves what the page hasn't saved yet.
2. Downloads the installer with this OBS's sign-in and checks its SHA-256.
3. Starts it in update mode (`/VERYSILENT /OBSPETUPDATE=<update.txt>`, see the top of `obspet.iss`). Windows asks for permission. If it gets none, nothing changes.
4. Once the installer has permission, it writes a `ready` file. The plugin then leaves a handoff for the next OBS (`update-handoff.json`: the loopback port, the key encrypted with DPAPI, the open pages) and closes OBS the normal way, so scenes are saved.
5. The installer waits for OBS to be gone (2 minutes at most), installs, and opens the same OBS again with its own command line (without `--startstreaming` and the like), not elevated.
6. The new OBS listens on the same port with the same key, so the open pages reconnect and reload into the new version. If the port is taken, it opens them again instead. They say whether the update installed.

It refuses while OBS is streaming, recording or running the replay buffer or virtual camera, or while another OBS is open. A copy that isn't in `%ProgramData%\obs-studio\plugins\obspet` (installed by hand) gets a link to the download page instead. Files go to `%TEMP%\obspet-update\`, including `setup.log`.

To publish an update, bump `buildspec.json`, rebuild, run `tools/package.py`, then deploy the backend (`npm run deploy` copies the newest installer and its SHA-256).

## Known limits

- Two OBS instances signed in to one account both get every event, so chat reactions use credit twice.
- Extra canvases (OBS 31+ vertical setups) aren't supported. The pet uses the main canvas.
- The sign-in (`account.json`) is encrypted with DPAPI for the Windows user, so it can't be copied to another PC or user.
- Uninstalling deletes the local sign-in but doesn't sign the OBS out on the website.

## Building (Windows x64)

You need Visual Studio 2022 with **Desktop development with C++** and Windows SDK 10.0.22621, and CMake 3.28 or newer (Visual Studio ships one).

```powershell
cmake --preset windows-x64          # once: downloads OBS 31.1.1 sources and obs-deps into .deps/
cmake --build --preset windows-x64  # RelWithDebInfo
```

This writes `build_x64/RelWithDebInfo/obspet.dll`, plus a copy with its data in `build_x64/rundir/RelWithDebInfo/`.

Don't run `cmake --install` without `--prefix`: the default prefix is `C:\ProgramData\obs-studio\plugins`, the live OBS plugin folder. To install, use:

```powershell
cmake --install build_x64 --config RelWithDebInfo --prefix <folder>
```

That gives `<folder>/obspet/obspet.dll` and `<folder>/obspet/data/`, the layout OBS 33 loads from `%ProgramData%\obs-studio\plugins\`.

To try it, close OBS, install into `%ProgramData%\obs-studio\plugins`, start OBS, and look for `[obspet]` lines in **Help → Log Files**.

The plugin talks to `https://sindruid.com` unless the environment variable `OBSPET_BACKEND` points elsewhere. The account is stored in `%APPDATA%\obs-studio\plugin_config\obspet\`. Sign out before switching backends.

## Packaging

The installer needs [Inno Setup](https://jrsoftware.org/isinfo.php) 6.7 or newer.

```powershell
python tools/package.py                    # newest dll in build_x64 -> dist/OBSPETS-<version>-setup.exe and -windows-x64.zip
python tools/package.py --source-url URL   # override the source link in SOURCE.txt (default: github.com/Sindruid/OBSPETS)
python tools/package.py --dll path\to\obspet.dll --no-pdb
python tools/package.py --no-installer     # only the zip
python tools/package.py --sign "signtool sign /fd sha256 /tr <timestamp url> /td sha256 /a $f"
```

- The version comes from `buildspec.json`. Bump it there and rebuild so the dll carries it too.
- The script checks the dll is an x64 build, warns if `src/` changed since it was built, and refuses packs that are marked private or point outside their own folder.
- Only bundle art and sounds whose license allows redistribution.

The installer copies `obspet/` into `%ProgramData%\obs-studio\plugins\obspet` and removes files an older version left behind. It never closes OBS itself (that could end a stream); it asks you to close it. Uninstalling signs this OBS out and removes the plugin folder, but keeps the user's packs and settings in `%APPDATA%\obs-studio\plugin_config\obspet`. The test switches at the top of `obspet.iss` install into a folder of your choice for the current user only.

The zip is for installing by hand: `README.txt`, `SOURCE.txt`, `THIRD-PARTY.txt`, `LICENSE.txt`, and the `obspet/` folder.

## Licensing

- The plugin links libobs (GPLv2), so it is licensed **GPLv2 or later** (`LICENSE`). It ships as `LICENSE.txt` in the zip and in the installed plugin folder, with `SOURCE.txt` saying where the source is.
- **Lua 5.4** is MIT. Its notice ships in `THIRD-PARTY.txt`.
- The backend at sindruid.com is a separate service that the plugin only talks to over HTTP. Character packs (art and sounds) are data the plugin loads, under their own licenses (see each pack's `LICENSE.txt`). The default `blob` character is generated from code.
