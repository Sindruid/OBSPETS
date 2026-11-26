#include "pack.hpp"
#include "util.hpp"

#include <util/platform.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <set>

namespace obspet {

const char *const ANIMS[9] = {"idle", "look", "clean", "clean2", "walk", "run", "sleep", "paw", "scared"};
const char *const POSES[4] = {"crouch", "rise", "fall", "land"};
static const std::map<std::string, std::string> ANIM_FALLBACK = {
	{"look", "idle"}, {"clean", "idle"}, {"clean2", "clean"}, {"walk", "idle"},
	{"run", "walk"},  {"sleep", "idle"}, {"paw", "idle"},     {"scared", "look"}};
static const std::map<std::string, std::string> POSE_FALLBACK = {
	{"crouch", "idle"}, {"rise", "crouch"}, {"fall", "rise"}, {"land", "crouch"}};
static const std::set<std::string> VOICE = {"pet", "notice", "cheer", "question", "plead", "angry"};
static const std::set<std::string> FOLEY = {"swipe", "land", "pounce", "fling", "pop"};

static const std::map<std::string, std::string> DEFAULT_COLORS = {
	{"heart", "#c42a1f"}, {"glint", "#ece4de"}, {"bang", "#f8c934"}, {"anger", "#db3329"}, {"mark", "#ece4de"},
	{"dots", "#ece4de"},  {"note", "#f8c934"},  {"note2", "#ece4de"}, {"spark", "#ece4de"}, {"z", "#ece4de"}};
static const std::map<std::string, std::vector<std::string>> GLYPH_ROWS = {
	{"z", {"####", "..#.", ".#..", "####"}},
	{"heart", {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."}},
	{"bang", {"##", "##", "##", "##", "..", "##"}},
	{"spark", {"..#..", "..#..", "##.##", "..#..", "..#.."}},
	{"anger", {".#...#.", "##...##", ".......", ".......", "##...##", ".#...#."}},
	{"what", {".###.", "#...#", "...#.", "..#..", ".....", "..#.."}},
	{"note", {"..###", "..#.#", "..#..", "..#..", "###..", "###.."}},
	{"dots", {"##.##.##", "##.##.##"}},
};

bool is_voice(const std::string &role) { return VOICE.count(role) > 0; }
bool is_sound_role(const std::string &role) { return VOICE.count(role) || FOLEY.count(role); }

const Anim &Pack::anim(const std::string &k) const
{
	auto it = anims.find(k);
	return it != anims.end() ? it->second : anims.at("idle");
}
const Rgb &Pack::color(const std::string &k) const
{
	auto it = colors.find(k);
	return it != colors.end() ? it->second : colors.at("mark");
}
const Glyph &Pack::glyph(const std::string &k) const
{
	auto it = glyphs.find(k);
	return it != glyphs.end() ? it->second : glyphs.at("dots");
}

static Glyph make_glyph(const std::vector<std::string> &rows)
{
	Glyph g;
	g.h = (int)rows.size();
	for (int y = 0; y < g.h; y++) {
		const std::string &row = rows[y];
		g.w = std::max(g.w, (int)row.size());
		int x = 0;
		while (x < (int)row.size()) {
			if (row[x] == '#') {
				int s = x;
				while (x < (int)row.size() && row[x] == '#')
					x++;
				g.runs.push_back({s, y, x - s});
			} else {
				x++;
			}
		}
	}
	return g;
}

static bool hex_rgb(const std::string &hex, Rgb &out)
{
	unsigned r, g, b;
	const char *s = hex.c_str();
	if (*s == '#')
		s++;
	if (strlen(s) != 6 || sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3)
		return false;
	out = {r / 255.0f, g / 255.0f, b / 255.0f};
	return true;
}

static std::string slash(std::string dir)
{
	std::replace(dir.begin(), dir.end(), '\\', '/');
	if (!dir.empty() && dir.back() != '/')
		dir += '/';
	return dir;
}
const std::map<std::string, SoundSet> &sound_sets()
{
	static const std::map<std::string, SoundSet> all = [] {
		std::map<std::string, SoundSet> out;
		std::string root = slash(module_data_path("sounds"));
		if (root == "/")
			return out;
		auto wavs = [](const std::string &dir, bool dirs) {
			std::vector<std::string> names;
			if (os_dir_t *d = os_opendir(dir.c_str())) {
				while (os_dirent *e = os_readdir(d)) {
					std::string n = e->d_name;
					if (n.empty() || n[0] == '.')
						continue;
					if (dirs ? e->directory : !e->directory && n.size() > 4 && lower(n.substr(n.size() - 4)) == ".wav")
						names.push_back(n);
				}
				os_closedir(d);
			}
			std::sort(names.begin(), names.end());
			return names;
		};
		for (auto &set : wavs(root, true)) {
			SoundSet ss;
			for (auto *roles : {&VOICE, &FOLEY}) {
				for (auto &k : *roles) {
					std::string dir = root + set + "/" + k + "/";
					SoundRole r;
					r.fallback = true;
					for (auto &n : wavs(dir, false)) {
						r.files.push_back(dir + n);
					}
					if (!r.files.empty())
						ss[k] = r;
				}
			}
			if (!ss.empty())
				out[set] = ss;
		}
		return out;
	}();
	return all;
}

const SoundRole *builtin_sound(const std::string &set, const std::string &role)
{
	auto &all = sound_sets();
	for (const std::string &s : {set, std::string("default")}) {
		auto it = all.find(s);
		if (it == all.end())
			continue;
		auto r = it->second.find(role);
		if (r != it->second.end())
			return &r->second;
	}
	return nullptr;
}

static bool is_abs(const std::string &p)
{
	return (p.size() > 2 && p[1] == ':' && (p[2] == '/' || p[2] == '\\')) || (!p.empty() && p[0] == '/');
}

// ------------------------------------------------------------------ reading Lua tables

// a number from pack.lua: finite and within reason, or `def`
static double sane(double v, double lo, double hi, double def)
{
	return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}
static const int MAX_FRAMES = 512, MAX_GRID = 4096;   // frames in one animation; a sheet's rows and columns

namespace {
struct L {
	lua_State *s;
	// value at the top: field `k` of the table at `idx`, pushed (pop it yourself)
	int field(int idx, const char *k) { return lua_getfield(s, idx, k); }
	std::string str_at(int idx) { return lua_type(s, idx) == LUA_TSTRING ? lua_tostring(s, idx) : ""; }
	bool num_at(int idx, double &out)
	{
		if (lua_type(s, idx) != LUA_TNUMBER)
			return false;
		out = lua_tonumber(s, idx);
		return true;
	}
	std::string get_str(int t, const char *k)
	{
		field(t, k);
		std::string v = str_at(-1);
		lua_pop(s, 1);
		return v;
	}
	bool get_num(int t, const char *k, double &out)
	{
		field(t, k);
		bool ok = num_at(-1, out);
		lua_pop(s, 1);
		return ok;
	}
	bool get_numi(int t, int i, double &out)
	{
		lua_geti(s, t, i);
		bool ok = num_at(-1, out);
		lua_pop(s, 1);
		return ok;
	}
};

// the colors: hue, saturation, contrast, lightness, tint = "#rrggbb", tint_amount, recolor = { ["#from"] = "#to" }
void read_look(L &l, int T, Look &o)
{
	lua_State *s = l.s;
	auto fin = [](double v, double lo, double hi, double def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
	if (l.get_num(T, "hue", o.hue)) {
		o.hue = std::fmod(o.hue, 360.0);
		if (o.hue > 180)
			o.hue -= 360;
		else if (o.hue < -180)
			o.hue += 360;
		if (!std::isfinite(o.hue))
			o.hue = 0;
	}
	if (l.get_num(T, "saturation", o.saturation))
		o.saturation = fin(o.saturation, 0, 4, 1);
	if (l.get_num(T, "contrast", o.contrast))
		o.contrast = fin(o.contrast, 0, 4, 1);
	if (l.get_num(T, "lightness", o.lightness))
		o.lightness = fin(o.lightness, -1, 1, 0);
	std::string t = l.get_str(T, "tint");
	if (!t.empty() && hex_rgb(t, o.tint)) {
		o.tint_amount = 1;
		if (l.get_num(T, "tint_amount", o.tint_amount))
			o.tint_amount = fin(o.tint_amount, 0, 1, 0);
	}
	l.field(T, "recolor");
	if (lua_istable(s, -1)) {
		int rt = lua_gettop(s);
		lua_pushnil(s);
		while (lua_next(s, rt)) {
			Rgb a, b;
			if (lua_type(s, -2) == LUA_TSTRING && lua_type(s, -1) == LUA_TSTRING && hex_rgb(lua_tostring(s, -2), a) &&
			    hex_rgb(lua_tostring(s, -1), b)) {
				auto u = [](const Rgb &c) {
					return ((uint32_t)std::lround(c.r * 255) << 16) | ((uint32_t)std::lround(c.g * 255) << 8) |
					       (uint32_t)std::lround(c.b * 255);
				};
				o.recolor[u(a)] = u(b);
			}
			lua_pop(s, 1);
		}
	}
	lua_pop(s, 1);
}
} // namespace

// an animation: { row = 0, frames = 4, fps = 5 [, col = 0] }, { 0, 4, 5 }, or { cells = {{c, r}, ...}, fps = 5 }
static bool read_anim(L &l, int t, Anim &a)
{
	if (!lua_istable(l.s, t))
		return false;
	t = lua_absindex(l.s, t);
	l.field(t, "cells");
	if (lua_istable(l.s, -1)) {
		int ct = lua_gettop(l.s);
		for (lua_Integer i = 1;; i++) {
			if (lua_geti(l.s, ct, i) != LUA_TTABLE) {
				lua_pop(l.s, 1);
				break;
			}
			double c = 0, r = 0;
			if (!l.get_numi(-1, 1, c))
				l.get_num(-1, "col", c);
			if (!l.get_numi(-1, 2, r))
				l.get_num(-1, "row", r);
			a.cells.push_back({(int)sane(c, 0, MAX_GRID, 0), (int)sane(r, 0, MAX_GRID, 0)});
			lua_pop(l.s, 1);
			if ((int)a.cells.size() >= MAX_FRAMES)
				break;
		}
	}
	lua_pop(l.s, 1);
	if (a.cells.empty()) {
		double row, n = 1, col = 0;
		if (!l.get_num(t, "row", row) && !l.get_numi(t, 1, row))
			return false;
		if (!l.get_num(t, "frames", n))
			l.get_numi(t, 2, n);
		l.get_num(t, "col", col);
		int nf = (int)sane(n, 0, MAX_FRAMES, 1), c0 = (int)sane(col, 0, MAX_GRID, 0), r0 = (int)sane(row, 0, MAX_GRID, 0);
		for (int i = 0; i < nf; i++)
			a.cells.push_back({c0 + i, r0});
	}
	if (a.cells.empty())
		return false;
	double fps = 8;
	if (!l.get_num(t, "fps", fps))
		l.get_numi(t, 3, fps);
	a.fps = fps > 0 ? sane(fps, 0.1, 60, 8) : 8;
	int n = a.n();
	double w, h;
	a.whoosh = l.get_num(t, "whoosh", w) ? (int)sane(w, 0, n - 1, 0) : (int)std::floor(n * 0.34);
	a.hit = l.get_num(t, "hit", h) ? (int)sane(h, 0, n - 1, 0) : std::min(n - 1, a.whoosh + 1);
	return true;
}

static void hook(lua_State *s, lua_Debug *)
{
	luaL_error(s, "pack.lua took too long (it should only return a table)");
}

// pack.lua is data, so it gets little memory: enough for any real pack, never enough to hurt OBS
struct LuaMem {
	size_t used = 0, limit = 32u << 20;
};
static void *lua_alloc_capped(void *ud, void *ptr, size_t osize, size_t nsize)
{
	auto *m = static_cast<LuaMem *>(ud);
	size_t old = ptr ? osize : 0;   // (with no block, osize says what kind of object it is)
	if (nsize == 0) {
		free(ptr);
		m->used -= old;
		return nullptr;
	}
	if (nsize > old && m->used + (nsize - old) > m->limit)
		return nullptr;   // Lua raises "not enough memory", which pcall catches
	void *p = realloc(ptr, nsize);
	if (p)
		m->used = m->used - old + nsize;
	return p;
}
static int lua_panic_quiet(lua_State *s)
{
	warn("pack.lua: %s", lua_type(s, -1) == LUA_TSTRING ? lua_tostring(s, -1) : "an error outside the sandbox");
	return 0;   // (Lua aborts after this. Only the library setup runs unprotected, far below the cap.)
}
// a Lua error's message, whatever the error is (error({}) has none)
static std::string lua_msg(lua_State *s)
{
	const char *m = lua_type(s, -1) == LUA_TSTRING || lua_type(s, -1) == LUA_TNUMBER ? lua_tostring(s, -1) : nullptr;
	return m ? m : "an error that isn't a message";
}

std::shared_ptr<Pack> load_pack(const std::string &dir_in, std::string &err)
{
	std::string dir = slash(dir_in);
	auto P = std::make_shared<Pack>();
	P->dir = dir;
	{
		std::string d = dir.substr(0, dir.size() - 1);
		size_t i = d.find_last_of('/');
		P->id = i == std::string::npos ? d : d.substr(i + 1);
	}
	auto warn_ = [&](const std::string &w) { P->warnings.push_back(w); };

	LuaMem mem;
	lua_State *s = lua_newstate(lua_alloc_capped, &mem);
	if (!s) {
		err = "out of memory";
		return nullptr;
	}
	std::unique_ptr<lua_State, void (*)(lua_State *)> guard(s, lua_close);
	lua_atpanic(s, lua_panic_quiet);
	// only what a data file could want: no io, os, require or load
	luaL_requiref(s, "_G", luaopen_base, 1);
	luaL_requiref(s, "math", luaopen_math, 1);
	luaL_requiref(s, "string", luaopen_string, 1);
	luaL_requiref(s, "table", luaopen_table, 1);
	lua_settop(s, 0);
	// Lua's pattern matching runs in C, where the time limit below can't stop it, and a pattern can
	// take for ever. A data file has no use for it. (This is also the strings' own metatable.)
	lua_getglobal(s, "string");
	for (const char *k : {"find", "match", "gmatch", "gsub", "dump"}) {
		lua_pushnil(s);
		lua_setfield(s, -2, k);
	}
	lua_settop(s, 0);
	lua_sethook(s, hook, LUA_MASKCOUNT, 10000000);

	std::string file = dir + "pack.lua";
	if (luaL_loadfilex(s, file.c_str(), "t") != LUA_OK) {
		err = "can't read " + file + ": " + lua_msg(s);
		return nullptr;
	}
	// the sandbox: a fresh _ENV with a handful of harmless globals
	lua_newtable(s);
	for (const char *g : {"math", "string", "table", "ipairs", "pairs", "tostring", "tonumber", "type"}) {
		lua_getglobal(s, g);
		lua_setfield(s, -2, g);
	}
	lua_setupvalue(s, -2, 1);
	if (lua_pcall(s, 0, 1, 0) != LUA_OK) {
		err = "error in " + file + ": " + lua_msg(s);
		return nullptr;
	}
	lua_sethook(s, nullptr, 0, 0);   // (reading the table below runs no Lua)
	if (!lua_istable(s, -1)) {
		err = file + " must end with `return { ... }`";
		return nullptr;
	}
	L l{s};
	const int T = lua_gettop(s);

	auto in_dir = [&](const std::string &name, const std::string &base) {
		if (name.empty())
			return std::string();
		std::string n = name;
		std::replace(n.begin(), n.end(), '\\', '/');
		return is_abs(n) ? n : base + n;
	};

	P->name = l.get_str(T, "name");
	if (P->name.empty())
		P->name = P->id;
	P->describe = l.get_str(T, "describe");
	P->pronouns = l.get_str(T, "pronouns");
	P->personality = l.get_str(T, "personality");
	P->sheet = in_dir(l.get_str(T, "sheet"), dir);
	if (P->sheet.empty()) {
		err = "pack.lua has no `sheet` (the sprite sheet PNG)";
		return nullptr;
	}
	if (!file_exists(P->sheet)) {
		err = "the sprite sheet isn't there: " + P->sheet;
		return nullptr;
	}
	double v;
	l.field(T, "cell");
	if (lua_istable(s, -1)) {
		double w = 32, h = 0;
		if (!l.get_numi(-1, 1, w))
			l.get_num(-1, "w", w);
		if (!l.get_numi(-1, 2, h))
			l.get_num(-1, "h", h);
		P->cw = (int)sane(w, 0, 2048, 32);
		P->ch = (int)sane(h > 0 ? h : w, 0, 2048, 32);
	} else if (l.num_at(-1, v)) {
		P->cw = P->ch = (int)sane(v, 0, 2048, 32);
	}
	lua_pop(s, 1);
	if (P->cw < 1 || P->ch < 1)
		P->cw = P->ch = 32;
	P->faces_left = l.get_str(T, "faces") == "left";
	l.get_num(T, "feet", P->feet);
	P->feet = sane(P->feet, -2048, 2048, 0);
	read_look(l, T, P->look);
	P->unit = P->ch / 32.0;

	// body: given values are used as they are; defaults scale with the cell height
	struct B {
		const char *k;
		double *dst, def;
	} body[] = {{"half", &P->half, 9}, {"top", &P->top, 15}, {"top_asleep", &P->top_asleep, 10},
		    {"head", &P->head, 3},  {"shadow", &P->shadow, 6}};
	l.field(T, "body");
	int bt = lua_istable(s, -1) ? lua_gettop(s) : 0;
	for (auto &b : body)
		if (!bt || !l.get_num(bt, b.k, *b.dst))
			*b.dst = b.def * P->unit;
	P->paw_x = 9 * P->unit;
	P->paw_y = 8 * P->unit;
	if (bt) {
		l.field(bt, "paw");
		if (lua_istable(s, -1)) {
			l.get_numi(-1, 1, P->paw_x);
			l.get_numi(-1, 2, P->paw_y);
		}
		lua_pop(s, 1);
	}
	lua_pop(s, 1);
	P->walk = 24 * P->unit;
	P->run = 70 * P->unit;
	l.field(T, "speed");
	if (lua_istable(s, -1)) {
		l.get_num(-1, "walk", P->walk);
		l.get_num(-1, "run", P->run);
	}
	lua_pop(s, 1);
	for (double *b : {&P->half, &P->top, &P->top_asleep, &P->head, &P->shadow, &P->paw_x, &P->paw_y})
		*b = sane(*b, -2048, 2048, 0);
	P->walk = sane(P->walk, 0, 5000, 24 * P->unit);
	P->run = sane(P->run, 0, 5000, 70 * P->unit);
	// animations, then stand-ins for the missing ones
	l.field(T, "anims");
	int at = lua_istable(s, -1) ? lua_gettop(s) : 0;
	for (const char *k : ANIMS) {
		if (!at)
			break;
		if (l.field(at, k) != LUA_TNIL) {
			Anim a;
			if (read_anim(l, -1, a)) {
				P->anims[k] = a;
				P->own_anims.insert(k);
			}
			else
				warn_(std::string("animation ") + k + " isn't { row = .., frames = .., fps = .. }");
		}
		lua_pop(s, 1);
	}
	lua_pop(s, 1);
	if (!P->anims.count("idle")) {
		err = "pack.lua needs at least an `idle` animation";
		return nullptr;
	}
	for (const char *k : ANIMS) {
		if (P->anims.count(k))
			continue;
		std::string f = ANIM_FALLBACK.at(k);
		while (!P->anims.count(f))
			f = ANIM_FALLBACK.at(f);
		P->anims[k] = P->anims[f];
		P->borrowed += std::string(P->borrowed.empty() ? "" : ", ") + k + "->" + f;
	}

	l.field(T, "poses");
	int pt = lua_istable(s, -1) ? lua_gettop(s) : 0;
	for (const char *k : POSES) {
		if (!pt)
			break;
		if (l.field(pt, k) == LUA_TTABLE) {
			double row = 0, col = 0;
			if (!l.get_num(-1, "row", row))
				l.get_numi(-1, 1, row);
			if (!l.get_num(-1, "col", col))
				l.get_numi(-1, 2, col);
			P->poses[k] = {(int)col, (int)row};
			P->own_poses.insert(k);
		}
		lua_pop(s, 1);
	}
	lua_pop(s, 1);
	for (const char *k : POSES) {
		if (P->poses.count(k))
			continue;
		std::string f = POSE_FALLBACK.at(k);
		while (f != "idle" && !P->poses.count(f))
			f = POSE_FALLBACK.at(f);
		P->poses[k] = f == "idle" ? P->anims["idle"].cells[0] : P->poses[f];
	}

	// colors and marks
	l.field(T, "colors");
	int ct = lua_istable(s, -1) ? lua_gettop(s) : 0;
	for (auto &kv : DEFAULT_COLORS) {
		Rgb c;
		hex_rgb(kv.second, c);
		if (ct) {
			std::string h = l.get_str(ct, kv.first.c_str());
			if (!h.empty() && !hex_rgb(h, c))
				warn_("color " + kv.first + " isn't \"#rrggbb\"");
		}
		P->colors[kv.first] = c;
	}
	// "..." had the "?"'s color (mark) before it got its own: a pack that doesn't set it keeps that
	if (!ct || l.get_str(ct, "dots").empty())
		P->colors["dots"] = P->colors["mark"];
	lua_pop(s, 1);
	l.field(T, "marks");
	int mt = lua_istable(s, -1) ? lua_gettop(s) : 0;
	for (auto &kv : GLYPH_ROWS) {
		std::vector<std::string> rows;
		if (mt && l.field(mt, kv.first.c_str()) == LUA_TTABLE) {
			for (lua_Integer i = 1;; i++) {
				if (i > 32 || lua_geti(s, -1, i) != LUA_TSTRING) {
					if (i <= 32)
						lua_pop(s, 1);
					break;
				}
				rows.push_back(std::string(lua_tostring(s, -1)).substr(0, 64));
				lua_pop(s, 1);
			}
		}
		if (mt)
			lua_pop(s, 1);
		P->glyphs[kv.first] = make_glyph(rows.empty() ? kv.second : rows);
	}
	lua_pop(s, 1);

	// sounds: role = "file.wav", { "a.wav", "b.wav" }, or another role's name to share its files
	std::string sdir = dir;
	{
		std::string sd = l.get_str(T, "sound_dir");
		if (!sd.empty())
			sdir = slash(in_dir(sd, dir));
	}
	// A role the pack doesn't mention plays the built-in sound from its `sound_set` (default
	// "default"; `role = false` keeps one role silent, `sound_set = "none"` all of them).
	{
		std::string set = lower(trim(l.get_str(T, "sound_set")));
		if (!set.empty())
			P->sound_set = set;
		if (P->sound_set != "none" && !sound_sets().count(P->sound_set)) {
			warn_("sound_set " + P->sound_set + " isn't one of OBSPETS's sound sets: using the default sounds");
			P->sound_set = "default";
		}
	}
	auto use_default = [&](const std::string &k) {
		if (P->sound_set == "none")
			return;
		if (const SoundRole *r = builtin_sound(P->sound_set, k))
			P->sounds[k] = *r;
	};
	l.field(T, "sounds");
	int st = lua_istable(s, -1) ? lua_gettop(s) : 0;
	if (!st) {
		for (auto &k : VOICE)
			use_default(k);
		for (auto &k : FOLEY)
			use_default(k);
	} else {
		std::set<std::string> done;
		std::function<void(const std::string &, int)> role = [&](const std::string &k, int depth) {
			if (done.count(k))
				return;
			done.insert(k);
			int ty = l.field(st, k.c_str());
			if (ty == LUA_TNIL) {
				lua_pop(s, 1);
				use_default(k);
				return;
			}
			std::vector<std::string> names;
			if (lua_type(s, -1) == LUA_TSTRING) {
				std::string v = lua_tostring(s, -1);
				if (is_sound_role(v) && depth < 4) {
					lua_pop(s, 1);
					role(v, depth + 1);
					if (P->sounds.count(v))
						P->sounds[k] = P->sounds[v];
					return;
				}
				names.push_back(v);
			} else if (lua_istable(s, -1)) {
				for (lua_Integer i = 1; i <= 64; i++) {
					if (lua_geti(s, -1, i) != LUA_TSTRING) {
						lua_pop(s, 1);
						break;
					}
					names.push_back(lua_tostring(s, -1));
					lua_pop(s, 1);
				}
			}
			lua_pop(s, 1);
			SoundRole r;
			for (auto &n : names) {
				std::string path = in_dir(n, sdir);
				if (file_exists(path)) {
					std::string ext = lower(n.size() > 4 ? n.substr(n.size() - 4) : n);
					if (ext != ".wav")
						warn_("sound " + k + ": " + n + " won't play: OBSPETS plays only .wav files");
					r.files.push_back(path);
				} else {
					warn_("sound " + k + ": " + n + " isn't in " + sdir);
				}
			}
			if (!r.files.empty())
				P->sounds[k] = r;
			else if (!names.empty())
				use_default(k);   // (its files are missing: the warnings say so)
		};
		for (auto &k : VOICE)
			role(k, 0);
		for (auto &k : FOLEY)
			role(k, 0);
		lua_pushnil(s);
		while (lua_next(s, st)) {
			if (lua_type(s, -2) == LUA_TSTRING && !is_sound_role(lua_tostring(s, -2)))
				warn_(std::string("unknown sound role ") + lua_tostring(s, -2));
			lua_pop(s, 1);
		}
	}
	lua_pop(s, 1);
	P->nsounds = (int)P->sounds.size();
	return P;
}

std::string hex_of(const Rgb &c)
{
	auto b = [](float v) { return (unsigned)std::lround(std::clamp(v, 0.0f, 1.0f) * 255); };
	char buf[8];
	snprintf(buf, sizeof buf, "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
	return buf;
}

std::string user_packs_dir()
{
	return slash(config_path("packs"));
}

std::vector<PackEntry> list_packs()
{
	std::vector<PackEntry> out;
	std::set<std::string> seen;
	auto scan = [&](const std::string &dir_in, bool user) {
		std::string dir = slash(dir_in);
		os_dir_t *d = os_opendir(dir.c_str());
		if (!d)
			return;
		while (os_dirent *e = os_readdir(d)) {
			std::string n = e->d_name;
			if (!e->directory || n.empty() || n[0] == '.' || n[0] == '_' || seen.count(n))
				continue;
			if (file_exists(dir + n + "/pack.lua")) {
				seen.insert(n);
				out.push_back({n, dir + n + "/", user});
			}
		}
		os_closedir(d);
	};
	scan(user_packs_dir(), true);   // the user's own first, so theirs wins a name clash
	scan(module_data_path("packs"), false);
	std::sort(out.begin(), out.end(), [](const PackEntry &a, const PackEntry &b) { return a.id < b.id; });
	return out;
}

} // namespace obspet
