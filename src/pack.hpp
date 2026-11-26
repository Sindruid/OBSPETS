// Character packs: a folder with a pack.lua (sprite sheet layout, animations, sounds, colors,
// body measurements, and how the mood model should see the pet) next to its sheet and sounds.
// See data/packs/README.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace obspet {

struct Glyph {
	int w = 0, h = 0;
	std::vector<std::array<int, 3>> runs;   // x, y, length
};
struct Cell {
	int col = 0, row = 0;
};
struct Anim {
	std::vector<Cell> cells;
	double fps = 8;
	int whoosh = 0, hit = 0;   // (paw) the frames the swipe sound plays on and sparks fly
	int n() const { return (int)cells.size(); }
};
struct Rgb {
	float r = 1, g = 1, b = 1;
};
// How the sprite sheet's colors are changed before it is shown, in this order (each pixel's alpha
// stays): exact colors swapped (recolor), then hue, saturation, contrast, lightness, and a tint
// multiplied in (which is what colors a white or grey pet). The editor previews the same steps.
struct Look {
	std::map<uint32_t, uint32_t> recolor;   // 0xRRGGBB -> 0xRRGGBB
	double hue = 0;          // degrees, -180..180, as CSS hue-rotate()
	double saturation = 1;   // 0 grey .. 2
	double contrast = 1;     // 0 flat grey .. 2
	double lightness = 0;    // -1 black .. 1 white
	Rgb tint;                // multiplied in by tint_amount
	double tint_amount = 0;  // 0..1
	bool any() const
	{
		return !recolor.empty() || hue != 0 || saturation != 1 || contrast != 1 || lightness != 0 || tint_amount > 0;
	}
};
struct SoundRole {
	std::vector<std::string> files;
	bool fallback = false;   // the plugin's default sound for this role (data/sounds/<role>/), not the pack's own
};

struct Pack {
	std::string id, dir, name, sheet;
	std::string describe, pronouns, personality;   // for chat moods
	int cw = 32, ch = 32;
	bool faces_left = false;
	double feet = 0, unit = 1;
	Look look;        // the sprite's colors, changed (hue, tint, recolor...)
	double half = 9, top = 15, top_asleep = 10, head = 3, paw_x = 9, paw_y = 8, shadow = 6;
	double walk = 24, run = 70;
	std::map<std::string, Anim> anims;   // every name in ANIMS (missing ones copy their stand-in)
	std::map<std::string, Cell> poses;   // every name in POSES
	std::map<std::string, Rgb> colors;
	std::map<std::string, Glyph> glyphs;
	std::map<std::string, SoundRole> sounds;
	int nsounds = 0;
	std::string sound_set = "default";   // the built-in sounds for roles it doesn't set ("none": silence)
	std::set<std::string> own_anims, own_poses;   // the ones pack.lua gives (the rest are stand-ins)
	std::string borrowed;                 // "look->idle, ..."
	std::vector<std::string> warnings;

	const Anim &anim(const std::string &k) const;
	const Rgb &color(const std::string &k) const;
	const Glyph &glyph(const std::string &k) const;
};
using PackPtr = std::shared_ptr<const Pack>;

extern const char *const ANIMS[9];
extern const char *const POSES[4];
bool is_voice(const std::string &role);
bool is_sound_role(const std::string &role);
// The plugin's own sound sets, data/sounds/<set>/<role>/*.wav ("default": soft, wordless calls
// that suit any pet; "cat": meows, a purr, a hiss). A pack picks one with `sound_set`; any role it
// doesn't set plays the set's sound, or the "default" set's when the set has none for that role.
using SoundSet = std::map<std::string, SoundRole>;
const std::map<std::string, SoundSet> &sound_sets();
const SoundRole *builtin_sound(const std::string &set, const std::string &role);   // nullptr: none

// -> the pack, or nullptr with `err` saying why
std::shared_ptr<Pack> load_pack(const std::string &dir, std::string &err);

// every pack folder: the bundled ones (data/packs) and the user's own (plugin_config/obspet/packs)
struct PackEntry {
	std::string id, path;
	bool user = false;
	bool folder = false;   // (the editor's list only) a Pet shows it from a folder of its own (pack_dir)
};
std::vector<PackEntry> list_packs();
std::string user_packs_dir();
std::string hex_of(const Rgb &c);   // -> "#rrggbb"

} // namespace obspet
