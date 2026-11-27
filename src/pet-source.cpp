// The OBS side: the OBSPETS source (settings, properties, the tick, the hub's events coming in,
// and the rules for which of its pets are on screen and who reacts), and each pet's settings.
#include "pet.hpp"
#include "chat-source.hpp"
#include "editor.hpp"
#include "settings.hpp"

#include <obs-frontend-api.h>
#include <graphics/vec2.h>
#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace obspet {

const char *PET_SOURCE_ID = "obspet";   // OBS saves sources by this id: changing it orphans existing Pets
static const double LABEL_LIFE = 4.5;
static const char *DEFAULT_PACKS[] = {"blob"};   // the first one found is the default character
// the characters pets 2, 3, ... start as (the default character if one isn't there)
static const char *EXTRA_PACKS[] = {"cat-ginger", "dog-retriever", "cat-black", "dog-husky", "cat-tabby"};

static const struct {
	const char *id;
	Hear h;
} HEAR[] = {{"both", Hear::Both}, {"stream", Hear::Stream}, {"me", Hear::Me}, {"off", Hear::Off}};

static Hear hear_of(const std::string &id)
{
	for (auto &h : HEAR)
		if (id == h.id)
			return h.h;
	return Hear::Both;
}

static std::mutex g_pets_mu;
static std::set<PetSource *> g_pets;

void with_pets(const std::function<void(PetSource &)> &f)
{
	std::lock_guard<std::mutex> g(g_pets_mu);   // (a source can't be destroyed while this is held)
	for (auto *p : g_pets)
		f(*p);
}

static std::string slot_key(const char *base, int slot)
{
	return slot ? base + std::to_string(slot + 1) : std::string(base);
}

// Every reaction a pet can be set not to do (each pet's "Reactions" list): the alert kinds,
// channel points ("pet") and the chat reactions (MOOD_NAMES). Pet N does it unless "do_<id>N" is off.
static const struct {
	const char *id, *label;
} REACTS[] = {
	{"follow", "Follows"},
	{"sub", "Subs"},
	{"gift", "Gifted subs"},
	{"bits", "Bits and Kicks"},
	{"raid", "Raids"},
	{"pet", "Channel points"},
	{"happy", "Chat: happy hop"},
	{"love", "Chat: hearts"},
	{"greet", "Chat: says hi"},
	{"curious", "Chat: curious"},
	{"show_off", "Chat: shows off"},
	{"play", "Chat: plays"},
	{"zoomies", "Chat: zoomies"},
	{"vibe", "Chat: bobs to music"},
	{"beg", "Chat: begs"},
	{"sleepy", "Chat: naps"},
	{"hop_on", "Chat: hops onto the chat"},
	{"hop_off", "Chat: hops off the chat"},
	{"swat_chat", "Chat: swats at the chat"},
	{"judge", "Chat: unimpressed"},
	{"sulk", "Chat: sulks"},
	{"startled", "Chat: startled"},
	{"scared", "Chat: scared"},
	{"angry", "Chat: angry"},
	{"yank", "Chat: rips a message out"},
};

// Who hears each reaction's sounds: "sfx_<id>" (both, stream, me or off) for every REACTS id and
// these two. Unset: the source's "hear".
static const char *SFX_MORE[] = {"automod", "idle"};   // Auto mod ripping a message out, and its own doings

static std::string do_key(const char *id, int slot)
{
	return slot_key((std::string("do_") + id).c_str(), slot);
}

std::string Pet::k(const char *base) const
{
	return slot_key(base, slot);
}

// ------------------------------------------------------------------ packs

static std::string default_pack()
{
	auto all = list_packs();
	for (const char *want : DEFAULT_PACKS)
		for (auto &p : all)
			if (p.id == want)
				return p.id;
	return all.empty() ? "" : all[0].id;
}

// (only the first pet can use a folder from anywhere)
static std::string pack_path(obs_data_t *s, int slot)
{
	std::string dir = slot ? "" : str(s, "pack_dir");
	if (!dir.empty())
		return dir;
	std::string want = str(s, slot_key("pack", slot).c_str());
	auto all = list_packs();
	for (auto &p : all)
		if (p.id == want)
			return p.path;
	std::string def = default_pack();
	for (auto &p : all)
		if (p.id == def)
			return p.path;
	return "";
}

// The sprite sheet as a texture, its colors changed by the pack's look (see Look in pack.hpp). The
// editor's preview (lookPixels in editor.js) does the same steps; this is done once, on the pixels.
static gs_texture_t *sheet_texture(const std::string &path, const Look &L)
{
	enum gs_color_format fmt = GS_UNKNOWN;
	uint32_t cx = 0, cy = 0;
	uint8_t *data = gs_create_texture_file_data(path.c_str(), &fmt, &cx, &cy);
	if (!data)
		return nullptr;
	bool rgba = fmt == GS_RGBA, bgra = fmt == GS_BGRA || fmt == GS_BGRX;
	if (L.any() && (rgba || bgra)) {
		double r = L.hue * 3.14159265358979 / 180, a = std::cos(r), b = std::sin(r);
		const double m[3][3] = {
			{0.213 + 0.787 * a - 0.213 * b, 0.715 - 0.715 * a - 0.715 * b, 0.072 - 0.072 * a + 0.928 * b},
			{0.213 - 0.213 * a + 0.143 * b, 0.715 + 0.285 * a + 0.140 * b, 0.072 - 0.072 * a - 0.283 * b},
			{0.213 - 0.213 * a - 0.787 * b, 0.715 - 0.715 * a + 0.715 * b, 0.072 + 0.928 * a + 0.072 * b}};
		const double tint[3] = {L.tint.r, L.tint.g, L.tint.b};
		int ri = rgba ? 0 : 2, bi = rgba ? 2 : 0;
		for (size_t i = 0, n = (size_t)cx * cy; i < n; i++) {
			uint8_t *px = data + i * 4;
			if (fmt != GS_BGRX && !px[3])
				continue;
			uint32_t key = ((uint32_t)px[ri] << 16) | ((uint32_t)px[1] << 8) | px[bi];
			if (!L.recolor.empty()) {
				auto it = L.recolor.find(key);
				if (it != L.recolor.end())
					key = it->second;
			}
			double c[3] = {(key >> 16) / 255.0, ((key >> 8) & 255) / 255.0, (key & 255) / 255.0};
			if (L.hue != 0) {
				double o[3];
				for (int k = 0; k < 3; k++)
					o[k] = m[k][0] * c[0] + m[k][1] * c[1] + m[k][2] * c[2];
				for (int k = 0; k < 3; k++)
					c[k] = o[k];
			}
			if (L.saturation != 1) {
				double y = 0.213 * c[0] + 0.715 * c[1] + 0.072 * c[2];
				for (double &v : c)
					v = y + (v - y) * L.saturation;
			}
			for (int k = 0; k < 3; k++) {
				double v = std::clamp(c[k], 0.0, 1.0);
				v = (v - 0.5) * L.contrast + 0.5;
				v = std::clamp(v, 0.0, 1.0);
				v = L.lightness >= 0 ? v + (1 - v) * L.lightness : v * (1 + L.lightness);
				v = v * (1 - L.tint_amount) + v * tint[k] * L.tint_amount;
				c[k] = v;
			}
			px[ri] = (uint8_t)std::clamp(std::round(c[0] * 255), 0.0, 255.0);
			px[1] = (uint8_t)std::clamp(std::round(c[1] * 255), 0.0, 255.0);
			px[bi] = (uint8_t)std::clamp(std::round(c[2] * 255), 0.0, 255.0);
		}
	}
	obs_enter_graphics();
	gs_texture_t *t = gs_texture_create(cx, cy, fmt, 1, (const uint8_t **)&data, 0);
	obs_leave_graphics();
	bfree(data);
	return t;
}

void Pet::use_pack(const std::string &dir)
{
	std::string err;
	if (dir.empty()) {
		err = "there are no characters. Install OBSPETS again, or put one in the folder that \"Open my characters folder\" opens.";
	} else {
		std::shared_ptr<Pack> P = load_pack(dir, err);
		if (P) {
			gs_texture_t *t = sheet_texture(P->sheet, P->look);
			uint32_t w = 0, h = 0;
			if (t) {
				obs_enter_graphics();
				w = gs_texture_get_width(t);
				h = gs_texture_get_height(t);
				obs_leave_graphics();
			}
			if (!t) {
				err = "the sprite sheet didn't load: " + P->sheet;
			} else {
				// frames past the edge of the sheet
				auto check = [&](const std::string &what, const Cell &c) {
					if ((c.col + 1) * P->cw > (int)w || (c.row + 1) * P->ch > (int)h)
						P->warnings.push_back(what + " uses the cell at column " + std::to_string(c.col) + ", row " +
								      std::to_string(c.row) + ", past the edge of the sheet");
				};
				std::set<const Anim *> seen;
				for (const char *k : ANIMS) {
					const Anim &a = P->anim(k);
					if (seen.insert(&a).second)
						for (auto &c : a.cells)
							check(std::string("animation ") + k, c);
				}
				for (const char *k : POSES)
					check(std::string("pose ") + k, P->poses[k]);
				for (auto &w8 : P->warnings)
					warn("%s: %s", P->name.c_str(), w8.c_str());
				obs_enter_graphics();
				if (tex)
					gs_texture_destroy(tex);
				obs_leave_graphics();
				tex = t;
				tw = w;
				th = h;
				pack = P;
				info("character %s from %s", P->name.c_str(), P->dir.c_str());
			}
		}
	}
	if (!err.empty())
		warn("%s", err.c_str());
	{
		std::lock_guard<std::mutex> g(mu_);
		pack_err = err;
		ui.pack = pack;
		ui.pack_err = err;
	}
	if (slot == 0) {
		std::lock_guard<std::mutex> g(owner->mu_);
		owner->ui.pack = pack;
		owner->ui.pack_err = err;
	}
}

std::string PetSource::pack_status()
{
	PackPtr P;
	std::string err;
	{
		std::lock_guard<std::mutex> g(mu_);
		P = ui.pack;
		err = ui.pack_err;
	}
	if (!P)
		return "No character loaded: " + (err.empty() ? std::string("pick one above.") : err);
	int own = 0;
	for (auto &kv : P->sounds)
		own += kv.second.fallback ? 0 : 1;
	std::string s = P->name + (!P->nsounds ? " is loaded. It has no sounds."
				   : own == P->nsounds ? " is loaded, with its own sounds."
				   : own ? " is loaded, with its own sounds and OBSPETS's " + P->sound_set + " sounds for the rest."
					 : " is loaded, with OBSPETS's " + P->sound_set + " sounds.");
	if (!P->borrowed.empty())
		s += "\nStand-ins for missing animations: " + P->borrowed + ".";
	std::string off;
	for (auto &r : REACTS) {
		std::string need = P->lacks(r.id);
		if (!need.empty())
			off += (off.empty() ? "" : "; ") + std::string(r.label) + " (needs " + need + ")";
	}
	if (!off.empty())
		s += "\nOff until it has the animation: " + off + ".";
	if (!err.empty())
		s += "\nThe last reload failed, so this is still the old one: " + err;
	for (size_t i = 0; i < P->warnings.size(); i++) {
		if (i >= 6) {
			s += "\n(and " + std::to_string(P->warnings.size() - 6) + " more in the log)";
			break;
		}
		s += "\n\xE2\x80\xA2 " + P->warnings[i];
	}
	return s;
}

void PetSource::reload_pack(const std::string &id)
{
	post([id](PetSource &o) {
		Data s(obs_source_get_settings(o.source));
		for (auto &p : o.pets) {
			bool uses = id.empty() || (p->pack && p->pack->id == id) ||
				    (str(s.get(), p->k("pack").c_str()) == id && (p->slot || str(s.get(), "pack_dir").empty()));
			if (!uses)
				continue;
			p->pack_key.clear();
			p->update(s.get());
		}
		o.profile_changed();
		o.status_dirty_ = true;
	});
}

std::vector<PackPtr> PetSource::packs_shown()
{
	std::vector<PackPtr> out;
	std::lock_guard<std::mutex> g(pets_mu_);
	for (auto &p : pets) {
		std::lock_guard<std::mutex> g2(p->mu_);
		if (p->ui.pack)
			out.push_back(p->ui.pack);
	}
	return out;
}

// ------------------------------------------------------------------ who hears it

static obs_monitoring_type monitoring_of(Hear h)
{
	return h == Hear::Stream ? OBS_MONITORING_TYPE_NONE
	       : h == Hear::Me   ? OBS_MONITORING_TYPE_MONITOR_ONLY
				 : OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT;
}

// OBS 33 has monitoring on or off only (monitored sources always reach the stream too), and its
// monitoring "type" calls are deprecated (each logs a warning). We build against older headers,
// so the new calls are looked up at run time.
using SetMonitoring = void (*)(obs_source_t *, bool);
using GetMonitoring = bool (*)(const obs_source_t *);
static FARPROC obs_proc(const char *name)
{
	HMODULE m = GetModuleHandleW(L"obs.dll");
	return m ? GetProcAddress(m, name) : nullptr;
}
static SetMonitoring set_monitoring_enabled()
{
	static auto f = reinterpret_cast<SetMonitoring>(obs_proc("obs_source_set_monitoring_enabled"));
	return f;
}
static GetMonitoring get_monitoring_enabled()
{
	static auto f = reinterpret_cast<GetMonitoring>(obs_proc("obs_source_get_monitoring_enabled"));
	return f;
}

static void apply_monitoring(obs_source_t *s, Hear h)
{
	if (auto f = set_monitoring_enabled())
		f(s, h != Hear::Stream);
	else
		obs_source_set_monitoring_type(s, monitoring_of(h));
}

static const char *read_monitoring(obs_source_t *s)
{
	if (auto f = get_monitoring_enabled())
		return f(s) ? "both" : "stream";
	obs_monitoring_type m = obs_source_get_monitoring_type(s);
	return m == OBS_MONITORING_TYPE_NONE ? "stream" : m == OBS_MONITORING_TYPE_MONITOR_ONLY ? "me" : "both";
}

// OBS 32 and newer stop the echo themselves: when a Desktop Audio (Audio Output Capture) source
// records the monitoring device, OBS leaves monitored sources out of the tracks it shares, so the
// stream gets the pet once, through the capture. Older OBS sends it twice.
bool obs_prevents_monitoring_echo()
{
	return obs_get_version() >= MAKE_SEMANTIC_VERSION(32, 0, 0);
}

// The source itself is the stream's side, so it's never monitored; its ears are the streamer's,
// always monitored (audio.hpp). On the UI thread, which owns monitoring.
static void set_sides(obs_source_t *src, obs_source_t *ears, bool all_tracks)
{
	struct A {
		obs_weak_source_t *w, *e;
		bool all;
	};
	obs_queue_task(
		OBS_TASK_UI,
		[](void *p) {
			auto *a = static_cast<A *>(p);
			if (obs_source_t *s = obs_weak_source_get_source(a->w)) {
				if (a->all)
					obs_source_set_audio_mixers(s, 0x3F);
				if (strcmp(read_monitoring(s), "stream") != 0) {
					info("%s: monitoring off (who hears each sound is set in OBSPETS Settings, Sound)",
					     obs_source_get_name(s));
					apply_monitoring(s, Hear::Stream);
				}
				obs_source_release(s);
			}
			if (obs_source_t *e = a->e ? obs_weak_source_get_source(a->e) : nullptr) {
				if (strcmp(read_monitoring(e), "stream") == 0)
					apply_monitoring(e, Hear::Me);
				obs_source_release(e);
			}
			obs_weak_source_release(a->w);
			obs_weak_source_release(a->e);
			delete a;
		},
		new A{obs_source_get_weak_source(src), ears ? obs_source_get_weak_source(ears) : nullptr, all_tracks}, false);
}

// On the first frame. A Pet saved by 0.1 (which had no sound) comes back with no audio tracks at
// all, and loading a scene sets the tracks and monitoring from the save after the source is made.
// So this waits for the first frame (this thread), and hands it to the UI thread, where it runs
// after the loading. The first time: every audio track. Every time: the two sides (set_sides).
static void first_audio(obs_source_t *src, obs_source_t *ears, bool first)
{
	set_sides(src, ears, first);
}

// ------------------------------------------------------------------ settings

// The size that suits the canvas: 5 on 1080p, more on bigger canvases, less on smaller ones.
static int default_scale()
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || !ovi.base_height)
		return 5;
	return std::clamp((int)std::lround(5.0 * ovi.base_height / 1080.0), 2, 12);
}

static std::string extra_pack(int slot)
{
	std::string want = EXTRA_PACKS[(slot - 1) % (int)(sizeof EXTRA_PACKS / sizeof *EXTRA_PACKS)];
	for (auto &p : list_packs())
		if (p.id == want)
			return want;
	return default_pack();
}

void PetSource::defaults(obs_data_t *s)
{
	// more pets (pet 1 is "pack", "name", "scale", "weight"; pet 2 is "pack2", ...)
	obs_data_set_default_int(s, "pets", 1);
	for (int i = 0; i < MAX_PETS; i++) {
		if (i) {
			obs_data_set_default_string(s, slot_key("pack", i).c_str(), extra_pack(i).c_str());
			obs_data_set_default_string(s, slot_key("name", i).c_str(), "");   // (empty: its character's name)
			obs_data_set_default_int(s, slot_key("scale", i).c_str(), default_scale());
		}
		obs_data_set_default_int(s, slot_key("weight", i).c_str(), 5);
		for (auto &r : REACTS)
			obs_data_set_default_bool(s, do_key(r.id, i).c_str(), true);
	}
	obs_data_set_default_string(s, "who", "turns");      // turns, random, all
	obs_data_set_default_int(s, "at_once", MAX_PETS);    // (more than there are: all of them)
	obs_data_set_default_int(s, "swap_min", 10);         // (0: never)
	obs_data_set_default_bool(s, "swap_alert", false);
	obs_data_set_default_string(s, "next_in", "turns");  // turns, random

	obs_data_set_default_string(s, "pack", default_pack().c_str());
	obs_data_set_default_string(s, "pack_dir", "");
	obs_data_set_default_string(s, "name", "");
	obs_data_set_default_int(s, "scale", default_scale());
	obs_data_set_default_int(s, "floor", 24);
	obs_data_set_default_string(s, "perch", "");   // empty: found by name ("chat"); NO_SOURCE: none
	obs_data_set_default_string(s, "avoid", "");   // (before 0.2: names, comma separated; now avoid_list)
	obs_data_set_default_string(s, "avoid_add", "");
	obs_data_set_default_double(s, "visit", 0.5);
	obs_data_set_default_bool(s, "perch_box", false);    // another chat (not OBSPETS Chat): sit on it
	obs_data_set_default_bool(s, "other_yank", false);   // another chat: yank messages out of it
	obs_data_set_default_double(s, "naps", 1);
	obs_data_set_default_double(s, "energy", 1);
	obs_data_set_default_string(s, "alert", "");   // empty: found by name ("alert"); NO_SOURCE: none
	obs_data_set_default_bool(s, "reactions", true);
	obs_data_set_default_bool(s, "twitch_alerts", true);
	obs_data_set_default_string(s, "reward", "Pet {name}");
	obs_data_set_default_bool(s, "moods", true);
	obs_data_set_default_bool(s, "allow_delete", true);
	obs_data_set_default_bool(s, "automod", true);
	obs_data_set_default_bool(s, "automod_show", true);
	obs_data_set_default_string(s, "personality", "");
	obs_data_set_default_string(s, "ignore", DEFAULT_BOTS);
	obs_data_set_default_string(s, "label_after", " pet {name}");
	obs_data_set_default_int(s, "label_color", 0xFFDEE4EC);
	Data f = data_new();
	obs_data_set_string(f.get(), "face", strcmp(text_id(), "text_gdiplus") == 0 ? "Segoe UI Semibold" : "Sans Serif");
	obs_data_set_int(f.get(), "size", 26);
	obs_data_set_default_obj(s, "label_font", f.get());
	// (alert overlays read these: "me" and "off" mean the stream doesn't hear the pet)
	obs_data_set_default_string(s, "hear", "both");
	obs_data_set_default_double(s, "ear_vol", 1.0);
	obs_data_set_default_string(s, "test_pick", "follow");
	obs_data_set_default_string(s, "test_more", "mood:happy");
	obs_data_set_default_bool(s, "advanced", false);
}

// The sources to keep off: the list in the properties, plus the old comma-separated text.
static std::vector<std::string> avoid_list(obs_data_t *s)
{
	std::vector<std::string> out;
	auto add = [&](const std::string &n0) {
		std::string n = trim(n0);
		if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end())
			out.push_back(n);
	};
	obs_data_array_t *arr = obs_data_get_array(s, "avoid_list");
	for (size_t i = 0; arr && i < obs_data_array_count(arr); i++) {
		obs_data_t *it = obs_data_array_item(arr, i);
		add(str(it, "value"));
		obs_data_release(it);
	}
	obs_data_array_release(arr);
	for (auto &n : split(str(s, "avoid"), ','))
		add(n);
	return out;
}

static std::string fill_name(std::string s, const std::string &name)
{
	for (size_t at; (at = s.find("{name}")) != std::string::npos;)
		s.replace(at, 6, name);
	return s;
}

void Pet::update(obs_data_t *s)
{
	// the pack first: everything below is measured in it
	std::string key = str(s, k("pack").c_str()) + "|" + (slot ? "" : str(s, "pack_dir"));
	if (key != pack_key) {
		pack_key = key;
		clips.clear();
		PackPtr had = pack;
		use_pack(pack_path(s, slot));
		// a character swapped in while it runs starts afresh (the old one's timers are in its units)
		if (had && pack != had && started) {
			mode = Mode::Ground;
			fixed.clear();
			on_frame.clear();
			wiggling = busy = false;
			anim = "idle";
			at = 0;
			has_timer = true;
			timer = 2;
			cb = decide;
			done = nullptr;
		}
	}
	double unit = pack ? pack->unit : 1;
	px = (double)std::clamp<long long>(obs_data_get_int(s, k("scale").c_str()), 1, 12);
	weight = (double)std::clamp<long long>(obs_data_get_int(s, k("weight").c_str()), 0, 10);
	S = px * unit;   // motion unit: one pixel of a 32 px tall pet, on screen
	floor_px = (double)obs_data_get_int(s, "floor");
	perch_name = trim(str(s, "perch"));
	avoid_names = avoid_list(s);
	sit_names.clear();
	if (obs_data_array_t *arr = obs_data_get_array(s, "sit_on")) {
		for (size_t i = 0; i < obs_data_array_count(arr); i++) {
			obs_data_t *it = obs_data_array_item(arr, i);
			std::string n = trim(str(it, "value"));
			if (!n.empty())
				sit_names.insert(n);
			obs_data_release(it);
		}
		obs_data_array_release(arr);
	}
	visit = obs_data_get_double(s, "visit");
	perch_box = obs_data_get_bool(s, "perch_box");
	other_yank = obs_data_get_bool(s, "other_yank");
	naps = obs_data_get_double(s, "naps");
	energy = obs_data_get_double(s, "energy");
	alert_name = trim(str(s, "alert"));
	reactions = obs_data_get_bool(s, "reactions");
	twitch_alerts = obs_data_get_bool(s, "twitch_alerts");
	automod = obs_data_get_bool(s, "automod");
	automod_show = obs_data_get_bool(s, "automod_show");
	name = trim(str(s, k("name").c_str()));
	if (name.empty())
		name = pack ? pack->name : "Pet";
	reward = lower(trim(fill_name(str(s, "reward"), name)));
	label_after = fill_name(str(s, "label_after"), name);
	label_color = (uint32_t)obs_data_get_int(s, "label_color");
	{
		Data font(obs_data_get_obj(s, "label_font"));
		std::string fkey = font ? obs_data_get_json(font.get()) : "";
		if (fkey != font_key) {
			if (!font_key.empty())
				for (obs_source_t *t : {lname, lrest, yname, ytext})
					if (t) {
						Data u = data_new();
						obs_data_set_obj(u.get(), "font", font.get());
						obs_source_update(t, u.get());
					}
			font_key = fkey;
		}
	}
	hear = hear_of(str(s, "hear"));
	sfx.clear();
	auto route = [&](const char *id) {
		std::string key = std::string("sfx_") + id;
		if (obs_data_has_user_value(s, key.c_str()))
			sfx[id] = hear_of(str(s, key.c_str()));
	};
	for (auto &r : REACTS)
		route(r.id);
	for (const char *id : SFX_MORE)
		route(id);
	owner->scene_t = 0;   // (look at the scene again: the chat, alert or kept-off sources may have changed)

	// what the chat-mood engine needs from this Pet
	// Auto mod runs whether or not the pet reacts to chat; with no chat reactions, it's all the AI
	// is asked about
	bool react_chat = reactions && obs_data_get_bool(s, "moods");
	mood.on = react_chat || automod;
	mood.automod_only = !react_chat;
	mood.allow_delete = automod;
	mood.name = name;
	mood.describe = pack ? pack->describe : "";
	mood.pronouns = pack && !pack->pronouns.empty() ? pack->pronouns : "they";   // the editor saves "they" as empty
	std::string pers = slot ? "" : trim(str(s, "personality"));
	mood.personality = pers.empty() && pack ? pack->personality : pers;
	ignore_own.clear();
	for (auto &n : split(str(s, "ignore"), ',')) {
		std::string u = lower(trim(n));
		if (!u.empty() && u[0] == '@')
			u = u.substr(1);
		if (!u.empty())
			ignore_own.push_back(u);
	}
	merge_ignore(*this);
	wont.clear();
	mood.skip.clear();
	for (auto &r : REACTS)
		// off by choice, or the character has no animation it needs (REACT_NEEDS)
		if (!obs_data_get_bool(s, do_key(r.id, slot).c_str()) || (pack && !pack->lacks(r.id).empty())) {
			wont.insert(r.id);
			// (a rip is also how Auto mod deletes: the model may still pick it; this pet just won't act it out)
			if (strncmp(r.label, "Chat:", 5) == 0 && strcmp(r.id, "yank") != 0)
				mood.skip.push_back(r.id);
		}
	// ripping a deleted message out on stream takes a pet that reacts, and does "yank"
	mood.show_deletes = automod_show && reactions && !wont.count("yank");
	if (mood.automod_only) {   // the model is only offered "none" and "yank" (how Auto mod deletes)
		mood.skip.clear();
		for (int i = 0; i < MOOD_COUNT; i++)
			if (strcmp(MOOD_NAMES[i], "yank") != 0)
				mood.skip.push_back(MOOD_NAMES[i]);
	}
	if (slot == 0) {
		std::lock_guard<std::mutex> g(owner->mu_);
		owner->ui.name = name;
	}
}

// ------------------------------------------------------------------ lifetime

Pet::Pet(PetSource *o, int sl, obs_data_t *settings) : owner(o), slot(sl), source(o->source)
{
	W = o->W;
	H = o->H;
	obs_enter_graphics();
	char *err = nullptr;
	effect = gs_effect_create(EFFECT, "obspet.effect", &err);
	obs_leave_graphics();
	if (!effect)
		warn("the effect didn't compile: %s", err ? err : "?");
	bfree(err);
	if (effect) {
		p_image = gs_effect_get_param_by_name(effect, "image");
		p_frame = gs_effect_get_param_by_name(effect, "frame");
		p_texsize = gs_effect_get_param_by_name(effect, "texsize");
		p_flip = gs_effect_get_param_by_name(effect, "flip");
		p_color = gs_effect_get_param_by_name(effect, "color");
		p_alpha = gs_effect_get_param_by_name(effect, "alpha");
	}
	mixer = o->mixer.get();
	ears = o->ear_mixer.get();
	update(settings);
	Data font(obs_data_get_obj(settings, "label_font"));
	lname = text_source("obspet label name", font.get());
	lrest = text_source("obspet label rest", font.get());
	yname = text_source("obspet yank name", font.get());
	ytext = text_source("obspet yank text", font.get());
}

Pet::~Pet()
{
	free_gfx(*this);
}

PetSource::PetSource(obs_data_t *settings, obs_source_t *src) : source(src)
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		W = ovi.base_width;
		H = ovi.base_height;
	}
	mixer = std::make_unique<Mixer>(src);
	ears = create_ears(obs_source_get_name(src));
	if (ears)
		ear_mixer = std::make_unique<Mixer>(ears);
	else
		warn("couldn't make the source for what you hear: sounds set to you only won't play");
	// Once, the first time this version runs it: every audio track (see tick: setup_audio).
	if (!obs_data_get_bool(settings, "audio_set")) {
		obs_data_set_bool(settings, "audio_set", true);
		setup_audio = true;
	}
	// before 0.5: a Sounds tick box, and two sliders for how often it reacts to chat
	if (obs_data_has_user_value(settings, "sounds")) {
		if (!obs_data_get_bool(settings, "sounds"))
			obs_data_set_string(settings, "hear", "off");
		obs_data_erase(settings, "sounds");
	}
	// a pet's own "how chatty" (before 0.5 two sliders, then a choice, then seconds in 0.6) is gone:
	// the account's "Time between reactions" is the only one
	for (const char *k : {"mood_every", "mood_gap", "chatty", "chatty_s"})
		obs_data_erase(settings, k);
	// 0.6: Auto mod has its own switches (it ran when chat reactions did,
	// and "Auto mod only" hid the pet and its reactions: now that's chat reactions off)
	if (!obs_data_has_user_value(settings, "automod")) {
		auto on = [&](const char *k) { return !obs_data_has_user_value(settings, k) || obs_data_get_bool(settings, k); };
		bool only = obs_data_get_bool(settings, "automod_only");
		obs_data_set_bool(settings, "automod", on("allow_delete") && ((on("reactions") && on("moods")) || only));
		if (only) {
			obs_data_set_bool(settings, "moods", false);
			obs_data_set_bool(settings, "automod_show", false);
		}
	}
	obs_data_erase(settings, "automod_only");
	// written by the chat and alert pages that talked to it over obs-websocket (before 0.5)
	for (const char *k : {"chat_top", "chat_h", "chat_seq", "pet_seq", "pet_user", "pet_color", "alert_kind", "alert_n",
			      "alert_seq", "mood_kind", "mood_seq", "mood_user", "mood_color", "mood_text", "mood_fx", "mood_fy",
			      "mood_img", "yank_ack"})
		obs_data_erase(settings, k);
	// the old comma-separated "keep off" text becomes the list
	if (!str(settings, "avoid").empty()) {
		obs_data_array_t *arr = obs_data_array_create();
		for (auto &n : avoid_list(settings)) {
			Data it = data_new();
			obs_data_set_string(it.get(), "value", n.c_str());
			obs_data_array_push_back(arr, it.get());
		}
		obs_data_set_array(settings, "avoid_list", arr);
		obs_data_array_release(arr);
		obs_data_set_string(settings, "avoid", "");
	}
	pets.push_back(std::make_unique<Pet>(this, 0, settings));
	lead_ = pets[0].get();
	// a new pet is called what its character is called (the name box is never blank)
	if (trim(str(settings, "name")).empty() && lead_->pack)
		obs_data_set_string(settings, "name", lead_->pack->name.c_str());
	update(settings);
	{
		std::lock_guard<std::mutex> g(g_pets_mu);
		g_pets.insert(this);
	}
	Hub::get().add(this);
	profile_changed();
}

PetSource::~PetSource()
{
	mixer.reset();             // (its thread stops before the pets go)
	ear_mixer.reset();
	destroy_ears(ears);
	Hub::get().remove(this);   // (after this, no hub thread calls into it)
	{
		std::lock_guard<std::mutex> g(g_pets_mu);
		g_pets.erase(this);
	}
	std::lock_guard<std::mutex> g(pets_mu_);
	pets.clear();
}

void PetSource::update(obs_data_t *s)
{
	hear = hear_of(str(s, "hear"));
	ear_trim = std::clamp(obs_data_get_double(s, "ear_vol"), 0.0, 2.0);

	std::string w = str(s, "who");
	who = w == "random" ? Who::Random : w == "all" ? Who::All : Who::Turns;
	at_once = (int)std::clamp<long long>(obs_data_get_int(s, "at_once"), 1, MAX_PETS);
	double every = 60.0 * (double)std::clamp<long long>(obs_data_get_int(s, "swap_min"), 0, 600);
	if (every != swap_every)
		swap_t = every;
	swap_every = every;
	swap_alert = obs_data_get_bool(s, "swap_alert");
	next_random = str(s, "next_in") == "random";

	// the pets: as many as asked for (a new one starts off screen; balance() brings it on)
	int n = (int)std::clamp<long long>(obs_data_get_int(s, "pets"), 1, MAX_PETS);
	{
		std::lock_guard<std::mutex> g(pets_mu_);
		while ((int)pets.size() > n)
			pets.pop_back();
		while ((int)pets.size() < n)
			pets.push_back(std::make_unique<Pet>(this, (int)pets.size(), s));
	}
	for (auto &p : pets)
		p->update(s);
	profile_changed();
}

void PetSource::post(std::function<void(PetSource &)> f)
{
	std::lock_guard<std::mutex> g(mu_);
	inbox_.push_back(std::move(f));
}

Pet *PetSource::at_slot(int slot)
{
	return slot >= 0 && slot < (int)pets.size() ? pets[slot].get() : nullptr;
}

// ------------------------------------------------------------------ who reacts

std::vector<Pet *> PetSource::stage()
{
	std::vector<Pet *> out;
	for (auto &p : pets)
		if (p->on_stage && p->started && !p->leaving && p->pack)
			out.push_back(p.get());
	return out;
}

// weighted by each one's chance (all 0: evenly)
static Pet *by_chance(const std::vector<Pet *> &from)
{
	if (from.empty())
		return nullptr;
	double total = 0;
	for (auto *p : from)
		total += p->weight;
	if (total <= 0)
		return from[(size_t)rnd(0, (double)from.size() - 0.001)];
	double r = rnd(0, total);
	for (auto *p : from) {
		r -= p->weight;
		if (r <= 0 && p->weight > 0)
			return p;
	}
	return from.back();
}

// on screen, and does `kind` ("": anyone)
static std::vector<Pet *> able(std::vector<Pet *> st, const std::string &kind)
{
	if (!kind.empty())
		st.erase(std::remove_if(st.begin(), st.end(), [&](Pet *p) { return !p->can(kind); }), st.end());
	return st;
}

Pet *PetSource::pick(const std::string &kind)
{
	auto st = able(stage(), kind);
	if (st.empty())
		return nullptr;
	if (who != Who::Turns)
		return by_chance(st);
	Pet *next = st[0];   // the next slot after the one that went last
	for (auto *p : st)
		if (p->slot > turn) {
			next = p;
			break;
		}
	turn = next->slot;
	return next;
}

std::vector<Pet *> PetSource::reactors(const std::string &kind)
{
	if (who == Who::All)
		return able(stage(), kind);
	Pet *p = pick(kind);
	return p ? std::vector<Pet *>{p} : std::vector<Pet *>{};
}

// The hub asks the chat model on behalf of one pet (its name, character and what it's doing): the
// one that acts the answer out. Picked when the previous answer is handed out.
void PetSource::profile_changed()
{
	Pet *p = at_slot(next_chat);
	if (!p || !p->on_stage || p->leaving) {
		auto st = stage();
		p = st.empty() ? lead_ : st[0];
	}
	if (p)
		Hub::get().set_profile(this, p->mood);
}

// ------------------------------------------------------------------ the hub (any thread)

void PetSource::hub_event(const HubEvent &e)
{
	switch (e.t) {
	case HubEvent::T::Alert:
		post([e](PetSource &o) {
			if (!o.lead_->reactions || !o.lead_->twitch_alerts)
				return;
			for (Pet *p : o.reactors(e.kind)) {
				p->has_alert = true;
				p->next_alert = {e.kind, e.n};
				p->swap_after = o.swap_alert;
				p->react_at = o.now;
			}
		});
		break;
	case HubEvent::T::Redeem:
		post([e](PetSource &o) {
			if (!o.lead_->reactions || o.lead_->reward.empty() || lower(trim(e.title)) != o.lead_->reward)
				return;
			for (Pet *p : o.reactors("pet")) {
				p->has_pet = true;
				p->next_pet_user = e.user;
				p->next_pet_color = soften(e.color);
			}
		});
		break;
	case HubEvent::T::Chat:
		post([](PetSource &o) {
			for (Pet *p : o.stage()) {
				// (an OBSPETS Chat counts the messages already)
				if (p->chat_kind == ChatKind::Ours)
					continue;
				p->heat += 1;
				p->quiet = 0;
				p->burst_n += 1;
			}
		});
		break;
	case HubEvent::T::Mood:
		post([e](PetSource &o) {
			if (!o.lead_->reactions)
				return;   // (Auto mod deleted it already, whether the pet acts it out or not)
			// the pet it was asked for, if it's still on screen and does this; else one that does; then
			// the next one's turn
			Pet *p = o.at_slot(o.next_chat);
			auto st = able(o.stage(), e.kind);
			if (!p || std::find(st.begin(), st.end(), p) == st.end())
				p = o.who == PetSource::Who::Turns ? o.pick(e.kind) : by_chance(st);
			if (p) {
				p->has_mood = true;
				p->next_mood = MoodReq{e.kind, p->now, e.user, soften(e.color), e.text, e.msg_id, 0.5, 0.9, e.remove};
			}
			Pet *n = o.who == PetSource::Who::Turns ? o.pick() : by_chance(o.stage());
			o.next_chat = n ? n->slot : -1;
			o.profile_changed();
		});
		break;
	}
}

void PetSource::hub_status_changed()
{
	status_dirty_ = true;
}

// Shown in the program (also inside a nested scene): the hub sends chat moods to such a source first.
bool PetSource::on_screen() const
{
	return obs_source_active(source);
}

// A refresh someone asked for (status_dirty_), once nothing was edited for 3 s: OBS sends an edit
// 0.5 s after the last key, so by then the typing has stopped. Until then it stays due.
bool PetSource::refresh_due()
{
	if (!status_dirty_ || os_gettime_ns() - last_edit_ns_ < 3000000000ULL)
		return false;
	return status_dirty_.exchange(false);
}

void PetSource::refresh_properties()
{
	obs_weak_source_t *w = obs_source_get_weak_source(source);
	obs_queue_task(
		OBS_TASK_UI,
		[](void *p) {
			auto *ws = static_cast<obs_weak_source_t *>(p);
			obs_source_t *s = obs_weak_source_get_source(ws);
			if (s) {
				obs_source_update_properties(s);
				obs_source_release(s);
			}
			obs_weak_source_release(ws);
		},
		w, false);
}

// ------------------------------------------------------------------ the tick

void Pet::tick(double dt)
{
	if (!on_stage || !pack)
		return;
	if (!started) {
		started = true;
		scan(*this);
		double sx = W * start_frac;
		double best = sx, bd = 1e18;
		for (auto &sg : segs) {
			double cx = clampd(sx, sg.a, sg.b);
			if (std::abs(cx - sx) < bd)
				best = cx, bd = std::abs(cx - sx);
		}
		x = std::floor(best);
		y = floorY;
		play(*this, "idle");
		after(*this, 2, decide);
	}
	now += dt;
	at += dt;
	if (scene_seen != owner->scene_n) {   // (the source looked at the scene again)
		scan(*this);
		if (on == On::Floor && mode == Mode::Ground)
			y = floorY;
	}
	// on the chat, follow its top smoothly (the page slides its lines in over about 0.26 s)
	if (on == On::Perch && has_perch && mode != Mode::Air)
		y += (perch_y - y) * (1 - std::exp(-dt * 14));
	if (on == On::Ledge && has_ledge && mode != Mode::Air)
		y += (on_ledge.y - y) * (1 - std::exp(-dt * 14));
	if (bobbing) {
		bob += dt;
		if (bob > 0.24)
			bobbing = false;
	}
	heat *= std::exp(-dt / 120);
	burst_n *= std::exp(-dt / 12);   // messages in the last few seconds
	quiet += dt;
	if (label_on) {
		label_t += dt;
		if (label_t > LABEL_LIFE)
			label_on = false;
	}
	react(*this);
	tick_motion(*this, dt);
}

// Sends the pet that's been on screen longest off (`only`: that one), if it isn't in the middle of
// something. False: nobody could go yet.
bool PetSource::send_out(Pet *only)
{
	Pet *out = nullptr;
	for (Pet *p : stage())
		if ((!only || p == only) && !p->busy && !p->has_yank && !p->has_alert && !p->has_mood && !p->has_pet &&
		    p->mode != Mode::Air && (!out || p->stage_at < out->stage_at))
			out = p;
	if (!out)
		return false;
	exit_dir = out->x < W / 2 ? -1 : 1;
	last_out = out->slot;
	out->swap_after = false;
	leave_stage(*out, exit_dir);
	return true;
}

// Keeps `at_once` pets on screen, swapping by the rules. A pet comes on once the one before it has
// gone, from the side that one left by.
void PetSource::balance(double dt)
{
	std::vector<Pet *> ready;   // pets with a character
	for (auto &p : pets)
		if (p->pack)
			ready.push_back(p.get());
	int want = std::min(at_once, (int)ready.size());
	if (!staged) {   // the start: the first ones, spread out
		staged = true;
		int i = 0;
		for (Pet *p : ready) {
			if (i >= want)
				break;
			p->on_stage = true;
			p->stage_at = now;
			p->start_frac = std::clamp(0.72 - 0.16 * i, 0.08, 0.92);
			i++;
		}
		swap_t = swap_every;
		return;
	}
	int on = 0;
	bool going = false;
	for (Pet *p : ready) {
		if (p->on_stage && !p->leaving)
			on++;
		going = going || p->leaving;
	}
	if (on > want) {
		send_out();
		return;
	}
	if (on < want) {
		if (going || now - entered_at < 1.5)
			return;   // (one at a time)
		std::vector<Pet *> off;
		for (Pet *p : ready)
			if (!p->on_stage)
				off.push_back(p);
		if (off.size() > 1)   // (not straight back on, if there's anyone else)
			off.erase(std::remove_if(off.begin(), off.end(), [&](Pet *p) { return p->slot == last_out; }), off.end());
		Pet *in = nullptr;
		if (next_random) {
			std::vector<Pet *> able;
			for (Pet *p : off)
				if (p->weight > 0)
					able.push_back(p);
			in = by_chance(able.empty() ? off : able);
		} else {
			for (Pet *p : off)   // the next in line after the last one that came on
				if (p->slot > last_in) {
					in = p;
					break;
				}
			if (!in && !off.empty())
				in = off[0];
		}
		if (in) {
			in->on_stage = true;
			in->stage_at = now;
			last_in = in->slot;
			entered_at = now;
			enter_stage(*in, exit_dir);
		}
		return;
	}
	// swapping, when there are more pets than places
	if ((int)ready.size() <= want) {
		swap_due = false;
		for (Pet *p : ready)
			p->swap_after = false;
		return;
	}
	if (swap_every > 0) {
		swap_t -= dt;
		if (swap_t <= 0) {
			swap_t = swap_every;
			swap_due = true;
		}
	}
	if (swap_due && !going && send_out())
		swap_due = false;
	// "Swap after an alert": once it's done reacting (or after 30 s, whatever it's doing)
	for (Pet *p : stage())
		if (p->swap_after && !going && now - p->react_at > 2 &&
		    ((!p->busy && !p->has_alert && p->mode == Mode::Ground) || now - p->react_at > 30)) {
			p->busy = false;
			if (send_out(p))
				break;
		}
}

void PetSource::tick(float seconds)
{
	if (!hear_set) {
		hear_set = true;
		first_audio(source, ears, setup_audio);
		setup_audio = false;
	}
	if (ears) {
		// what the streamer hears follows the Pet's fader and mute, times its own volume
		float v = (float)(obs_source_get_volume(source) * ear_trim);
		if (v != ear_vol_set)
			obs_source_set_volume(ears, ear_vol_set = v);
		int m = obs_source_muted(source) ? 1 : 0;
		if (m != ear_mute_set)
			obs_source_set_muted(ears, (ear_mute_set = m) != 0);
	}
	// monitoring turned on for the Pet itself (Advanced Audio Properties) would play the stream's
	// side to the streamer too: back off
	mon_t -= seconds;
	if (mon_t <= 0) {
		mon_t = 2;
		if (strcmp(read_monitoring(source), "stream") != 0)
			set_sides(source, ears, false);
	}
	std::vector<std::function<void(PetSource &)>> in;
	{
		std::lock_guard<std::mutex> g(mu_);
		in.swap(inbox_);
	}
	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		W = ovi.base_width;
		H = ovi.base_height;
	}
	for (auto &f : in)
		f(*this);
	if (refresh_due())
		refresh_properties();
	double dt = std::min(0.1, (double)seconds);
	now += dt;
	// the scene around the pets, for all of them at once (before they move: the first one also
	// decides where they start)
	scene_t -= dt;
	if (scene_t <= 0 && lead_ && std::any_of(pets.begin(), pets.end(), [](auto &p) { return p->pack != nullptr; })) {
		scene_t = 0.25;
		scan_scene(*this);
	}
	balance(dt);
	for (auto &p : pets)
		p->tick(dt);
}

void PetSource::render()
{
	for (auto &p : pets)
		if (p->on_stage)
			p->render();
}

// ------------------------------------------------------------------ tests

void PetSource::test(const std::string &what, const std::string &pack_id)
{
	post([what, pack_id](PetSource &o) {
		std::vector<Pet *> to;
		if (!pack_id.empty()) {   // (the editor's "Try it": a pet showing that character)
			for (Pet *p : o.stage())
				if (p->pack && p->pack->id == pack_id) {
					to.push_back(p);
					break;
				}
		} else if (what.rfind("mood:", 0) == 0) {
			if (Pet *p = o.pick(what == "mood:automod" ? "yank" : what.substr(5)))
				to.push_back(p);
		} else {
			to = o.reactors(what.substr(0, what.find(':')));
		}
		for (Pet *pp : to) {
			Pet &p = *pp;
			if (what.rfind("mood:", 0) == 0) {
				bool automod = what == "mood:automod";
				o.act_out(p, automod ? "yank" : what.substr(5), automod ? "v1ewb0t_4u" : "inkwell",
					  automod ? "best viewers and followers on streamboost dot shop" : "this pet is overrated", automod);
			} else if (what == "pet") {
				p.has_pet = true;
				p.next_pet_user = "inkwell";
				p.next_pet_color = "#a6c1db";
			} else {
				auto c = what.find(':');
				p.has_alert = true;
				p.next_alert = {what.substr(0, c), c == std::string::npos ? 1 : std::stoi(what.substr(c + 1))};
				p.swap_after = o.swap_alert;
				p.react_at = o.now;
			}
		}
	});
}

// A chat mood acted out as a test (on the pet thread). A yank needs a line to rip out, so an
// OBSPETS Chat shows `text` first; `automod`: as if Auto mod deleted it (nothing is deleted for real).
void PetSource::act_out(Pet &p, const std::string &kind, const std::string &user, const std::string &text, bool automod)
{
	std::string id;
	if (kind == "yank")
		with_chat(p, [&](ChatSource &c) {
			id = c.add_test_line(user, text, "#1E90FF");
			if (automod)
				c.strike(id);
		});
	p.has_mood = true;
	p.next_mood = MoodReq{kind, p.now, user, "#a6c1db", text, id, 0.35, 0.93, automod, true};
}

// The settings page's chat tester: play what the AI picked for a line the streamer typed.
void PetSource::try_mood(const std::string &kind, const std::string &user, const std::string &text, bool automod)
{
	post([kind, user, text, automod](PetSource &o) {
		if (Pet *p = o.pick(kind))
			o.act_out(*p, kind, user, text, automod);
	});
}

// ------------------------------------------------------------------ shared helpers

PetSource *PetSource::from(obs_source_t *s)
{
	const char *id = s ? obs_source_get_unversioned_id(s) : nullptr;
	if (!id || strcmp(id, PET_SOURCE_ID) != 0)
		return nullptr;
	return static_cast<PetSource *>(obs_obj_get_data(s));
}

static std::string who(const std::string &pet) { return pet.empty() ? std::string("your pet") : pet; }

std::string pet_name_now(PetSource *pet, obs_data_t *s)
{
	std::string nm = trim(str(s, "name"));
	if (nm.empty() && pet) {
		std::lock_guard<std::mutex> g(pet->mu_);
		nm = pet->ui.name;
	}
	return nm;
}

std::string pet_reward_now(PetSource *pet, obs_data_t *s)
{
	return trim(fill_name(str(s, "reward"), pet_name_now(pet, s)));
}

static std::string pack_label(const std::string &id)
{
	std::string s = id;
	std::replace(s.begin(), s.end(), '_', ' ');
	std::replace(s.begin(), s.end(), '-', ' ');
	if (!s.empty())
		s[0] = (char)toupper((unsigned char)s[0]);
	return s;
}

std::string pet_pack_name(const std::string &id)
{
	for (auto &pk : list_packs())
		if (pk.id == id) {
			std::string err;
			if (auto P = load_pack(pk.path, err))
				return P->name;
			break;
		}
	return pack_label(id);
}

ChatKind pet_chat_kind(PetSource *pet, obs_data_t *s, std::string &name)
{
	std::string pick = trim(str(s, "perch"));
	if (pet) {
		std::lock_guard<std::mutex> g(pet->mu_);
		name = pick.empty() ? pet->ui.auto_chat : pick;
	} else {
		name = pick;
	}
	if (pick == NO_SOURCE || name.empty()) {
		name.clear();
		return ChatKind::None;
	}
	obs_source_t *src = obs_get_source_by_name(name.c_str());
	if (!src)
		return ChatKind::None;
	ChatKind k = ChatSource::from(src) ? ChatKind::Ours : ChatKind::Other;
	obs_source_release(src);
	return k;
}

// UI thread: creates an OBSPETS Chat in the current scene (bottom left, under `pet` if given) and
// picks it as `pet`'s chat source. Returns the new source's name, or "" if no scene was available.
std::string add_chat_for_pet(PetSource *pet)
{
	bool studio = obs_frontend_preview_program_mode_active();
	obs_source_t *scene_src = studio ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (!scene) {
		obs_source_release(scene_src);
		return "";
	}
	std::string name = "OBSPETS Chat";
	for (int i = 2;; i++) {
		obs_source_t *e = obs_get_source_by_name(name.c_str());
		if (!e)
			break;
		obs_source_release(e);
		name = "OBSPETS Chat " + std::to_string(i);
	}
	obs_source_t *chat = obs_source_create(CHAT_SOURCE_ID, name.c_str(), nullptr, nullptr);
	if (chat) {
		obs_sceneitem_t *item = obs_scene_add(scene, chat);
		if (item) {
			obs_video_info ovi;
			double H = obs_get_video_info(&ovi) ? ovi.base_height : 1080;
			double floor_px = pet && pet->lead_ ? pet->lead_->floor_px : 24.0;
			vec2 pos;
			vec2_set(&pos, 24.0f, (float)std::max(0.0, H - obs_source_get_height(chat) - 24 - floor_px));
			obs_sceneitem_set_pos(item, &pos);
			if (pet)
				if (obs_sceneitem_t *mine = obs_scene_find_source(scene, obs_source_get_name(pet->source)))
					obs_sceneitem_set_order_position(item, obs_sceneitem_get_order_position(mine));
		}
		obs_source_release(chat);
		if (pet) {
			Data u = data_new();
			obs_data_set_string(u.get(), "perch", name.c_str());
			obs_source_update(pet->source, u.get());
		}
	} else {
		name.clear();
	}
	obs_source_release(scene_src);
	return name;
}

// UI thread: finds an existing OBSPETS source or creates one, adds it to the current scene, returns its name.
std::string add_pet_to_scene()
{
	bool studio = obs_frontend_preview_program_mode_active();
	obs_source_t *scene_src = studio ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (!scene) {
		obs_source_release(scene_src);
		return "";
	}
	obs_source_t *pet = nullptr;
	obs_enum_sources(
		[](void *p, obs_source_t *s) {
			const char *id = obs_source_get_unversioned_id(s);
			if (id && strcmp(id, PET_SOURCE_ID) == 0) {
				*static_cast<obs_source_t **>(p) = obs_source_get_ref(s);
				return false;
			}
			return true;
		},
		&pet);
	if (!pet) {
		std::string name = "OBSPETS";
		for (int i = 2;; i++) {
			obs_source_t *e = obs_get_source_by_name(name.c_str());
			if (!e)
				break;
			obs_source_release(e);
			name = "OBSPETS " + std::to_string(i);
		}
		pet = obs_source_create(PET_SOURCE_ID, name.c_str(), nullptr, nullptr);
	}
	std::string out;
	if (pet) {
		out = obs_source_get_name(pet);
		if (!obs_scene_find_source_recursive(scene, out.c_str()))
			obs_scene_add(scene, pet);
		obs_source_release(pet);
	}
	obs_source_release(scene_src);
	return out;
}

// ------------------------------------------------------------------ properties (minimal)

// Concise account summary for the top of an OBSPETS source's properties dialog.
static std::string account_text(const HubStatus &st, const std::string &pet)
{
	std::string s;
	bool tw = !st.login.empty(), kk = !st.kick_login.empty();
	if (st.linking) {
		s = "Your code: " + st.code +
		    "\nA page opened in your browser. Check that it shows this same code, then click Continue with " +
		    (st.link_platform == "kick" ? "Kick" : "Twitch") + ". This window updates by itself when you're done.";
		if (!st.link_url.empty())
			s += "\nNo page? Go to " + st.link_url + " on any device and type the code.";
	} else if (st.signed_in && (tw || kk)) {
		s = "Signed in with ";
		if (tw)
			s += "Twitch as " + st.login;
		if (tw && kk)
			s += " and ";
		if (kk)
			s += "Kick as " + st.kick_login;
		s += ". ";
		bool live = (!tw || st.twitch_live) && (!kk || st.kick_live);
		s += !live ? "Connecting to your channel\xE2\x80\xA6"
		     : tw   ? "Reacting to your follows, subs, gifts, raids, channel points and chat."
			    : "Reacting to your subs, gifts, Kicks, hosts, channel points and chat.";
		if (!st.moods_server_on)
			s += "\nChat reactions are turned off in the OBSPETS settings (Reactions & AI).";
		if (!st.mood_note.empty())
			s += "\n" + st.mood_note;
	} else if (st.signed_in) {
		s = "Signed in, but with neither Twitch nor Kick any more. Sign in with one so " + who(pet) + " reacts to your channel.";
	} else {
		s = "Sign in with Twitch, Kick or both so " + who(pet) +
		    " reacts to follows, subs, gifts, raids, channel points and chat.";
	}
	if (!st.message.empty())
		s += "\n\n" + st.message;
	return s;
}

void add_account_properties(obs_properties_t *p, const std::string &pet_name, const std::string &prefix)
{
	HubStatus st = Hub::get().status();
	std::string head = prefix.empty() ? account_text(st, pet_name) : prefix + "\n\n" + account_text(st, pet_name);
	obs_properties_add_text(p, "account", head.c_str(), OBS_TEXT_INFO);

	bool tw = !st.login.empty(), kk = !st.kick_login.empty();
	auto again = [&](const char *id, const std::string &missing, const char *pf, const char *label) {
		if (missing.empty() || st.linking)
			return;
		obs_properties_add_text(p, (std::string(id) + "_info").c_str(),
					(std::string(pf) + " needs you to sign in again to turn on: " + missing + ".").c_str(), OBS_TEXT_INFO);
		obs_properties_add_button(p, id, label, [](obs_properties_t *, obs_property_t *b, void *) {
			Hub::get().sign_in(strcmp(obs_property_name(b), "again_kick") == 0 ? "kick" : "twitch");
			return false;
		});
	};
	if (tw)
		again("again_twitch", st.twitch_missing, "Twitch", "Sign in with Twitch again");
	if (kk)
		again("again_kick", st.kick_missing, "Kick", "Sign in with Kick again");

	if (st.linking) {
		obs_properties_add_button(p, "sign_in_page", "Open the sign-in page again", [](obs_properties_t *, obs_property_t *, void *) {
			Hub::get().open_sign_in_page();
			return false;
		});
		obs_properties_add_button(p, "sign_in_cancel", "Cancel sign-in", [](obs_properties_t *, obs_property_t *, void *) {
			Hub::get().cancel_sign_in();
			return false;
		});
	} else {
		bool any = st.signed_in && (tw || kk);
		if (!tw)
			obs_properties_add_button(p, "sign_in", any ? "Also sign in with Twitch" : "Sign in with Twitch",
						  [](obs_properties_t *, obs_property_t *, void *) {
							  Hub::get().sign_in("twitch");
							  return false;
						  });
		if (!kk)
			obs_properties_add_button(p, "sign_in_kick", any ? "Also sign in with Kick" : "Sign in with Kick",
						  [](obs_properties_t *, obs_property_t *, void *) {
							  Hub::get().sign_in("kick");
							  return false;
						  });
		const char *out = tw && kk ? "Sign out of Twitch and Kick" : tw ? "Sign out of Twitch" : kk ? "Sign out of Kick" : "Sign out";
		if (st.signed_in)
			obs_properties_add_button(p, "sign_out", out, [](obs_properties_t *, obs_property_t *, void *) {
				Hub::get().sign_out();
				return false;
			});
	}
}

obs_properties_t *PetSource::properties()
{
	obs_properties_t *p = obs_properties_create();
	Data s(obs_source_get_settings(source));
	std::string nm = pet_name_now(this, s.get());
	PackPtr P;
	std::string pack_err;
	{
		std::lock_guard<std::mutex> g(mu_);
		P = ui.pack;
		pack_err = ui.pack_err;
	}

	std::string prefix;
	if (!P)
		prefix = "Character didn't load: " + (pack_err.empty() ? std::string("open Settings to pick one.") : pack_err);
	add_account_properties(p, nm, prefix);

	obs_property_t *open_set = obs_properties_add_button(p, "open_settings", "Open settings", [](obs_properties_t *, obs_property_t *, void *d) {
		auto *pet = static_cast<PetSource *>(d);
		const char *name = obs_source_get_name(pet->source);
		open_settings("pet", name ? name : "");
		return false;
	});
	obs_property_set_long_description(open_set, "Opens OBSPETS Settings in your browser. All changes save automatically.");

	obs_property_t *ed = obs_properties_add_button(p, "open_editor", "Edit characters", [](obs_properties_t *, obs_property_t *, void *d) {
		auto *pet = static_cast<PetSource *>(d);
		std::string id;
		{
			std::lock_guard<std::mutex> g(pet->mu_);
			id = pet->ui.pack ? pet->ui.pack->id : "";
		}
		open_editor(id);
		return false;
	});
	obs_property_set_long_description(ed, "Opens OBSPETS Settings on Characters, with this pet's character: change it, or make your own.");

	return p;
}

// ------------------------------------------------------------------ registration

void register_pet_source()
{
	obs_source_info si = {};
	si.id = PET_SOURCE_ID;
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW;
	si.icon_type = OBS_ICON_TYPE_CUSTOM;
	si.get_name = [](void *) { return "OBSPETS"; };   // (also what a new one is called in Sources)
	si.create = [](obs_data_t *s, obs_source_t *src) -> void * { return new PetSource(s, src); };
	si.destroy = [](void *d) { delete static_cast<PetSource *>(d); };
	si.get_defaults = [](obs_data_t *s) { PetSource::defaults(s); };
	si.get_properties = [](void *d) -> obs_properties_t * {
		if (d)
			return static_cast<PetSource *>(d)->properties();
		return obs_properties_create();
	};
	si.update = [](void *d, obs_data_t *s) {
		auto *pet = static_cast<PetSource *>(d);
		pet->last_edit_ns_ = os_gettime_ns();
		pet->update(s);
	};
	si.video_tick = [](void *d, float t) { static_cast<PetSource *>(d)->tick(t); };
	si.video_render = [](void *d, gs_effect_t *) { static_cast<PetSource *>(d)->render(); };
	si.get_width = [](void *d) { return static_cast<PetSource *>(d)->width(); };
	si.get_height = [](void *d) { return static_cast<PetSource *>(d)->height(); };
	obs_register_source(&si);
}

} // namespace obspet
