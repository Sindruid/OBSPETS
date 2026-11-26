// The OBS side: the OBSPETS source (settings, properties, the tick, the hub's events coming in,
// and the rules for which of its pets are on screen and who reacts), and each pet's settings.
#include "pet.hpp"
#include "chat-source.hpp"
#include "editor.hpp"

#include <obs-frontend-api.h>
#include <graphics/vec2.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace obspet {

static const char *SOURCE_ID = "obspet";   // OBS saves sources by this id: changing it orphans existing Pets
static const double LABEL_LIFE = 4.5;
static const char *DEFAULT_PACKS[] = {"blob"};   // the first one found is the default character
// the characters pets 2, 3, ... start as (the default character if one isn't there)
static const char *EXTRA_PACKS[] = {"cat-ginger", "dog-retriever", "cat-black", "dog-husky", "cat-tabby"};

// "How often it reacts to chat": seconds between chat reactions (the mood engine's gap), and
// between the sounds it makes for them (mood_gap), at least
static const struct {
	const char *id, *label;
	int every, sound_gap;
} CHATTY[] = {
	{"rarely", "Rarely (about once a minute at most)", 60, 120},
	{"sometimes", "Sometimes (every 15 seconds at most)", 15, 30},
	{"often", "Often (every 8 seconds at most)", 8, 20},
};

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

// "Who hears it" is OBS's audio monitoring for the source (on the UI thread, which owns it). Off
// leaves it as it was: the pet just makes no sound.
void set_monitoring(obs_source_t *src, Hear h)
{
	if (h == Hear::Off)
		return;
	obs_queue_task(
		OBS_TASK_UI,
		[](void *p) {
			auto *a = static_cast<std::pair<obs_weak_source_t *, obs_monitoring_type> *>(p);
			if (obs_source_t *s = obs_weak_source_get_source(a->first)) {
				obs_source_set_monitoring_type(s, a->second);
				obs_source_release(s);
			}
			obs_weak_source_release(a->first);
			delete a;
		},
		new std::pair<obs_weak_source_t *, obs_monitoring_type>(obs_source_get_weak_source(src), monitoring_of(h)),
		false);
}

// On the first frame. A Pet saved by 0.1 (which had no sound) comes back with no audio tracks at
// all, and loading a scene sets the tracks and monitoring from the save after the source is made.
// So this waits for the first frame (this thread), and hands it to the UI thread, where it runs
// after the loading. The first time: every audio track, and "who hears it". After that, monitoring
// picked in Advanced Audio Properties is what "who hears it" then says.
static void first_audio(obs_source_t *src, bool first, Hear want)
{
	struct A {
		obs_weak_source_t *w;
		bool first;
		Hear want;
	};
	obs_queue_task(
		OBS_TASK_UI,
		[](void *p) {
			auto *a = static_cast<A *>(p);
			if (obs_source_t *s = obs_weak_source_get_source(a->w)) {
				if (a->first) {
					obs_source_set_audio_mixers(s, 0x3F);
					if (a->want != Hear::Off)
						obs_source_set_monitoring_type(s, monitoring_of(a->want));
				} else if (a->want != Hear::Off) {
					obs_monitoring_type m = obs_source_get_monitoring_type(s);
					const char *got = m == OBS_MONITORING_TYPE_NONE ? "stream"
							  : m == OBS_MONITORING_TYPE_MONITOR_ONLY ? "me"
												  : "both";
					if (hear_of(got) != a->want) {
						Data u = data_new();
						obs_data_set_string(u.get(), "hear", got);
						obs_source_update(s, u.get());
					}
				}
				obs_source_release(s);
			}
			obs_weak_source_release(a->w);
			delete a;
		},
		new A{obs_source_get_weak_source(src), first, want}, false);
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
	obs_data_set_default_string(s, "chatty", "sometimes");
	obs_data_set_default_string(s, "personality", "");
	obs_data_set_default_string(s, "ignore", DEFAULT_BOTS);
	obs_data_set_default_string(s, "label_after", " pet {name}");
	obs_data_set_default_int(s, "label_color", 0xFFDEE4EC);
	Data f = data_new();
	obs_data_set_string(f.get(), "face", strcmp(text_id(), "text_gdiplus") == 0 ? "Segoe UI Semibold" : "Sans Serif");
	obs_data_set_int(f.get(), "size", 26);
	obs_data_set_default_obj(s, "label_font", f.get());
	obs_data_set_default_string(s, "hear", "both");   // (alert overlays can read it: "me" and "off" mean the stream doesn't hear the pet)
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
	visit = obs_data_get_double(s, "visit");
	perch_box = obs_data_get_bool(s, "perch_box");
	other_yank = obs_data_get_bool(s, "other_yank");
	naps = obs_data_get_double(s, "naps");
	energy = obs_data_get_double(s, "energy");
	alert_name = trim(str(s, "alert"));
	reactions = obs_data_get_bool(s, "reactions");
	twitch_alerts = obs_data_get_bool(s, "twitch_alerts");
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
	int every = 15;
	mood_gap = 30;
	for (auto &c : CHATTY)
		if (str(s, "chatty") == c.id)
			every = c.every, mood_gap = c.sound_gap;
	scan_t = 0;

	// what the chat-mood engine needs from this Pet
	mood.on = reactions && obs_data_get_bool(s, "moods");
	mood.allow_delete = obs_data_get_bool(s, "allow_delete");
	mood.gap = every;
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
		if (!obs_data_get_bool(s, do_key(r.id, slot).c_str())) {
			wont.insert(r.id);
			// (a rip is also how Auto mod deletes: the model may still pick it; this pet just won't act it out)
			if (strncmp(r.label, "Chat:", 5) == 0 && strcmp(r.id, "yank") != 0)
				mood.skip.push_back(r.id);
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
	if (obs_data_has_user_value(settings, "mood_every")) {
		long long e = obs_data_get_int(settings, "mood_every");
		obs_data_set_string(settings, "chatty", e >= 35 ? "rarely" : e <= 10 ? "often" : "sometimes");
		obs_data_erase(settings, "mood_every");
	}
	obs_data_erase(settings, "mood_gap");
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
	Hear h = hear_of(str(s, "hear"));
	if (hear_set && h != hear)
		set_monitoring(source, h);   // (before the first frame, tick does it)
	hear = h;

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
	scan_t -= dt;
	if (scan_t <= 0) {
		scan_t = 0.25;
		scan(*this);
		if (on == On::Floor && mode == Mode::Ground)
			y = floorY;
	}
	// on the chat, follow its top smoothly (the page slides its lines in over about 0.26 s)
	if (on == On::Perch && has_perch && mode != Mode::Air)
		y += (perch_y - y) * (1 - std::exp(-dt * 14));
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
		first_audio(source, setup_audio, hear);
		setup_audio = false;
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

static const struct {
	const char *id, *label;
} TESTS[] = {
	{"follow", "Someone follows"},
	{"sub", "Someone subscribes"},
	{"gift:5", "Someone gifts 5 subs"},
	{"bits:100", "Someone cheers 100 bits"},
	{"raid:20", "A raid with 20 viewers"},
	{"pet", "Someone pets it with channel points"},
	{"mood:yank", "It rips a message out of the chat"},
	{"mood:automod", "Auto mod catches spam (nothing is deleted)"},
	{"chat", "Someone says hi in chat (signed in; read like real chat)"},
};

// The chat reactions, as the "Try a reaction" list says them (ids from MOOD_NAMES, and "automod":
// a yank of a made-up spam line, crossed out as when Auto mod deletes one).
static const struct {
	const char *id, *label;
} MOOD_TESTS[] = {
	{"happy", "Chat: a happy hop"},
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
	{"yank", "Chat: rips a message out of the chat"},
	{"automod", "Chat: Auto mod catches spam (crossed out; nothing is deleted)"},
	// and more alerts
	{"gift:20", "Someone gifts 20 subs"},
	{"bits:1000", "Someone cheers 1,000 bits"},
	{"raid:60", "A raid with 60 viewers"},
};

void PetSource::test(const std::string &what, const std::string &pack_id)
{
	if (what == "chat") {
		std::string nm;
		{
			std::lock_guard<std::mutex> g(mu_);
			nm = ui.name;
		}
		Hub::get().test_chat("inkwell", "hi " + nm + "! you're the best");
		return;
	}
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
			if (what == "pet") {
				p.has_pet = true;
				p.next_pet_user = "inkwell";
				p.next_pet_color = "#a6c1db";
			} else if (what.rfind("mood:", 0) == 0) {
				// an OBSPETS Chat shows the line first, so there's a real one to yank
				std::string id;
				bool automod = what == "mood:automod";
				std::string user = automod ? "v1ewb0t_4u" : "inkwell",
					    text = automod ? "best viewers and followers on streamboost dot shop" : "this pet is overrated";
				if (what == "mood:yank" || automod)
					with_chat(p, [&](ChatSource &c) {
						id = c.add_test_line(user, text, "#1E90FF");
						if (automod)
							c.strike(id);   // (as if Auto mod deleted it: nothing is deleted for real)
					});
				p.has_mood = true;
				p.next_mood = MoodReq{automod ? "yank" : what.substr(5), p.now, user, "#a6c1db", text, id, 0.35, 0.93, automod, true};
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

// ------------------------------------------------------------------ properties

static obs_properties_t *group(obs_properties_t *p, const char *id, const char *title)
{
	obs_properties_t *g = obs_properties_create();
	obs_properties_add_group(p, id, title, OBS_GROUP_NORMAL, g);
	return g;
}

static std::string who(const std::string &pet) { return pet.empty() ? std::string("the pet") : pet; }

// The top of the window: whether it's signed in, and what to do next.
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
		// (Kick doesn't say when someone follows)
		s += !live ? "Connecting to your channel\xE2\x80\xA6"
		     : tw   ? "Reacting to your follows, subs, gifts, raids, channel points and chat."
			    : "Reacting to your subs, gifts, Kicks, hosts, channel points and chat.";
		if (!st.moods_server_on)
			s += "\nReactions to chat are turned off. To turn them on, click \"Chat reactions and limits\" below.";
		if (!st.mood_note.empty())
			s += "\n" + st.mood_note;
	} else if (st.signed_in) {
		s = "Signed in, but with neither Twitch nor Kick any more. Sign in with one so " + who(pet) + " reacts to your channel.";
	} else {
		s = "Sign in with Twitch, Kick or both so " + who(pet) +
		    " reacts to follows, subs, gifts, raids, channel points and chat. Without signing in, it still walks about, naps and sits on your chat.";
	}
	if (!st.message.empty())
		s += "\n\n" + st.message;
	return s;
}

static std::string pet_name_now(PetSource *pet, obs_data_t *s)
{
	std::string nm = trim(str(s, "name"));
	if (nm.empty()) {
		std::lock_guard<std::mutex> g(pet->mu_);
		nm = pet->ui.name;
	}
	return nm;
}

// The channel-point reward as it would be made right now: the reward text with {name} filled in.
static std::string reward_now(PetSource *pet, obs_data_t *s)
{
	return trim(fill_name(str(s, "reward"), pet_name_now(pet, s)));
}

// The reward line and button say which reward and whether it's there (on each platform it's signed
// in with), and keep up as the name or the reward text change. The fields with {name} say what it
// stands for.
static void reward_text(PetSource *pet, obs_properties_t *p, obs_data_t *s)
{
	std::string r = reward_now(pet, s), nm = pet_name_now(pet, s);
	HubStatus st = Hub::get().status();
	struct On {
		bool kick;
		Reward state;
		std::string msg;
	};
	std::vector<On> on;
	if (!st.login.empty())
		on.push_back({false, st.reward_title == r ? st.reward : Reward::Unknown, st.reward_msg});
	if (!st.kick_login.empty())
		on.push_back({true, st.reward_title == r ? st.kick_reward : Reward::Unknown, st.kick_reward_msg});
	std::string line, button;
	if (r.empty()) {
		line = "No channel-point reward: its title under \"What it reacts to\" is empty.";
	} else {
		bool missing = false, failed = false;
		for (auto &o : on) {
			std::string l;
			switch (o.state) {
			case Reward::There:
				l = "\xE2\x9C\x93 Viewers can pet " + who(nm) + " with your channel-point reward \"" + r + "\".";
				break;
			case Reward::Made:
				l = "\xE2\x9C\x93 Made the channel-point reward \"" + r + "\" (100 points). To change its cost" +
				    (o.kick ? ", go to your channel points on Kick's dashboard."
					    : " or picture, go to Twitch: Creator Dashboard, Viewer Rewards, Channel Points.") +
				    " Keep its title.";
				break;
			case Reward::NotAllowed:
				l = o.kick ? "Kick doesn't let this channel have channel-point rewards yet. Everything else works."
					   : "Channel points need Twitch Affiliate or Partner, so there's no reward to make yet. Everything else works.";
				break;
			case Reward::Checking:
				l = "Checking your channel-point rewards\xE2\x80\xA6";
				break;
			case Reward::Failed:
				l = "Couldn't make the reward \"" + r + "\". " + o.msg;
				failed = true;
				break;
			default:
				l = "No \"" + r + "\" reward yet.";
				missing = true;
			}
			line += (line.empty() ? "" : "\n") + (on.size() > 1 ? std::string(o.kick ? "Kick: " : "Twitch: ") : "") + l;
		}
		if (missing && on.size() == 1)
			line = "Let viewers pet " + who(nm) + " with channel points. This makes a reward called \"" + r + "\" on your channel.";
		if (missing)
			button = "Make the \"" + r + "\" reward";
		else if (failed)
			button = "Try again";
	}
	if (obs_property_t *t = obs_properties_get(p, "reward_status"))
		obs_property_set_description(t, line.c_str());
	if (obs_property_t *b = obs_properties_get(p, "make_reward")) {
		obs_property_set_description(b, button.c_str());
		obs_property_set_visible(b, !button.empty());
	}
	if (obs_property_t *f = obs_properties_get(p, "reward"))
		obs_property_set_description(f, ("Channel-point reward title ({name} = " + who(nm) + ")").c_str());
	if (obs_property_t *f = obs_properties_get(p, "label_after"))
		obs_property_set_description(f, ("Text after the viewer's name ({name} = " + who(nm) + ")").c_str());
}

static bool on_name(void *d, obs_properties_t *p, obs_property_t *, obs_data_t *s)
{
	reward_text(static_cast<PetSource *>(d), p, s);
	return true;
}

// The character list shows names, not folder names: "blob" -> "Blob".
static std::string pack_label(const std::string &id)
{
	std::string s = id;
	std::replace(s.begin(), s.end(), '_', ' ');
	std::replace(s.begin(), s.end(), '-', ' ');
	if (!s.empty())
		s[0] = (char)toupper((unsigned char)s[0]);
	return s;
}

// A new character: a pet still called after the old one takes the new one's name.
static bool on_pack(void *d, obs_properties_t *p, obs_property_t *, obs_data_t *s)
{
	auto *pet = static_cast<PetSource *>(d);
	std::string old_name, cur_id;
	{
		std::lock_guard<std::mutex> g(pet->mu_);
		old_name = pet->ui.pack ? pet->ui.pack->name : "";
		cur_id = pet->ui.pack ? pet->ui.pack->id : "";
	}
	// OBS runs this on every rebuild of the properties too (a button, a status change), not only
	// when a character is picked. So only a pick of a character other than the one in use counts,
	// and never while a folder ("Or use a character folder") overrides the list: otherwise a rebuild
	// renamed the pet after whatever the list happened to say.
	if (!trim(str(s, "pack_dir")).empty())
		return false;
	std::string nm = trim(str(s, "name")), id = str(s, "pack");
	if (id.empty() || id == cur_id || !(nm.empty() || nm == old_name))
		return false;
	std::string want = pack_label(id);
	for (auto &pk : list_packs())
		if (pk.id == id) {
			std::string err;
			if (auto P = load_pack(pk.path, err))
				want = P->name;
			break;
		}
	if (want == nm)
		return false;
	obs_data_set_string(s, "name", want.c_str());
	reward_text(pet, p, s);
	return true;
}

// Every source with a picture, for the chat, alert and keep-off pickers (not the pet itself).
static std::vector<std::string> picture_sources(obs_source_t *self)
{
	struct Ctx {
		obs_source_t *self;
		std::vector<std::string> names;
	} ctx{self, {}};
	obs_enum_sources(
		[](void *p, obs_source_t *s) {
			auto *c = static_cast<Ctx *>(p);
			const char *n = obs_source_get_name(s);
			if (s != c->self && n && *n && (obs_source_get_output_flags(s) & OBS_SOURCE_VIDEO))
				c->names.push_back(n);
			return true;
		},
		&ctx);
	std::sort(ctx.names.begin(), ctx.names.end(), [](const std::string &a, const std::string &b) { return lower(a) < lower(b); });
	return ctx.names;
}

// A source picker: "find it by itself" (saying what it found), every source, and "none".
static void source_list(obs_properties_t *g, const char *key, const char *label, const std::vector<std::string> &names,
			const std::string &cur, const std::string &found, const char *word)
{
	obs_property_t *l = obs_properties_add_list(g, key, label, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	std::string a = found.empty() ? std::string("Find it by itself (a source with \"") + word + "\" in its name)"
				      : "Find it by itself (found: " + found + ")";
	obs_property_list_add_string(l, a.c_str(), "");
	bool listed = cur.empty() || cur == NO_SOURCE;
	for (auto &n : names) {
		obs_property_list_add_string(l, n.c_str(), n.c_str());
		listed = listed || n == cur;
	}
	if (!listed)
		obs_property_list_add_string(l, (cur + " (not found)").c_str(), cur.c_str());
	obs_property_list_add_string(l, "None", NO_SOURCE);
}

// Picking a source under "Keep it off" adds it to the list below.
static bool on_avoid_add(obs_properties_t *p, obs_property_t *, obs_data_t *s)
{
	std::string n = str(s, "avoid_add");
	if (n.empty())
		return false;
	if (obs_property_t *l = obs_properties_get(p, "avoid_list"))
		obs_property_set_visible(l, true);
	auto have = avoid_list(s);
	if (std::find(have.begin(), have.end(), n) == have.end()) {
		obs_data_array_t *arr = obs_data_get_array(s, "avoid_list");
		if (!arr)
			arr = obs_data_array_create();
		Data it = data_new();
		obs_data_set_string(it.get(), "value", n.c_str());
		obs_data_array_push_back(arr, it.get());
		obs_data_set_array(s, "avoid_list", arr);
		obs_data_array_release(arr);
	}
	obs_data_set_string(s, "avoid_add", "");
	return true;
}

// ------------------------------------------------------------------ the chat it uses

// Which chat the properties are about right now: the one picked, or what "find it by itself" found.
static ChatKind ui_chat(PetSource *pet, obs_data_t *s, std::string &name)
{
	std::string pick = trim(str(s, "perch"));
	{
		std::lock_guard<std::mutex> g(pet->mu_);
		name = pick.empty() ? pet->ui.auto_chat : pick;
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

// The line under "Your chat": what it does with this chat, and what would make it better.
static void chat_text(PetSource *pet, obs_properties_t *p, obs_data_t *s)
{
	std::string name, pet_name = who(pet_name_now(pet, s));
	ChatKind k = ui_chat(pet, s, name);
	bool picked_none = trim(str(s, "perch")) == NO_SOURCE;
	std::string t;
	switch (k) {
	case ChatKind::Ours:
		t = "\xE2\x9C\x93 \"" + name + "\" is an OBSPETS Chat: " + pet_name +
		    " sits on the newest message, gets bumped up by new ones, and rips real messages right out of it.";
		break;
	case ChatKind::Other:
		t = "\"" + name + "\" isn't an OBSPETS Chat, so " + pet_name +
		    " just walks around it. You can let it sit on this chat and rip messages out of it below, but both "
		    "work much better with an OBSPETS Chat, which has the usual chat overlay options too (fonts, colors, "
		    "badges, 7TV/BTTV/FFZ emotes, filters, custom CSS).";
		break;
	default:
		t = picked_none ? "No chat: " + pet_name + " walks along the bottom and ignores chat on screen."
				: "No chat found in this scene. Add an OBSPETS Chat (button below) and " + pet_name +
					  " sits on its messages and rips them out. Or pick your own chat above.";
	}
	t += "\nAuto mod works with any chat: the message is deleted on Twitch or Kick the moment it's caught, so every chat "
	     "overlay drops it, and the pet flings it crossed out.";
	if (obs_property_t *i = obs_properties_get(p, "chat_info"))
		obs_property_set_description(i, t.c_str());
	bool other = k == ChatKind::Other;
	if (obs_property_t *b = obs_properties_get(p, "perch_box"))
		obs_property_set_visible(b, other);
	if (obs_property_t *b = obs_properties_get(p, "other_yank"))
		obs_property_set_visible(b, other);
	if (obs_property_t *v = obs_properties_get(p, "visit"))
		obs_property_set_visible(v, k == ChatKind::Ours || (other && obs_data_get_bool(s, "perch_box")));
	if (obs_property_t *b = obs_properties_get(p, "add_chat"))
		obs_property_set_visible(b, k != ChatKind::Ours);
	if (obs_property_t *b = obs_properties_get(p, "add_chat_note"))
		obs_property_set_visible(b, k == ChatKind::Other);
}

static bool on_chat(void *d, obs_properties_t *p, obs_property_t *, obs_data_t *s)
{
	chat_text(static_cast<PetSource *>(d), p, s);
	return true;
}

// "Add an OBSPETS Chat to this scene": bottom left, just under the pet, and picked as its chat.
static bool add_chat(obs_properties_t *p, obs_property_t *, void *d)
{
	auto *pet = static_cast<PetSource *>(d);
	obs_source_t *scene_src = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(scene_src);
	if (!scene) {
		obs_source_release(scene_src);
		return false;
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
			vec2 pos;
			vec2_set(&pos, 24.0f, (float)std::max(0.0, H - obs_source_get_height(chat) - 24 - pet->lead_->floor_px));
			obs_sceneitem_set_pos(item, &pos);
			if (obs_sceneitem_t *mine = obs_scene_find_source(scene, obs_source_get_name(pet->source)))
				obs_sceneitem_set_order_position(item, obs_sceneitem_get_order_position(mine));
		}
		obs_source_release(chat);
		Data s(obs_source_get_settings(pet->source));
		obs_data_set_string(s.get(), "perch", name.c_str());
		obs_source_update(pet->source, s.get());
		chat_text(pet, p, s.get());
	}
	obs_source_release(scene_src);
	return true;
}

// The properties window: the few things most people touch, then "Show all settings" for the rest
// (hidden until ticked, so the window stays short).
// ---- more pets

// Each pet's "Reactions" list: picking one turns it on or off for that pet, and the list goes back
// to saying what's off.
static const char *const CHECK = "\xE2\x9C\x93  ", *const CROSS = "\xE2\x9C\x97  ";

static std::string short_label(const char *label)
{
	std::string l = strncmp(label, "Chat: ", 6) == 0 ? label + 6 : label;
	return lower(l);
}

static void fill_reacts(obs_property_t *l, obs_data_t *s, int slot)
{
	obs_property_list_clear(l);
	std::string off;
	int n = 0;
	for (auto &r : REACTS)
		if (!obs_data_get_bool(s, do_key(r.id, slot).c_str()) && n++ < 3)
			off += (n > 1 ? ", " : "") + short_label(r.label);
	if (n > 3)
		off += " and " + std::to_string(n - 3) + " more";
	obs_property_list_add_string(l, n ? ("Off: " + off).c_str() : "All on", "");
	for (auto &r : REACTS)
		obs_property_list_add_string(l, ((obs_data_get_bool(s, do_key(r.id, slot).c_str()) ? CHECK : CROSS) + std::string(r.label)).c_str(),
					     r.id);
}

static bool on_reacts(obs_properties_t *, obs_property_t *l, obs_data_t *s)
{
	std::string key = obs_property_name(l);   // "reacts", "reacts2", ...
	int slot = key.size() > 6 ? std::stoi(key.substr(6)) - 1 : 0;
	std::string id = str(s, key.c_str());
	if (id.empty())
		return false;
	std::string k = do_key(id.c_str(), slot);
	obs_data_set_bool(s, k.c_str(), !obs_data_get_bool(s, k.c_str()));
	obs_data_set_string(s, key.c_str(), "");
	fill_reacts(l, s, slot);
	return true;
}

static obs_property_t *add_reacts(obs_properties_t *g, obs_data_t *s, int slot, const char *label)
{
	obs_property_t *l = obs_properties_add_list(g, slot_key("reacts", slot).c_str(), label, OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_STRING);
	obs_property_set_long_description(l, "Pick one to turn it on or off for this pet. Another pet on screen does it instead.");
	fill_reacts(l, s, slot);
	obs_property_set_modified_callback(l, on_reacts);
	return l;
}

// Shows what the pet count and the rules make matter: each pet's settings, the swap settings
// when fewer fit on screen than there are, and the chances when something is picked at random.
static bool pets_ui(obs_properties_t *p, obs_property_t *, obs_data_t *s)
{
	while (obs_properties_t *up = obs_properties_get_parent(p))
		p = up;
	int n = (int)std::clamp<long long>(obs_data_get_int(s, "pets"), 1, MAX_PETS);
	int on = (int)std::clamp<long long>(obs_data_get_int(s, "at_once"), 1, MAX_PETS);
	bool more = n > 1, swaps = more && on < n;
	bool chance = more && (str(s, "who") != "turns" || (swaps && str(s, "next_in") == "random"));
	auto show = [&](const std::string &id, bool v) {
		if (obs_property_t *q = obs_properties_get(p, id.c_str()))
			obs_property_set_visible(q, v);
	};
	for (int i = 1; i < MAX_PETS; i++)
		show("g_pet" + std::to_string(i + 1), i < n);
	for (int i = 0; i < MAX_PETS; i++)
		show(slot_key("weight", i), chance && i < n);
	show("who", more);
	show("at_once", more);
	show("swap_min", swaps);
	show("swap_alert", swaps);
	show("next_in", swaps);
	if (obs_property_t *q = obs_properties_get(p, "at_once"))
		obs_property_int_set_limits(q, 1, std::max(1, n), 1);
	if (obs_property_t *q = obs_properties_get(p, "reacts"))
		obs_property_set_description(q, more ? "Pet 1's reactions" : "Reactions");
	return true;
}

static void add_pets(obs_properties_t *p, obs_data_t *s)
{
	obs_properties_t *g = obs_properties_create();
	obs_property_t *np = obs_properties_add_int_slider(g, "pets", "How many", 1, MAX_PETS, 1);
	obs_property_set_modified_callback(np, pets_ui);
	obs_property_t *w = obs_properties_add_list(g, "who", "Who reacts", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(w, "Take turns", "turns");
	obs_property_list_add_string(w, "Random (by chance)", "random");
	obs_property_list_add_string(w, "All of them", "all");
	obs_property_set_long_description(w, "To alerts and channel points. Chat reactions are one pet at a time.");
	obs_property_set_modified_callback(w, pets_ui);
	obs_property_t *ao = obs_properties_add_int_slider(g, "at_once", "On screen at once", 1, MAX_PETS, 1);
	obs_property_set_long_description(ao, "Fewer than all of them: they swap. One jumps off screen and the next jumps on.");
	obs_property_set_modified_callback(ao, pets_ui);
	obs_property_t *sm = obs_properties_add_int_slider(g, "swap_min", "Swap every", 0, 60, 1);
	obs_property_int_set_suffix(sm, " min");
	obs_property_set_long_description(sm, "0: never.");
	obs_property_t *sa = obs_properties_add_bool(g, "swap_alert", "Swap after an alert");
	obs_property_set_long_description(sa, "The pet that reacted leaves when it's done.");
	obs_property_t *ni = obs_properties_add_list(g, "next_in", "Who comes on", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(ni, "Next in line", "turns");
	obs_property_list_add_string(ni, "Random (by chance)", "random");
	obs_property_set_modified_callback(ni, pets_ui);
	obs_property_t *w1 = obs_properties_add_int_slider(g, "weight", "Pet 1's chance", 0, 10, 1);
	obs_property_set_long_description(w1, "Higher is picked more often. 0: never.");
	add_reacts(g, s, 0, "Pet 1's reactions");
	obs_properties_add_group(p, "g_pets", "Pets", OBS_GROUP_NORMAL, g);

	auto packs = list_packs();
	for (int i = 1; i < MAX_PETS; i++) {
		obs_properties_t *gi = obs_properties_create();
		obs_property_t *l = obs_properties_add_list(gi, slot_key("pack", i).c_str(), "Character", OBS_COMBO_TYPE_LIST,
							     OBS_COMBO_FORMAT_STRING);
		std::set<std::string> listed;
		for (auto &pk : packs)
			if (listed.insert(pk.id).second)
				obs_property_list_add_string(l, pack_label(pk.id).c_str(), pk.id.c_str());
		obs_property_t *nm = obs_properties_add_text(gi, slot_key("name", i).c_str(), "Name", OBS_TEXT_DEFAULT);
		obs_property_set_long_description(nm, "Empty: the character's name.");
		obs_properties_add_int_slider(gi, slot_key("scale", i).c_str(), "Size", 1, 12, 1);
		obs_property_t *wi = obs_properties_add_int_slider(gi, slot_key("weight", i).c_str(), "Chance", 0, 10, 1);
		obs_property_set_long_description(wi, "Higher is picked more often. 0: never.");
		add_reacts(gi, s, i, "Reactions");
		std::string id = "g_pet" + std::to_string(i + 1), title = "Pet " + std::to_string(i + 1);
		obs_properties_add_group(p, id.c_str(), title.c_str(), OBS_GROUP_NORMAL, gi);
	}
}

static const char *ADVANCED_GROUPS[] = {"g_try", "g_scene", "g_react", "g_moods", "g_mood", "g_label", "g_char"};

static bool show_advanced(obs_properties_t *p, obs_property_t *, obs_data_t *s)
{
	bool on = obs_data_get_bool(s, "advanced");
	for (const char *id : ADVANCED_GROUPS)
		if (obs_property_t *g = obs_properties_get(p, id))
			obs_property_set_visible(g, on);
	return true;
}

// The Character list is grayed out while a folder ("Or use a character folder") is set, as the
// folder is what's used then. `p` may be any group of the properties.
static void pack_list_state(obs_properties_t *p, obs_data_t *s)
{
	while (obs_properties_t *up = obs_properties_get_parent(p))
		p = up;
	obs_property_t *list = obs_properties_get(p, "pack");
	if (!list)
		return;
	bool folder = !trim(str(s, "pack_dir")).empty();
	obs_property_set_description(list, folder ? "Character (not used)" : "Character");
	obs_property_set_enabled(list, !folder);
	obs_property_set_long_description(list, folder ? "A character folder is set under \"Your own characters\" (Show all settings), "
							 "so that one is used. Clear that folder to pick from this list."
						       : nullptr);
}

obs_properties_t *PetSource::properties()
{
	obs_properties_t *p = obs_properties_create();
	HubStatus st = Hub::get().status();
	Data s(obs_source_get_settings(source));
	std::string nm = pet_name_now(this, s.get());
	PackPtr P;
	std::string pack_err, found_chat, found_alert;
	bool box_moved;
	{
		std::lock_guard<std::mutex> g(mu_);
		P = ui.pack;
		pack_err = ui.pack_err;
		found_chat = ui.auto_chat;
		found_alert = ui.auto_alert;
		box_moved = ui.moved;
	}

	// ---- signing in
	obs_properties_add_text(p, "account", account_text(st, nm).c_str(), OBS_TEXT_INFO);
	bool tw = !st.login.empty(), kk = !st.kick_login.empty();
	if (st.signed_in && (tw || kk) && !st.linking) {
		// the channel-point reward: whether it's there, and a button to make it
		obs_properties_add_text(p, "reward_status", "", OBS_TEXT_INFO);
		obs_properties_add_button(p, "make_reward", "", [](obs_properties_t *, obs_property_t *, void *d) {
			auto *pet = static_cast<PetSource *>(d);
			Data s(obs_source_get_settings(pet->source));
			Hub::get().ensure_reward(reward_now(pet, s.get()));
			return false;
		});
		Hub::get().check_reward(reward_now(this, s.get()));
		obs_properties_add_button(p, "page", "Chat reactions and limits (opens sindruid.com)",
					  [](obs_properties_t *, obs_property_t *, void *) {
						  open_url(Hub::backend() + "/streamtools/obspets");
						  return false;
					  });
	}
	// a sign-in from before a feature needed more of the channel: signing in again turns it on
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
		obs_properties_add_button(p, "sign_in_page", "Open the page again", [](obs_properties_t *, obs_property_t *, void *) {
			Hub::get().open_sign_in_page();
			return false;
		});
		obs_properties_add_button(p, "sign_in_cancel", "Cancel", [](obs_properties_t *, obs_property_t *, void *) {
			Hub::get().cancel_sign_in();
			return false;
		});
	} else {
		// one button per platform it isn't signed in with: either signs in, or adds that one to the account
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
		// (signing out ends this OBS's sign-in, so every platform it's signed in with: say which)
		const char *out = tw && kk ? "Sign out of Twitch and Kick" : tw ? "Sign out of Twitch" : kk ? "Sign out of Kick" : "Sign out";
		if (st.signed_in)
			obs_properties_add_button(p, "sign_out", out, [](obs_properties_t *, obs_property_t *, void *) {
				Hub::get().sign_out();
				return false;
			});
	}

	// ---- the short list
	obs_property_t *list = obs_properties_add_list(p, "pack", "Character", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	std::set<std::string> listed;
	for (auto &pk : list_packs())
		if (listed.insert(pk.id).second)
			obs_property_list_add_string(list, pack_label(pk.id).c_str(), pk.id.c_str());
	obs_property_set_modified_callback2(list, on_pack, this);
	pack_list_state(p, s.get());
	obs_property_t *ed = obs_properties_add_button(p, "edit_pack", "Open pet editor", [](obs_properties_t *, obs_property_t *, void *d) {
		auto *pet = static_cast<PetSource *>(d);
		std::string id;
		{
			std::lock_guard<std::mutex> g(pet->mu_);
			id = pet->ui.pack ? pet->ui.pack->id : "";
		}
		open_editor(id);
		return false;
	});
	obs_property_set_long_description(ed, "Opens the pet editor in your browser. Drop in animations and sounds one at a time; "
					      "the pet here updates as you go.");
	if (!P)   // a character that didn't load says why right here
		obs_properties_add_text(p, "pack_problem",
					("This character didn't load: " + (pack_err.empty() ? std::string("pick another one above.") : pack_err)).c_str(),
					OBS_TEXT_INFO);
	obs_property_t *name_prop = obs_properties_add_text(p, "name", "Name", OBS_TEXT_DEFAULT);
	obs_property_set_long_description(name_prop, "What it's called on stream: in its name label and its channel-point reward.");
	obs_property_set_modified_callback2(name_prop, on_name, this);
	obs_property_t *sz = obs_properties_add_int_slider(p, "scale", "Size", 1, 12, 1);
	obs_property_set_long_description(sz, "How big it is. 5 suits a 1080p canvas.");
	if (P && P->nsounds > 0) {
		obs_property_t *hl = obs_properties_add_list(p, "hear", "Who hears it", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		obs_property_list_add_string(hl, "You and your stream", "both");
		obs_property_list_add_string(hl, "Only your stream", "stream");
		obs_property_list_add_string(hl, "Only you", "me");
		obs_property_list_add_string(hl, "Nobody (no sounds)", "off");
		obs_property_set_long_description(hl, "You hear it through OBS's audio monitoring, like an alert. If you hear it twice "
						      "(an echo), you already hear your stream: pick \"Only your stream\". Its volume is "
						      "under its name in OBS's Audio Mixer.");
	}
	if (box_moved) {
		obs_properties_add_text(p, "moved_info",
					"Its box in this scene was moved or resized. It still finds your chat, but it's made to fill "
					"the whole screen: then it walks along the bottom and is the size set above.",
					OBS_TEXT_INFO);
		obs_properties_add_button(p, "fit", "Fit it to the screen", [](obs_properties_t *, obs_property_t *, void *d) {
			auto *pet = static_cast<PetSource *>(d);
			if (!fit_to_screen(pet->source))
				warn("pet: its box couldn't be fitted to the screen (is it in a moved group?)");
			pet->status_dirty_ = true;
			return false;
		});
	}

	add_pets(p, s.get());

	obs_property_t *tl = obs_properties_add_list(p, "test_pick", "Try a reaction", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (auto &t : TESTS)
		obs_property_list_add_string(tl, t.label, t.id);
	obs_properties_add_button(p, "test_go", "Play it", [](obs_properties_t *, obs_property_t *, void *d) {
		auto *pet = static_cast<PetSource *>(d);
		Data s(obs_source_get_settings(pet->source));
		pet->test(str(s.get(), "test_pick"));
		return false;
	});

	// ---- the chat: shown to everyone, as it decides a lot of what the pet can do
	auto names = picture_sources(source);
	obs_properties_t *gc = group(p, "g_chat", "Your chat");
	source_list(gc, "perch", "Chat source", names, str(s.get(), "perch"), found_chat, "chat");
	obs_property_set_modified_callback2(obs_properties_get(gc, "perch"), on_chat, this);
	obs_properties_add_text(gc, "chat_info", "", OBS_TEXT_INFO);   // (chat_text)
	obs_properties_add_float_slider(gc, "visit", "How often it sits on top of the chat", 0, 1, 0.05);
	obs_property_t *pb = obs_properties_add_bool(gc, "perch_box", "Sit on top of this chat anyway");
	obs_property_set_long_description(pb, "Guesses where the newest message is from the chat's pixels. "
					      "Works best with an OBSPETS Chat.");
	obs_property_set_modified_callback2(pb, on_chat, this);
	obs_property_t *oy = obs_properties_add_bool(gc, "other_yank", "Rip messages out of this chat anyway");
	obs_property_set_long_description(oy, "Flings a copy of the message. The real one stays. "
					      "An OBSPETS Chat lets it rip out the real line.");
	obs_properties_add_button(gc, "add_chat", "Add an OBSPETS Chat to this scene", add_chat);
	obs_properties_add_text(gc, "add_chat_note", "Then hide your old chat source, if you had one.", OBS_TEXT_INFO);

	obs_property_t *adv = obs_properties_add_bool(p, "advanced", "Show all settings");
	obs_property_set_modified_callback(adv, show_advanced);

	// ---- everything else (hidden until "Show all settings")
	obs_properties_t *g = group(p, "g_try", "More reactions to try");
	obs_property_t *ml = obs_properties_add_list(g, "test_more", "Reaction", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (auto &m : MOOD_TESTS)
		obs_property_list_add_string(ml, m.label, strchr(m.id, ':') ? m.id : (std::string("mood:") + m.id).c_str());
	obs_properties_add_button(g, "test_more_go", "Play it", [](obs_properties_t *, obs_property_t *, void *d) {
		auto *pet = static_cast<PetSource *>(d);
		Data s(obs_source_get_settings(pet->source));
		pet->test(str(s.get(), "test_more"));
		return false;
	});

	g = group(p, "g_scene", "Where it walks");
	obs_property_t *fl = obs_properties_add_int_slider(g, "floor", "Height above the bottom edge", 0, 400, 1);
	obs_property_int_set_suffix(fl, " px");
	obs_property_set_long_description(fl, "Raise it if it walks behind something at the bottom of your screen.");
	source_list(g, "alert", "Your alerts (it looks at them)", names, str(s.get(), "alert"), found_alert, "alert");
	obs_property_t *aa = obs_properties_add_list(g, "avoid_add", "Keep it off", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(aa, "Add a source it shouldn't walk in front of\xE2\x80\xA6", "");
	for (auto &n : names)
		obs_property_list_add_string(aa, n.c_str(), n.c_str());
	obs_property_set_modified_callback(aa, on_avoid_add);
	obs_property_t *al = obs_properties_add_editable_list(g, "avoid_list", "", OBS_EDITABLE_LIST_TYPE_STRINGS, nullptr, nullptr);
	obs_property_set_visible(al, !avoid_list(s.get()).empty());   // (an empty list is just a big blank box)

	g = group(p, "g_react", "What it reacts to");
	obs_properties_add_bool(g, "reactions", "React to alerts, channel points and chat");
	obs_properties_add_bool(g, "twitch_alerts", "Follows, subs, gifts, bits, Kicks and raids");
	obs_property_t *rw = obs_properties_add_text(g, "reward", "", OBS_TEXT_DEFAULT);   // (its label: reward_text)
	obs_property_set_modified_callback2(rw, on_name, this);

	// (Auto mod and the limits are on sindruid.com/streamtools/obspets; the personality is the character's, in the editor)
	g = group(p, "g_moods", "Chat");
	obs_properties_add_bool(g, "moods", "React to what people say in chat");
	obs_property_t *ch = obs_properties_add_list(g, "chatty", "How often", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (auto &c : CHATTY)
		obs_property_list_add_string(ch, c.label, c.id);
	obs_property_set_long_description(ch, "At most this often. It only reacts when chat says something worth reacting to.");
	obs_properties_add_text(g, "ignore", "Never react to these chatters (bots), separated by commas", OBS_TEXT_DEFAULT);

	g = group(p, "g_mood", "Habits");
	obs_properties_add_float_slider(g, "naps", "Naps (0 never, 1 normal, 3 a lot)", 0, 3, 0.1);
	obs_properties_add_float_slider(g, "energy", "Energy (walks, hops and swats)", 0, 3, 0.1);

	g = group(p, "g_label", "Name label (when a viewer pets it)");
	obs_properties_add_text(g, "label_after", "", OBS_TEXT_DEFAULT);   // (its label: reward_text)
	obs_properties_add_font(g, "label_font", "Font");
	obs_properties_add_color(g, "label_color", "Text color");

	g = group(p, "g_char", "Your own characters");
	obs_properties_add_button(g, "open_packs", "Open my characters folder", [](obs_properties_t *, obs_property_t *, void *) {
		std::string dir = user_packs_dir();
		os_mkdirs(dir.c_str());
		open_folder(dir);
		return false;
	});
	obs_property_t *pd = obs_properties_add_path(g, "pack_dir", "Or use a character folder from anywhere", OBS_PATH_DIRECTORY, "", nullptr);
	// (the Character list above grays out while a folder is set: show that straight away)
	obs_property_set_modified_callback(pd, [](obs_properties_t *gp, obs_property_t *, obs_data_t *st) {
		pack_list_state(gp, st);
		return true;
	});
	obs_properties_add_text(g, "status", pack_status().c_str(), OBS_TEXT_INFO);
	obs_properties_add_button(g, "reload", "Reload the character (after changing its files)", [](obs_properties_t *, obs_property_t *, void *d) {
		static_cast<PetSource *>(d)->reload_pack();
		return false;
	});

	show_advanced(p, adv, s.get());
	pets_ui(p, nullptr, s.get());
	reward_text(this, p, s.get());
	chat_text(this, p, s.get());
	return p;
}

// ------------------------------------------------------------------ registration

void register_pet_source()
{
	obs_source_info si = {};
	si.id = SOURCE_ID;
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
