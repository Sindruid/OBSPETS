// What the pet does: wandering, the chat perch, alerts, channel-point pets and chat moods.
#include "pet.hpp"
#include "chat-source.hpp"

#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace obspet {

static const double NAP_GRACE = 30;

static double fl(double v) { return std::floor(v + 0.5); }

// ------------------------------------------------------------------ sounds

// Who hears a sound of reaction `kind` ("": the pet's own doings, "idle"): its "sfx_<id>", or the
// source's "hear".
static Hear route(const Pet &d, const std::string &kind)
{
	auto it = d.sfx.find(kind.empty() ? "idle" : kind);
	return it != d.sfx.end() ? it->second : d.hear;
}

// The Pet plays its sounds itself (audio.cpp): to the stream, the streamer or both (`h`). Each file
// is decoded the first time it plays; one that won't decode stays silent, said once.
static bool play_file(Pet &d, const std::string &path, Hear h)
{
	bool me = h == Hear::Both || h == Hear::Me, stream = h == Hear::Both || h == Hear::Stream;
	if (!d.ears)
		me = false;
	// Desktop Audio records what the streamer hears (OBS 32+ says so): the stream gets the
	// streamer's copy through it, so a second copy would be an echo
	if (me && stream && monitor_reaches_stream())
		stream = false;
	Mixer *m = stream ? d.mixer : me ? d.ears : nullptr;
	if (!m)
		return false;
	auto it = d.clips.find(path);
	if (it == d.clips.end()) {
		std::string err;
		ClipPtr c = load_wav(path, m->rate(), err);
		if (!c)
			warn("can't play %s: %s", path.c_str(), err.c_str());
		it = d.clips.emplace(path, c).first;
	}
	if (!it->second)
		return false;
	if (stream)
		d.mixer->play(it->second);
	if (me)
		d.ears->play(it->second);   // (the same rate: both are OBS's)
	return true;
}

// Chat moods can come every few seconds, so a mood only makes a voice sound twice the account's
// time between reactions (10 s at least) after its last one. Alerts and channel-point pets always do. Foley is never held back.
bool sound(Pet &d, const std::string &key, bool from_mood, const char *kind)
{
	Hear h = route(d, kind ? std::string(kind) : d.act);
	if (!d.pack || h == Hear::Off)
		return false;
	auto it = d.pack->sounds.find(key);
	if (it == d.pack->sounds.end() || it->second.files.empty())
		return false;
	const SoundRole &snd = it->second;
	bool voice = is_voice(key);
	if (voice && from_mood && d.now - d.voiced < std::max(10.0, 2 * Hub::get().ai_gap()))
		return false;
	if (!obs_source_active(d.source))   // only while it is on screen
		return false;
	int n = (int)snd.files.size();
	int k = rnd_int(0, n - 1);
	auto lf = d.last_file.find(key);
	if (n > 1 && lf != d.last_file.end() && lf->second == k)
		k = (k + 1) % n;
	d.last_file[key] = k;
	const std::string &path = snd.files[k];
	if (!play_file(d, path, h))
		return false;
	if (voice)
		d.voiced = d.now;
	return true;
}

// ------------------------------------------------------------------ the scene

// Where the pet is: the scene that holds it, searched from the program scene down through nested
// scenes (an "Overlays" scene shown inside every scene is common), and its scene item there.
struct Home {
	obs_source_t *scene_src = nullptr;   // (a reference: release it)
	obs_scene_t *scene = nullptr;
	obs_sceneitem_t *self = nullptr;     // nullptr: it isn't in the program (the scan uses that scene)
};

static bool find_home(obs_scene_t *scene, obs_source_t *self, Home &h, int depth)
{
	if (obs_sceneitem_t *it = obs_scene_find_source_recursive(scene, obs_source_get_name(self))) {
		h.scene = scene;
		h.self = it;
		return true;
	}
	if (depth >= 6)
		return false;
	struct Ctx {
		obs_source_t *self;
		Home *h;
		int depth;
		bool found;
	} ctx{self, &h, depth, false};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			auto *c = static_cast<Ctx *>(p);
			obs_scene_t *sub = obs_sceneitem_visible(item) && !obs_sceneitem_is_group(item)
						   ? obs_scene_from_source(obs_sceneitem_get_source(item))
						   : nullptr;
			if (sub && find_home(sub, c->self, *c->h, c->depth + 1))
				c->found = true;
			return !c->found;
		},
		&ctx);
	return ctx.found;
}

static Home home_of(obs_source_t *self)
{
	Home h;
	obs_source_t *cur = obs_frontend_get_current_scene();
	obs_scene_t *top = obs_scene_from_source(cur);
	if (top && !find_home(top, self, h, 0)) {
		h.scene = top;
		h.self = nullptr;
	}
	if (h.scene) {
		h.scene_src = obs_source_get_ref(obs_scene_get_source(h.scene));
		if (!h.scene_src)
			h.scene = nullptr, h.self = nullptr;
	}
	obs_source_release(cur);
	return h;
}

// An item's transform in its scene: an item in a group goes through the group's too. `box`: the
// transform of its on-screen box (a unit square), else of its source's own pixels.
static void item_xf(obs_scene_t *scene, obs_sceneitem_t *item, bool box, matrix4 &m)
{
	if (box)
		obs_sceneitem_get_box_transform(item, &m);
	else
		obs_sceneitem_get_draw_transform(item, &m);
	if (obs_sceneitem_t *grp = obs_sceneitem_get_group(scene, item)) {
		matrix4 g;
		obs_sceneitem_get_draw_transform(grp, &g);
		matrix4_mul(&m, &m, &g);
	}
}

// An item's box in the pet's own pixels (inv: the scene to the pet's pixels; nullptr: they're the same)
static Box rect_of(obs_scene_t *scene, obs_sceneitem_t *item, const matrix4 *inv)
{
	matrix4 m;
	item_xf(scene, item, true, m);
	Box r{1e18, 1e18, -1e18, -1e18};
	for (int k = 0; k < 4; k++) {
		vec3 v;
		vec3_set(&v, (float)(k & 1), (float)(k >> 1), 0);
		vec3_transform(&v, &v, &m);
		if (inv)
			vec3_transform(&v, &v, inv);
		r.x0 = std::min(r.x0, (double)v.x);
		r.y0 = std::min(r.y0, (double)v.y);
		r.x1 = std::max(r.x1, (double)v.x);
		r.y1 = std::max(r.y1, (double)v.y);
	}
	return r;
}

// Whether the pet's box was moved, resized or turned in its scene (`inv`, if given: its scene's
// coordinates to the pet's own pixels)
static bool box_moved(obs_scene_t *scene, obs_sceneitem_t *self, matrix4 *inv)
{
	matrix4 m;
	item_xf(scene, self, false, m);
	auto close_to = [](float a, float b, float eps) { return std::abs(a - b) < eps; };
	bool same = close_to(m.x.x, 1, 1e-3f) && close_to(m.y.y, 1, 1e-3f) && close_to(m.x.y, 0, 1e-3f) && close_to(m.y.x, 0, 1e-3f) &&
		    close_to(m.t.x, 0, 0.5f) && close_to(m.t.y, 0, 0.5f);
	if (same)
		return false;
	if (inv && !matrix4_inv(inv, &m))
		return false;   // (squashed to nothing: nothing to map)
	return true;
}

// "Fit it to the screen": the pet's box in the program scene (and its group) back to the whole canvas
bool fit_to_screen(obs_source_t *self)
{
	Home h = home_of(self);
	bool done = false;
	if (h.scene && h.self) {
		obs_sceneitem_t *it = h.self;
		obs_sceneitem_defer_update_begin(it);
		vec2 pos, one;
		vec2_set(&pos, 0, 0);
		vec2_set(&one, 1, 1);
		obs_sceneitem_crop crop = {};
		obs_sceneitem_set_bounds_type(it, OBS_BOUNDS_NONE);
		obs_sceneitem_set_rot(it, 0);
		obs_sceneitem_set_scale(it, &one);
		obs_sceneitem_set_crop(it, &crop);
		obs_sceneitem_set_alignment(it, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_pos(it, &pos);
		obs_sceneitem_defer_update_end(it);
		// in a group that's moved itself, its own position can't undo that: say it's still off
		done = !box_moved(h.scene, it, nullptr);
	}
	obs_source_release(h.scene_src);
	return done;
}

// No source named in the settings: the first visible OBSPETS Chat, else the first visible source
// whose name (or browser URL) says "chat" (or "alert"), in its scene and the groups in it. Looked
// for every couple of seconds, as it reads every item's settings.
struct AutoFind {
	obs_source_t *self;
	std::string ours, chat, alert;
};
static bool auto_look(obs_scene_t *, obs_sceneitem_t *item, void *p)
{
	auto *c = static_cast<AutoFind *>(p);
	if (!obs_sceneitem_visible(item))
		return true;
	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, auto_look, p);
		return true;
	}
	obs_source_t *s = obs_sceneitem_get_source(item);
	if (s == c->self)
		return true;
	std::string n = obs_source_get_name(s) ? obs_source_get_name(s) : "";
	if (ChatSource::from(s)) {
		if (c->ours.empty())
			c->ours = n;
		return true;
	}
	std::string ln = lower(n), url;
	const char *id = obs_source_get_unversioned_id(s);
	if (id && strcmp(id, "browser_source") == 0) {
		Data st(obs_source_get_settings(s));
		url = lower(str(st.get(), "url"));
	}
	auto says = [&](const char *w) { return ln.find(w) != std::string::npos || url.find(w) != std::string::npos; };
	if (c->chat.empty() && says("chat"))
		c->chat = n;
	else if (c->alert.empty() && says("alert"))
		c->alert = n;
	return true;
}

static void auto_find(PetSource &ps, obs_scene_t *scene)
{
	AutoFind ctx{ps.source, "", "", ""};
	obs_scene_enum_items(scene, auto_look, &ctx);
	ps.scene.auto_chat = ctx.ours.empty() ? ctx.chat : ctx.ours;
	ps.scene.auto_alert = ctx.alert;
	std::lock_guard<std::mutex> g(ps.mu_);   // (the properties say what was found)
	ps.ui.auto_chat = ps.scene.auto_chat;
	ps.ui.auto_alert = ctx.alert;
}

// The chatters it never reacts to: its own list, and the ones its OBSPETS Chat hides (bots).
void merge_ignore(Pet &d)
{
	d.mood.ignore = d.ignore_own;
	for (auto &n : split(d.chat_hide, ',')) {
		std::string u = lower(n);
		if (!u.empty() && u[0] == '@')
			u = u.substr(1);
		if (!u.empty() && std::find(d.mood.ignore.begin(), d.mood.ignore.end(), u) == d.mood.ignore.end())
			d.mood.ignore.push_back(u);
	}
}

// The scene lookup every pet of a source shares (4 times a second, from PetSource::tick): where the
// chat, the alert and the kept-off sources are. The settings it reads are the source's, not a pet's.
void scan_scene(PetSource &ps)
{
	const Pet &L = *ps.lead_;
	SceneView v;
	v.auto_chat = ps.scene.auto_chat;
	v.auto_alert = ps.scene.auto_alert;
	Home home = home_of(ps.source);
	if (obs_scene_t *scene = home.scene) {
		// Its box moved or resized: what's around it is mapped into its own pixels, so it still
		// walks under the chat and sits on it (along the bottom of its box)
		matrix4 inv;
		v.moved = home.self && box_moved(scene, home.self, &inv);
		const matrix4 *to_pet = v.moved ? &inv : nullptr;
		if (L.perch_name.empty() || L.alert_name.empty()) {
			ps.auto_t -= 0.25;
			if (ps.auto_t <= 0) {
				ps.auto_t = 2;
				auto_find(ps, scene);
				v.auto_chat = ps.scene.auto_chat;
				v.auto_alert = ps.scene.auto_alert;
			}
		}
		auto find = [&](const std::string &name, Box &out) -> obs_sceneitem_t * {
			if (name.empty() || name == NO_SOURCE)
				return nullptr;
			obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, name.c_str());
			if (!item || !obs_sceneitem_visible(item))
				return nullptr;
			out = rect_of(scene, item, to_pet);
			return item;
		};
		std::string chat_name = L.perch_name.empty() ? v.auto_chat : L.perch_name;
		obs_sceneitem_t *chat_item = find(chat_name, v.chat);
		v.have_chat = chat_item != nullptr;
		if (v.have_chat) {
			v.chat_name = chat_name;
			obs_source_t *cs = obs_sceneitem_get_source(chat_item);
			if (ChatSource *c = ChatSource::from(cs)) {
				v.kind = ChatKind::Ours;
				v.have_seq = c->layout(v.top, v.page_h, v.seq);
				v.hide = c->hidden();
			} else {
				v.kind = ChatKind::Other;
				v.chat_h = cs ? obs_source_get_height(cs) : 0;
			}
		}
		v.have_alert = find(L.alert_name.empty() ? v.auto_alert : L.alert_name, v.alert) != nullptr;
		for (auto &n : L.avoid_names) {
			Box r;
			if (find(n, r)) {
				v.avoid.push_back(r);
				if (L.sit_names.count(n))
					v.sit.push_back({n, r});
			}
		}
	}
	obs_source_release(home.scene_src);
	ps.scene = std::move(v);
	ps.scene_n++;
}

void scan(Pet &d)
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		d.W = ovi.base_width;
		d.H = ovi.base_height;
	}
	d.floorY = d.H - d.floor_px;
	d.half = d.pack->half * d.px;

	const SceneView &v = d.owner->scene;
	d.scene_seen = d.owner->scene_n;
	bool have_chat = v.have_chat, have_alert = v.have_alert, moved = v.moved;
	Box chat = v.chat, alert = v.alert;
	std::vector<Box> avoid = v.avoid;
	ChatKind kind = v.kind;
	std::string chat_name = v.chat_name;
	std::string hide = v.hide;
	long long top = v.top, page_h = v.page_h;
	if (kind == ChatKind::Ours && v.have_seq)
		d.rep_seq = v.seq;
	if (kind == ChatKind::Other) {
		// its pixels say where the top message is (read_chat_pixels, in render)
		uint32_t sh = v.chat_h;
		if (d.perch_box && d.px_src == chat_name && d.px_top >= 0 && sh > 0) {
			top = (long long)std::lround(d.px_top * sh);
			page_h = sh;
			// it grew: a new message came in (the scan only sees it rise)
			if (d.px_prev >= 0 && d.px_top - d.px_prev > 6.0 / sh)
				d.rep_seq++;
			d.px_prev = d.px_top;
		}
	}
	if (moved != d.moved) {
		d.moved = moved;
		if (d.slot == 0) {
			{
				std::lock_guard<std::mutex> g(d.owner->mu_);
				d.owner->ui.moved = moved;
			}
			d.owner->status_dirty_ = true;   // (the properties say so, and offer to fix it)
		}
	}
	if (hide != d.chat_hide) {
		d.chat_hide = hide;
		merge_ignore(d);
		d.owner->profile_changed();
	}
	if (have_chat)
		avoid.push_back(chat);
	d.alert_x = have_alert ? (alert.x0 + alert.x1) / 2 : d.W / 2;

	// Floor: the canvas width minus every kept-off source that reaches down to where it walks.
	double lo = d.half + 6, hi = d.W - d.half - 6;
	std::vector<Seg> cuts;
	for (auto &r : avoid)
		if (r.y1 > d.floorY - 20 * d.S && r.y0 < d.floorY)
			cuts.push_back({r.x0 - d.half, r.x1 + d.half});
	std::sort(cuts.begin(), cuts.end(), [](const Seg &a, const Seg &b) { return a.a < b.a; });
	std::vector<Seg> segs;
	double at = lo;
	for (auto &c : cuts) {
		if (c.a > at)
			segs.push_back({at, std::min(c.a, hi)});
		at = std::max(at, c.b);
	}
	if (at < hi)
		segs.push_back({at, hi});
	d.segs.clear();
	for (auto &s : segs)
		if (s.b - s.a > 4 * d.S)
			d.segs.push_back(s);

	// Perch: the top of the messages, from an OBSPETS Chat or (only if ticked) the pixels of another
	// chat. Not when that's near the top of the canvas (nowhere to stand).
	d.has_chat = have_chat;
	d.chat = chat;
	d.chat_kind = have_chat ? kind : ChatKind::None;
	d.chat_name = have_chat ? chat_name : "";
	d.can_yank = have_chat && (kind != ChatKind::Other || d.other_yank);
	d.want_px = have_chat && kind == ChatKind::Other && d.perch_box;
	if (!d.want_px || d.px_src != chat_name) {
		d.px_top = d.px_prev = -1;
		d.px_staged = false;
		d.px_src = d.want_px ? chat_name : "";
	}
	double h = chat.y1 - chat.y0;
	double py = page_h > 0 ? std::floor(chat.y0 + h * (1 - (double)top / (double)page_h) + 3 + 0.5) : 0;
	if (have_chat && page_h > 0 && top > 4 && py > d.H * 0.12 && (kind == ChatKind::Ours || d.perch_box)) {
		double w = chat.x1 - chat.x0;
		d.perch_y = py;
		d.perch_x0 = chat.x0 + std::max(d.half, 0.04 * w);
		// an OBSPETS Chat's text is on its left; where another chat's is isn't known: anywhere along it
		d.perch_x1 = kind == ChatKind::Ours ? std::max(d.perch_x0, chat.x0 + 0.62 * w)
						    : std::max(d.perch_x0, chat.x1 - std::max(d.half, 0.04 * w));
		d.has_perch = true;
	} else {
		d.has_perch = false;
	}

	// Ledges: the tops of the kept-off sources it may sit on. Not too near the top of the canvas
	// (nowhere to stand), and wide enough to stand on.
	d.ledges.clear();
	for (auto &[name, r] : v.sit) {
		Pet::Ledge l{name, r, std::floor(r.y0 + 0.5), r.x0 + d.half * 0.8, r.x1 - d.half * 0.8};
		if (l.y > d.H * 0.15 && l.y < d.floorY - 12 * d.S && l.x1 - l.x0 > 4 * d.S)
			d.ledges.push_back(l);
	}
	d.has_ledge = false;
	for (auto &l : d.ledges)
		if (l.name == d.ledge) {
			d.on_ledge = l;
			d.has_ledge = true;
		}

	// where it is and whether it sleeps, for the mood engine
	std::string st = std::string(d.anim == "sleep" ? "asleep" : "awake") +
			 (d.on == On::Perch   ? ", sitting on top of the chat"
			  : d.on == On::Ledge ? ", sitting on top of something on screen"
			  : d.has_chat        ? ", on the floor next to the chat"
					      : ", on the floor (the chat isn't on screen)");
	if (st != d.state_sent) {
		d.state_sent = st;
		d.mood.state = st;
		d.owner->profile_changed();
	}
}

static const Seg *seg_at(Pet &d, double x)
{
	for (auto &s : d.segs)
		if (x >= s.a - 1 && x <= s.b + 1)
			return &s;
	return nullptr;
}

static double nearest_floor_x(Pet &d, double x)
{
	double best = x, bd = 1e18;
	for (auto &s : d.segs) {
		double cx = clampd(x, s.a, s.b);
		if (std::abs(cx - x) < bd) {
			best = cx;
			bd = std::abs(cx - x);
		}
	}
	return best;
}

// ------------------------------------------------------------------ moving

void play(Pet &d, const std::string &a)
{
	d.anim = a;
	d.at = 0;
	d.fixed.clear();
	d.on_frame.clear();
}

void after(Pet &d, double secs, Next fn)
{
	d.has_timer = true;
	d.timer = secs;
	d.cb = std::move(fn);
}

static double anim_len(Pet &d, const std::string &a, double loops = 1)
{
	const Anim &an = d.pack->anim(a);
	return an.n() / an.fps * loops;
}

static void idle_then_decide(Pet &d)
{
	play(d, "idle");
	after(d, rnd(1.5, 3.5), decide);
}

static void walk_to(Pet &d, double tx, const std::string &kind, Next done)
{
	tx = fl(tx);
	if (std::abs(tx - d.x) < 3 * d.S) {
		done(d);
		return;
	}
	d.face = tx > d.x ? 1 : -1;
	play(d, kind);
	d.mode = Mode::Move;
	d.tx = tx;
	d.done = std::move(done);
	d.has_timer = false;
	d.speed = (kind == "run" ? d.pack->run : d.pack->walk) * d.px;
}

// A jump along a parabola to (tx, ty). `peak` < 0: enough to clear the destination.
static void leap(Pet &d, double tx, double ty, On dest, double peak, Next done)
{
	double rise = std::max(0.0, d.y - ty);
	d.mode = Mode::Air;
	d.dest = dest;
	d.done = std::move(done);
	d.has_timer = false;
	d.cb = nullptr;
	d.ax0 = d.x;
	d.ay0 = d.y;
	d.ax1 = fl(tx);
	d.ay1 = ty;
	d.air = 0;
	d.peak = peak >= 0 ? peak : rise * 0.6 + 14 * d.S;
	d.T = 0.4 + (rise + std::abs(ty - d.y)) / 3200 + std::abs(tx - d.x) / 2600 + d.peak / 2400;
	d.prep = 0.12;
	if (std::abs(tx - d.x) > 1)
		d.face = tx > d.x ? 1 : -1;
	d.fixed = "crouch";
	d.land_sfx = "land";
}

static void land(Pet &d)
{
	d.mode = Mode::Ground;
	d.x = d.ax1;
	d.y = d.ay1;
	d.on = d.dest;
	if (!d.land_sfx.empty())
		sound(d, d.land_sfx);
	d.sq = -0.22;
	d.fixed = "land";
	Next done = std::move(d.done);
	d.done = nullptr;
	after(d, 0.22, [done](Pet &dd) {
		dd.fixed.clear();
		if (done)
			done(dd);
		else
			idle_then_decide(dd);
	});
}

static void startled(Pet &d)
{
	play(d, "look");
	after(d, rnd(1.2, 2.2), decide);
}

// ------------------------------------------------------------------ deciding

static void range(Pet &d, double &a, double &b)
{
	if (d.on == On::Perch && d.has_perch) {
		a = d.perch_x0;
		b = d.perch_x1;
		return;
	}
	if (d.on == On::Ledge && d.has_ledge) {
		a = d.on_ledge.x0;
		b = d.on_ledge.x1;
		return;
	}
	if (const Seg *s = seg_at(d, d.x)) {
		a = s->a;
		b = s->b;
		return;
	}
	a = b = d.x;
}

struct Plan {
	double lx = 0, px = 0, fx = 0;
};

static bool visit_plan(Pet &d, Plan &out)
{
	if (!d.has_chat || !d.has_perch || d.on != On::Floor)
		return false;
	const Seg *s = seg_at(d, d.x);
	if (!s)
		return false;
	double rx = d.chat.x1 + d.half + 2 * d.S;
	if (rx >= s->a - 1 && rx <= s->b + 1) {
		out = {rx, d.perch_x1, 0};
		return true;
	}
	double lx = d.chat.x0 - d.half - 2 * d.S;
	if (lx >= s->a - 1 && lx <= s->b + 1) {
		out = {lx, d.perch_x0, 0};
		return true;
	}
	return false;
}

static bool leave_plan(Pet &d, Plan &out)
{
	if (!d.has_chat || !d.has_perch)
		return false;
	std::vector<Plan> opts;
	double rx = d.chat.x1 + d.half + 2 * d.S;
	if (seg_at(d, rx))
		opts.push_back({0, d.perch_x1, rx});
	double lx = d.chat.x0 - d.half - 2 * d.S;
	if (seg_at(d, lx))
		opts.push_back({0, d.perch_x0, lx});
	if (opts.empty())
		return false;
	std::sort(opts.begin(), opts.end(),
		  [&](const Plan &a, const Plan &b) { return std::abs(a.px - d.x) < std::abs(b.px - d.x); });
	out = opts[0];
	return true;
}

static void act_idle(Pet &d)
{
	play(d, "idle");
	after(d, rnd(4, 9), decide);
}
static void act_look(Pet &d)
{
	play(d, "look");
	after(d, rnd(2.5, 5), decide);
}
static void act_clean(Pet &d)
{
	if (d.pack && d.pack->has("stretch") && rnd(0, 1) < 0.4) {
		play(d, "stretch");
		after(d, anim_len(d, "stretch", 1), idle_then_decide);
		return;
	}
	play(d, "clean");
	after(d, anim_len(d, "clean", 3), [](Pet &dd) {
		play(dd, "clean2");
		after(dd, anim_len(dd, "clean2", 3), decide);
	});
}
static void act_sleep(Pet &d)
{
	play(d, "sleep");
	d.nap_at = d.now;
	d.z_t = 0.8;
	after(d, rnd(18, 40), [](Pet &dd) {
		play(dd, "look");
		after(dd, 2.5, decide);
	});
}
static void act_paw(Pet &d)
{
	play(d, "paw");
	after(d, anim_len(d, "paw", 2), idle_then_decide);
}
static void act_hop(Pet &d) { leap(d, d.x, d.y, d.on, 12 * d.S, nullptr); }
static void act_walk(Pet &d, const std::string &kind, bool far)
{
	double a, b;
	range(d, a, b);
	double dist = rnd(far ? 0.2 : 0.06, far ? 0.4 : 0.3) * d.W;
	double tx = d.x + (rnd(0, 1) < 0.5 ? -dist : dist);
	if (tx < a || tx > b)
		tx = d.x - (tx - d.x);
	walk_to(d, clampd(tx, a, b), kind, idle_then_decide);
}
void act_visit(Pet &d)
{
	Plan plan;
	if (!visit_plan(d, plan)) {
		act_idle(d);
		return;
	}
	walk_to(d, plan.lx, "walk", [plan](Pet &dd) {
		if (!dd.has_perch) {
			idle_then_decide(dd);
			return;
		}
		leap(dd, clampd(plan.px, dd.perch_x0, dd.perch_x1), dd.perch_y, On::Perch, -1, nullptr);
	});
}
static void act_leave(Pet &d)
{
	Plan plan;
	if (!leave_plan(d, plan)) {
		act_idle(d);
		return;
	}
	walk_to(d, plan.px, "walk", [plan](Pet &dd) { leap(dd, plan.fx, dd.floorY, On::Floor, -1, nullptr); });
}

// Up onto a ledge: from the floor beside it (a source that reaches the floor cuts it there), or
// from under it. One it can get to from where it stands, picked at random.
struct Climb {
	double lx = 0, px = 0;   // where it jumps from, and where it lands
	Pet::Ledge l;
};
static bool climb_plan(Pet &d, Climb &out)
{
	if (d.ledges.empty() || d.on != On::Floor)
		return false;
	const Seg *here = seg_at(d, d.x);
	if (!here)
		return false;
	std::vector<Climb> opts;
	for (auto &l : d.ledges) {
		double sides[] = {l.box.x0 - d.half - 2 * d.S, l.box.x1 + d.half + 2 * d.S};
		for (int i = 0; i < 2; i++)
			if (seg_at(d, sides[i]) == here)
				opts.push_back({sides[i], i ? l.x1 : l.x0, l});
		// straight up from under it, when it doesn't reach down to the floor
		double a = std::max(l.x0, here->a), b = std::min(l.x1, here->b);
		if (a < b) {
			double ux = clampd(d.x, a, b);
			opts.push_back({ux, ux, l});
		}
	}
	if (opts.empty())
		return false;
	out = opts[(size_t)rnd(0, (double)opts.size() - 0.001)];
	return true;
}
static void act_climb(Pet &d)
{
	Climb c;
	if (!climb_plan(d, c)) {
		act_idle(d);
		return;
	}
	walk_to(d, c.lx, "walk", [c](Pet &dd) {
		for (auto &l : dd.ledges)
			if (l.name == c.l.name) {
				dd.ledge = l.name;
				dd.on_ledge = l;
				dd.has_ledge = true;
				leap(dd, clampd(c.px, l.x0, l.x1), l.y, On::Ledge, -1, nullptr);
				return;
			}
		idle_then_decide(dd);   // (it went while it walked there)
	});
}
// Down off a ledge: to the floor beside it, on the nearer side it can land on.
static void off_ledge(Pet &d, Next n)
{
	const Box &b = d.on_ledge.box;
	double sides[] = {b.x0 - d.half - 2 * d.S, b.x1 + d.half + 2 * d.S};
	if (std::abs(sides[1] - d.x) < std::abs(sides[0] - d.x))
		std::swap(sides[0], sides[1]);
	double fx = seg_at(d, sides[0]) ? sides[0] : seg_at(d, sides[1]) ? sides[1] : nearest_floor_x(d, d.x);
	if (!d.has_ledge)
		fx = nearest_floor_x(d, d.x);
	leap(d, fx, d.floorY, On::Floor, 6 * d.S, std::move(n));
	d.prep = 0.04;
}
static void act_down(Pet &d)
{
	double edge = std::abs(d.on_ledge.x0 - d.x) < std::abs(d.on_ledge.x1 - d.x) ? d.on_ledge.x0 : d.on_ledge.x1;
	walk_to(d, edge, "walk", [](Pet &dd) { off_ledge(dd, nullptr); });
}

// chat quiet for minutes (or no chat) -> naps a lot; busy -> hardly
static double sleepiness(Pet &d)
{
	double z = 1;
	if (d.heat > 5)
		z = 0.5;
	else if (d.heat > 2)
		z = 0.8;
	else if (d.quiet > 300)
		z = 3.5;
	else if (d.quiet > 120)
		z = 1.8;
	return z * d.naps;
}

void decide(Pet &d)
{
	d.busy = false;
	d.act.clear();   // (its own doings from here on)
	double z = sleepiness(d), e = d.energy;
	struct Act {
		double w;
		int id;
	};
	enum { IDLE, LOOK, CLEAN, SLEEP, WALK, RUN, PAW, HOP, LEAVE, VISIT, CLIMB, DOWN };
	std::vector<Act> acts;
	if (d.on == On::Ledge && d.has_ledge) {
		// a good spot for a nap
		acts = {{30, IDLE}, {16, LOOK}, {12, CLEAN}, {20 * z, SLEEP}, {8 * e, WALK}, {4 * e, PAW}, {11, DOWN}};
	} else if (d.on == On::Perch && d.has_perch) {
		acts = {{30, IDLE}, {18, LOOK}, {12, CLEAN}, {14 * z, SLEEP}, {12 * e, WALK}, {5 * e, PAW}, {9, LEAVE}};
	} else {
		acts = {{26, IDLE}, {16, LOOK}, {12, CLEAN}, {22 * e, WALK}, {4 * e, RUN}, {9 * z, SLEEP}, {6 * e, PAW}, {5 * e, HOP}};
		Plan p;
		if (visit_plan(d, p) && d.now >= d.stay_down)
			acts.push_back({40 * d.visit, VISIT});
		Climb c;
		if (climb_plan(d, c) && d.now >= d.stay_down)
			acts.push_back({14, CLIMB});
	}
	double total = 0;
	for (auto &a : acts)
		total += a.w;
	double r = rnd(0, 1) * total;
	Act pick = acts[0];
	for (auto &a : acts) {
		r -= a.w;
		if (r <= 0) {
			pick = a;
			break;
		}
	}
	if (pick.id == d.last_act && pick.id != IDLE)
		pick = acts[0];   // no trick twice in a row
	d.last_act = pick.id;
	switch (pick.id) {
	case IDLE: act_idle(d); break;
	case LOOK: act_look(d); break;
	case CLEAN: act_clean(d); break;
	case SLEEP: act_sleep(d); break;
	case WALK: act_walk(d, "walk", false); break;
	case RUN: act_walk(d, "run", true); break;
	case PAW: act_paw(d); break;
	case HOP: act_hop(d); break;
	case LEAVE: act_leave(d); break;
	case VISIT: act_visit(d); break;
	case CLIMB: act_climb(d); break;
	case DOWN: act_down(d); break;
	}
}

// ------------------------------------------------------------------ effects

static Part &spawn(Pet &d, Part p)
{
	d.parts.push_back(std::move(p));
	return d.parts.back();
}

// one mark over its head at a time
static void mark(Pet &d, const char *glyph, const char *c, double life, bool big = false, bool throb = false, bool side = false)
{
	d.parts.erase(std::remove_if(d.parts.begin(), d.parts.end(), [](const Part &p) { return p.k == Part::K::Mark; }),
		      d.parts.end());
	Part p;
	p.k = Part::K::Mark;
	p.g = glyph;
	p.c = c;
	p.life = life;
	p.big = big;
	p.throb = throb;
	p.side = side;
	spawn(d, p);
}

static void bang(Pet &d, bool big = false) { mark(d, "bang", "bang", big ? 1.6 : 1.2, big); }

static void hearts(Pet &d, int n, const char *glyph = nullptr, const char *c = nullptr)
{
	int alive = 0;
	for (auto &p : d.parts)
		if (p.k == Part::K::Heart)
			alive++;
	for (int i = 0; i < std::min(n, 16 - alive); i++) {
		Part p;
		p.k = Part::K::Heart;
		p.t = -i * 0.16;
		p.life = 2.4;
		p.ph = rnd(0, 6.28);
		p.g = glyph;
		p.c = c ? c : "heart";
		p.x = d.x + rnd(-6, 6) * d.S;
		p.y = d.y - (d.pack->top - 2) * d.px;
		p.vy = rnd(6, 9) * d.S;
		p.vx = rnd(-2.5, 2.5) * d.S;
		spawn(d, p);
	}
}

static void sparks(Pet &d)
{
	double px = d.x + d.face * d.pack->paw_x * d.px, py = d.y - d.pack->paw_y * d.px;
	for (int i = 0; i < 4; i++) {
		double a = rnd(-1.3, 0.4), v = rnd(22, 40) * d.S;
		Part p;
		p.k = Part::K::Spark;
		p.life = rnd(0.45, 0.7);
		p.x = px;
		p.y = py;
		p.vx = std::cos(a) * v * d.face;
		p.vy = std::sin(a) * v;
		spawn(d, p);
	}
}

static void face_alert(Pet &d)
{
	if (std::abs(d.alert_x - d.x) > 2 * d.S)
		d.face = d.alert_x > d.x ? 1 : -1;
}

void interrupt(Pet &d)
{
	if (d.mode == Mode::Move)
		d.mode = Mode::Ground;
	d.done = nullptr;
	d.has_timer = false;
	d.cb = nullptr;
	d.fixed.clear();
	d.on_frame.clear();
	d.wiggling = false;
}

// ------------------------------------------------------------------ chains of steps

struct ChainState {
	std::vector<Step> steps;
	size_t i = 0;
};

static void chain_next(Pet &d, std::shared_ptr<ChainState> c)
{
	if (c->i < c->steps.size()) {
		Step s = c->steps[c->i++];
		s(d, [c](Pet &dd) { chain_next(dd, c); });
	} else {
		d.busy = false;
		idle_then_decide(d);
	}
}

static void chain(Pet &d, std::vector<Step> steps)
{
	d.busy = true;
	auto c = std::make_shared<ChainState>();
	c->steps = std::move(steps);
	chain_next(d, c);
}

static Step hold(const char *a, double secs)
{
	return [a, secs](Pet &d, Next n) {
		play(d, a);
		after(d, secs, n);
	};
}
static Step loops(const char *a, double k)
{
	return [a, k](Pet &d, Next n) {
		play(d, a);
		after(d, anim_len(d, a, k), n);
	};
}
static Step fx(std::function<void(Pet &)> f)
{
	return [f](Pet &d, Next n) {
		f(d);
		n(d);
	};
}
static Step say(const char *key)
{
	return [key](Pet &d, Next n) {
		sound(d, key, d.voice == "mood");
		n(d);
	};
}
// a voice sound with its call (a bark, a meow) when it has one: only while the sound plays, so it
// never calls out silently (the chat-mood sound gap can hold the sound back)
static Step call(const char *key)
{
	return [key](Pet &d, Next n) {
		if (sound(d, key, d.voice == "mood") && d.pack->has("call")) {
			play(d, "call");
			after(d, anim_len(d, "call", 1), n);
		} else {
			n(d);
		}
	};
}
static Step hop(double peak, int nhearts)
{
	return [peak, nhearts](Pet &d, Next n) {
		leap(d, d.x, d.y, d.on, peak * d.S, [n, nhearts](Pet &dd) {
			if (nhearts > 0)
				hearts(dd, nhearts);
			n(dd);
		});
		d.prep = 0.07;
	};
}
static Step paw(double k, bool spark = false)
{
	return [k, spark](Pet &d, Next n) {
		play(d, "paw");
		if (spark) {
			const Anim &a = d.pack->anim("paw");
			d.on_frame = {{a.whoosh, [](Pet &dd) { sound(dd, "swipe"); }}, {a.hit, sparks}};
		}
		after(d, anim_len(d, "paw", k), n);
	};
}
static void get_down(Pet &d, Next n)
{
	if (d.on != On::Perch) {
		n(d);
		return;
	}
	Plan plan;
	bool ok = leave_plan(d, plan);
	leap(d, ok ? plan.fx : nearest_floor_x(d, d.x), d.floorY, On::Floor, 6 * d.S, n);
	d.prep = 0.04;
}
static void lap(Pet &d, Next n)
{
	double a, b;
	range(d, a, b);
	double far = (d.x - a > b - d.x) ? a : b;
	walk_to(d, clampd(far, d.x - d.W / 3, d.x + d.W / 3), "run", n);
}

// ------------------------------------------------------------------ alerts

bool run_alert(Pet &d, const std::string &kind, int n)
{
	if (kind == "follow") {
		bool woke = d.anim == "sleep";
		bang(d);
		face_alert(d);
		d.bobbing = true, d.bob = 0;
		chain(d, {call("notice"), hold("idle", woke ? 0.9 : 0.5), hold("look", 2.4), fx(face_alert), hold("idle", 1.4)});
	} else if (kind == "sub") {
		bang(d);
		face_alert(d);
		d.bobbing = true, d.bob = 0;
		chain(d, {say("cheer"), hold("idle", 0.45), hop(15, 3), paw(1), loops("clean", 2)});
	} else if (kind == "gift") {
		n = std::max(1, n);
		int hops = (int)clampd(1 + std::floor(std::log((double)n) / std::log(2.0) + 1e-9), 1, 4);
		int per = (int)clampd(std::ceil(std::min(n + 1, 14) / (double)hops), 1, 5);
		bang(d);
		face_alert(d);
		d.bobbing = true, d.bob = 0;
		std::vector<Step> steps = {say("cheer"), hold("idle", 0.4)};
		for (int i = 1; i <= hops; i++)
			steps.push_back(hop(i == hops ? 17 : 11, per));
		steps.push_back(paw(1));
		steps.push_back(loops("clean", 2));
		chain(d, steps);
	} else if (kind == "bits") {
		face_alert(d);
		d.bobbing = true, d.bob = 0;
		int swats = n >= 1000 ? 3 : n >= 100 ? 2 : 1;
		std::vector<Step> steps = {say("notice"), hold("idle", 0.3), paw(swats, true)};
		if (n >= 500)
			steps.push_back(hop(13, 0));
		steps.push_back(fx(face_alert));
		steps.push_back(hold("idle", 1));
		chain(d, steps);
	} else if (kind == "raid") {
		bang(d, true);
		face_alert(d);
		d.sq = 0.3;
		std::vector<Step> steps = {call("notice"), get_down};
		for (int i = 0; i < (n >= 50 ? 3 : n >= 10 ? 2 : 1); i++)
			steps.push_back(lap);
		steps.push_back([](Pet &dd, Next nx) {
			double a, b;
			range(dd, a, b);
			walk_to(dd, clampd(dd.alert_x, a, b), "run", nx);
		});
		steps.push_back(fx(face_alert));
		steps.push_back(hold("look", 1.8));
		steps.push_back(say("cheer"));
		steps.push_back(hop(14, 4));
		steps.push_back(paw(1));
		chain(d, steps);
	} else {
		return false;
	}
	return true;
}

// ------------------------------------------------------------------ moods

static double chat_x(Pet &d) { return d.has_chat ? (d.chat.x0 + d.chat.x1) / 2 : d.alert_x; }
static void face_chat(Pet &d)
{
	if (d.on != On::Perch && std::abs(chat_x(d) - d.x) > 2 * d.S)
		d.face = chat_x(d) > d.x ? 1 : -1;
}
static void face_away(Pet &d)
{
	if (d.on == On::Perch)
		d.face = -d.face;
	else
		d.face = chat_x(d) > d.x ? -1 : 1;
}
static void wake(Pet &d, Next n)
{
	if (d.anim == "sleep") {
		play(d, "look");
		after(d, 0.9, n);
	} else {
		n(d);
	}
}
static Step wander(double frac, int dir, const char *kind = "walk")
{
	return [frac, dir, kind](Pet &d, Next n) {
		double a, b;
		range(d, a, b);
		int s = (chat_x(d) > d.x ? 1 : -1) * dir;
		if (d.on == On::Perch)
			s = d.face * dir;
		walk_to(d, clampd(d.x + s * frac * d.W, a, b), kind, n);
	};
}
// the floor spot right beside the chat on the side it can reach (the nearer one)
static bool chat_side(Pet &d, double &out)
{
	if (!d.has_chat || d.on != On::Floor)
		return false;
	const Seg *s = seg_at(d, d.x);
	if (!s)
		return false;
	bool got = false;
	for (double x : {d.chat.x1 + d.half + d.S, d.chat.x0 - d.half - d.S})
		if (x >= s->a - 1 && x <= s->b + 1 && (!got || std::abs(x - d.x) < std::abs(out - d.x))) {
			out = x;
			got = true;
		}
	return got;
}
static Step to_chat(const char *kind0)
{
	return [kind0](Pet &d, Next n) {
		double x;
		if (!chat_side(d, x)) {
			face_chat(d);
			n(d);
			return;
		}
		std::string kind = kind0;
		if (std::abs(x - d.x) > 0.2 * d.W)
			kind = "run";   // from far away it runs, so chat doesn't wait long
		walk_to(d, x, kind, [n](Pet &dd) {
			face_chat(dd);
			n(dd);
		});
	};
}
static Step crouch(double secs)
{
	return [secs](Pet &d, Next n) {
		d.fixed = "crouch";
		d.wiggling = true;
		d.wiggle = 0;
		after(d, secs, [n](Pet &dd) {
			dd.fixed.clear();
			dd.wiggling = false;
			n(dd);
		});
	};
}
static void pounce_up(Pet &d, Next n)
{
	double cs;
	if (d.on != On::Floor || !d.has_perch || d.stay_down > d.now || !chat_side(d, cs) || std::abs(cs - d.x) > 4 * d.S) {
		n(d);
		return;
	}
	double px = std::abs(d.perch_x0 - d.x) < std::abs(d.perch_x1 - d.x) ? d.perch_x0 : d.perch_x1;
	leap(d, px, d.perch_y, On::Perch, -1, n);
	d.prep = 0.05;
	d.sq = -0.2;
	d.land_sfx = "pounce";
}
static Step maybe(double chance, Step step)
{
	return [chance, step](Pet &d, Next n) {
		if (rnd(0, 1) < chance)
			step(d, n);
		else
			n(d);
	};
}
static void get_down_if_up(Pet &d, Next n)
{
	if (d.on == On::Perch)
		get_down(d, n);
	else
		n(d);
}
static void angry_mark(Pet &d) { mark(d, "anger", "anger", 3.4, true, true, true); }
static void pick(Pet &d, const std::vector<std::function<std::vector<Step>()>> &versions)
{
	chain(d, versions[rnd_int(0, (int)versions.size() - 1)]());
}
static Step nap()
{
	return [](Pet &d, Next) {
		d.busy = false;
		act_sleep(d);
	};
}
static Step flip_face()
{
	return fx([](Pet &d) { d.face = -d.face; });
}
static Step dots_mark(double life, bool away = false)
{
	return fx([life, away](Pet &d) {
		if (away)
			face_away(d);
		mark(d, "dots", "dots", life);
	});
}
static Step what_mark(double life)
{
	return fx([life](Pet &d) { mark(d, "what", "mark", life); });
}

static void mood_angry(Pet &d)
{
	d.stay_down = d.now + rnd(40, 70);
	angry_mark(d);
	face_chat(d);
	d.sq = 0.25;
	pick(d, {
			// puffs up, gets down, stomps off, glares back, turns its back
			[] {
				return std::vector<Step>{wake, say("angry"), loops("angry", 2), get_down_if_up,
							 wander(rnd(0.08, 0.16), -1), fx(face_chat), hold("idle", 1.6),
							 fx(face_away), loops("stretch", 1)};
			},
			// charges the chat and swipes at it, then turns its back on it
			[] {
				return std::vector<Step>{wake, to_chat("run"), fx(angry_mark), say("angry"), loops("angry", 1),
							 paw(2, true), get_down_if_up, fx(face_away), wander(rnd(0.05, 0.1), -1),
							 fx(face_chat), hold("look", 1.2)};
			},
			// a huff: turns its back right where it is, glances back, still cross
			[] {
				return std::vector<Step>{wake, say("angry"), loops("angry", 1), fx(face_away), hold("idle", 2.2),
							 fx([](Pet &dd) {
								 dd.face = -dd.face;
								 angry_mark(dd);
							 }),
							 hold("look", 0.8), fx(face_away), loops("stretch", 1), get_down_if_up};
			},
		});
}

static void mood_hop_on(Pet &d)
{
	d.stay_down = 0;
	if (d.on == On::Perch) {
		chain(d, {wake, hop(8, 1), hold("idle", 1)});
		return;
	}
	Plan p;
	double cs;
	if (!visit_plan(d, p) && !chat_side(d, cs)) {
		mark(d, "what", "mark", 1.8);
		chain(d, {wake, hold("look", 2)});
		return;
	}
	pick(d, {
			[] {
				return std::vector<Step>{wake, call("notice"), [](Pet &dd, Next) {
								 dd.busy = false;
								 act_visit(dd);
							 }};
			},
			// runs over, wiggles and pounces up
			[] {
				return std::vector<Step>{wake, say("question"), to_chat("run"), crouch(rnd(0.4, 0.7)), pounce_up,
							 hold("idle", 0.8)};
			},
		});
}

static void mood_hop_off(Pet &d)
{
	d.stay_down = d.now + rnd(45, 90);
	if (d.on != On::Perch) {
		face_chat(d);
		chain(d, {wake, hold("look", 1.6)});
		return;
	}
	chain(d, {wake, hold("idle", 0.4), get_down, fx(face_chat), hold("look", 1.6)});
}

static void mood_love(Pet &d)
{
	sound(d, "pet", true);
	hearts(d, 4);
	d.bobbing = true, d.bob = 0;
	if (d.anim == "sleep")
		return;   // purrs in its sleep
	pick(d, {
			[] {
				return std::vector<Step>{hold("idle", 0.5), loops("clean", 2), fx([](Pet &dd) { hearts(dd, 2); }),
							 loops("clean2", 1), hold("idle", 1)};
			},
			// trots up to the chat, looks up at it, more hearts and a little hop
			[] {
				return std::vector<Step>{to_chat("walk"), hold("look", 0.7), fx([](Pet &dd) {
								 hearts(dd, 4);
								 dd.bobbing = true, dd.bob = 0;
							 }),
							 hop(7, 0), loops("clean", 1), hold("idle", 0.8)};
			},
			// happy bobbing, a heart with each
			[] {
				std::vector<Step> s = {fx(face_chat)};
				for (int i = 0; i < 4; i++) {
					s.push_back(fx([](Pet &dd) {
						dd.bobbing = true, dd.bob = 0;
						hearts(dd, 1);
					}));
					s.push_back(hold("idle", 0.38));
				}
				s.push_back(loops("clean2", 1));
				return s;
			},
		});
}

static void mood_happy(Pet &d)
{
	face_chat(d);
	pick(d, {
			[] {
				return std::vector<Step>{wake, say("question"), hold("idle", 0.3), hop(13, 3), hold("idle", 0.4),
							 hop(8, 0), hold("idle", 1)};
			},
			[] { return std::vector<Step>{wake, to_chat("run"), call("notice"), hop(13, 3), hold("idle", 1)}; },
			[] { return std::vector<Step>{wake, call("notice"), lap, fx(face_chat), hop(12, 2), hold("idle", 1)}; },
		});
}

static void mood_curious(Pet &d)
{
	face_chat(d);
	pick(d, {
			[] {
				return std::vector<Step>{wake, what_mark(2.2), say("question"), hold("look", 1.6),
							 wander(rnd(0.03, 0.07), 1), fx(face_chat), hold("look", 1.6)};
			},
			// creeps right up to the chat and gives it a careful tap
			[] {
				return std::vector<Step>{wake, what_mark(2.6), to_chat("walk"), say("question"), hold("look", 1.2),
							 paw(1), hold("look", 1.2)};
			},
		});
}

static void mood_sleepy(Pet &d)
{
	if (d.anim == "sleep")
		return;
	pick(d, {
			[] { return std::vector<Step>{hold("idle", 0.6), loops("stretch", 1), nap()}; },
			// curls up right next to the chat
			[] { return std::vector<Step>{to_chat("walk"), loops("stretch", 1), nap()}; },
		});
}

static void mood_zoomies(Pet &d)
{
	bang(d);
	d.sq = 0.2;
	pick(d, {
			[] { return std::vector<Step>{wake, say("notice"), hold("idle", 0.25), lap, lap, lap, hop(12, 0), hold("idle", 1.2)}; },
			// laps, then straight at the chat and up onto it
			[] {
				return std::vector<Step>{wake, say("notice"), lap, lap, to_chat("run"), crouch(0.3), pounce_up,
							 hold("idle", 1)};
			},
		});
}

static void mood_play(Pet &d)
{
	face_chat(d);
	pick(d, {
			[] {
				return std::vector<Step>{wake, hold("idle", 0.3), paw(2, true), hop(9, 0), paw(1, true), hold("idle", 0.8)};
			},
			[] {
				return std::vector<Step>{wake, to_chat("run"), crouch(rnd(0.5, 0.8)), paw(2, true), hop(9, 0),
							 paw(1, true), hold("idle", 0.8)};
			},
		});
}

static void mood_sulk(Pet &d)
{
	face_away(d);
	pick(d, {
			[] {
				return std::vector<Step>{wake, dots_mark(2.6, true), hold("idle", 2.6), flip_face(), hold("look", 0.7),
							 flip_face(), loops("stretch", 1), hold("idle", 1)};
			},
			// walks off a little way first, then sits with its back to chat
			[] {
				return std::vector<Step>{wake, fx(face_away), wander(rnd(0.06, 0.12), -1), dots_mark(2.6, true),
							 hold("idle", 2.4), flip_face(), hold("look", 0.7), flip_face(), hold("idle", 1)};
			},
		});
}

static void mood_vibe(Pet &d)
{
	std::vector<Step> steps = {wake};
	for (int i = 1; i <= 6; i++) {
		steps.push_back(fx([i](Pet &dd) {
			dd.bobbing = true, dd.bob = 0;
			if (i % 2 == 1)
				hearts(dd, 1, "note", i % 4 == 1 ? "note" : "note2");
			if (i == 4)
				dd.face = -dd.face;
		}));
		steps.push_back(hold("idle", 0.42));
	}
	steps.push_back(hop(7, 0));
	steps.push_back(hold("idle", 1));
	chain(d, steps);
}

static void mood_startled(Pet &d)
{
	bang(d, true);
	d.sq = 0.3;
	pick(d, {
			[] { return std::vector<Step>{say("question"), hop(16, 0), loops("scared", 1), fx(face_chat), hold("look", 1.5)}; },
			[] {
				return std::vector<Step>{say("question"), loops("scared", 1), wander(rnd(0.1, 0.16), -1, "run"),
							 fx(face_chat), hold("look", 1.5)};
			},
		});
}

static void mood_swat_chat(Pet &d)
{
	face_chat(d);
	pick(d, {
			[] {
				return std::vector<Step>{wake, to_chat("run"), crouch(rnd(0.5, 0.9)), paw(rnd_int(2, 3), true),
							 hold("idle", 0.3), maybe(0.4, [](Pet &dd, Next n) {
								 crouch(0.3)(dd, [n](Pet &d2) { pounce_up(d2, n); });
							 }),
							 hold("look", 1)};
			},
			// a quick one-two and a hop back
			[] {
				return std::vector<Step>{wake, to_chat("run"), paw(1, true), hop(8, 0), paw(2, true), wander(0.04, -1),
							 fx(face_chat), hold("look", 1)};
			},
		});
}

static void mood_greet(Pet &d)
{
	pick(d, {
			[] { return std::vector<Step>{wake, to_chat("run"), hold("look", 0.6), call("question"), hop(11, 2), hold("idle", 1)}; },
			[] { return std::vector<Step>{wake, fx(face_chat), call("notice"), hop(9, 1), to_chat("walk"), hold("look", 1.2)}; },
		});
}

static void mood_judge(Pet &d)
{
	chain(d, {wake, to_chat("walk"), hold("look", 2.4), dots_mark(2.2), hold("idle", 1.2), fx(face_away),
		  loops("stretch", 1), hold("idle", 0.8)});
}

static void mood_scared(Pet &d)
{
	d.stay_down = d.now + rnd(30, 60);
	bang(d, true);
	d.sq = 0.3;
	chain(d, {say("angry"), loops("scared", 1), get_down_if_up, wander(rnd(0.18, 0.28), -1, "run"), fx(face_chat),
		  loops("scared", 1), hold("look", 1.4)});
}

static void mood_show_off(Pet &d)
{
	chain(d, {wake, lap, fx(face_chat), crouch(0.4), hop(20, 0), fx([](Pet &dd) {
			  sparks(dd);
			  dd.bobbing = true, dd.bob = 0;
		  }),
		  loops("clean", 2), hold("idle", 1)});
}

static void mood_beg(Pet &d)
{
	chain(d, {wake, to_chat("walk"), say("plead"), hold("beg", 1.6), hold("look", 0.6), hold("beg", 1.8),
		  hold("idle", 0.8)});
}

// ------------------------------------------------------------------ yank

static void burst(Pet &d, double x, double y)
{
	for (int i = 1; i <= 12; i++) {
		double a = i / 12.0 * 6.283 + rnd(-0.2, 0.2), v = rnd(25, 50) * d.S;
		Part p;
		p.k = Part::K::Spark;
		p.life = rnd(0.45, 0.85);
		p.x = x;
		p.y = y;
		p.vx = std::cos(a) * v;
		p.vy = std::sin(a) * v - 8 * d.S;
		spawn(d, p);
	}
}

// the swipe that sends it flying, away from the chat toward open floor
static void fling(Pet &d, Next n)
{
	if (d.has_yank_msg && Hub::get().gone(d.yank_msg.msg_id)) {
		// deleted since the pet set off (a mod, a ban): swipe at nothing rather than show it again
		if (d.yank_msg.req)
			with_chat(d, [&](ChatSource &c) { c.yank_done(d.yank_msg.req, false); });
		d.has_yank_msg = false;
		n(d);
		return;
	}
	if (!d.has_yank_msg || !d.can_yank) {
		if (d.has_yank_msg && d.yank_msg.req)
			with_chat(d, [&](ChatSource &c) { c.yank_done(d.yank_msg.req, false); });
		d.has_yank_msg = false;
		n(d);
		return;
	}
	YankMsg m = d.yank_msg;
	d.has_yank_msg = false;
	double w = d.chat.x1 - d.chat.x0, h = d.chat.y1 - d.chat.y0;
	// An OBSPETS Chat said where the line is and how it looks; the real line goes as this one flies.
	double k = 1;
	if (m.req)
		with_chat(d, [&](ChatSource &c) {
			YankReply r;
			if (c.yank_reply(m.req, r) && r.found) {
				m.fx = r.fx;
				m.fy = r.fy;
				if (!r.text.empty())
					m.text = r.text;
				if (!r.img.empty()) {
					m.img = r.img;
					if (r.page_w > 0)
						k = w / r.page_w;
				}
			}
			c.yank_done(m.req, true);
		});
	set_text(d.yname, m.user, m.color.empty() ? std::string("#a6c1db") : m.color);
	set_text(d.ytext, "  " + m.text, d.label_color);
	load_yank_img(d, m.img);
	if (!d.yimg)
		k = 1;
	double fxx = d.x;
	if (d.on == On::Perch) {
		Plan p;
		fxx = leave_plan(d, p) ? p.fx : nearest_floor_x(d, d.x);
	}
	const Seg *s = seg_at(d, fxx);
	Seg whole{0, d.W};
	if (!s)
		s = &whole;
	int dir = ((s->a + s->b) / 2 > chat_x(d)) ? 1 : -1;
	Yank &y = d.yank;
	y = Yank();
	y.x = d.chat.x0 + clampd(m.fx, 0, 1) * w;
	y.y = d.chat.y0 + clampd(m.fy, 0, 1) * h;
	y.t0 = d.now;
	y.vx = dir * rnd(800, 1150);
	y.vy = -rnd(700, 1000);
	y.va = dir * rnd(4, 8);
	y.lo = s->a - d.half;
	y.hi = s->b + d.half;
	y.k = clampd(k, 0.2, 4);
	y.msg_id = m.msg_id;
	y.strike = m.strike;
	d.has_yank = true;
	sound(d, "fling");
	d.sq = 0.2;
	n(d);
}
static void paw_fling(Pet &d, Next n)
{
	play(d, "paw");
	double half = anim_len(d, "paw", 1) / 2;
	after(d, half, [half, n](Pet &dd) {
		sparks(dd);
		fling(dd, [half, n](Pet &d2) { after(d2, half, n); });
	});
}
static void watch_it(Pet &d, Next n)
{
	play(d, "look");
	auto chk = std::make_shared<Next>();
	*chk = [n, w = std::weak_ptr<Next>(chk)](Pet &dd) {
		if (!dd.has_yank || dd.yank.rest || dd.now - dd.yank.t0 > 1.8) {
			n(dd);
			return;
		}
		if (std::abs(dd.yank.x - dd.x) > 2 * dd.S)
			dd.face = dd.yank.x > dd.x ? 1 : -1;
		if (auto self = w.lock())
			after(dd, 0.08, [self](Pet &d2) { (*self)(d2); });
	};
	(*chk)(d);
}
static void stalk(Pet &d, Next n)
{
	if (!d.has_yank) {
		n(d);
		return;
	}
	double a, b;
	range(d, a, b);
	int side = d.yank.x > d.x ? 1 : -1;
	double near = clampd(d.yank.x - side * 16 * d.S, a, b);
	Next creep = [near, n](Pet &dd) {
		walk_to(dd, near, "walk", [n](Pet &d2) {
			if (d2.has_yank)
				d2.face = d2.yank.x > d2.x ? 1 : -1;
			n(d2);
		});
	};
	if (std::abs(near - d.x) > 40 * d.S)
		walk_to(d, near - side * 30 * d.S, "run", creep);
	else
		creep(d);
}
static void bat_it(Pet &d, Next n)
{
	play(d, "paw");
	after(d, anim_len(d, "paw", 1) / 2, [n](Pet &dd) {
		if (dd.has_yank) {
			Yank &y = dd.yank;
			int side = y.x > dd.x ? 1 : -1;
			y.vx = side * rnd(150, 260);
			y.vy = -rnd(320, 420);
			y.va = side * rnd(1.5, 3);
			y.ground = y.rest = false;
			sound(dd, "swipe");
		}
		after(dd, anim_len(dd, "paw", 1) / 2, n);
	});
}
static void pounce_it(Pet &d, Next n)
{
	if (!d.has_yank) {
		n(d);
		return;
	}
	double a, b;
	range(d, a, b);
	leap(d, clampd(d.yank.x, a, b), d.floorY, On::Floor, 10 * d.S, n);
	d.prep = 0.04;
	d.pounce_yank = true;   // tick_motion bursts it as the pet comes down on it
	d.land_sfx = "pounce";
}

static void mood_yank(Pet &d)
{
	if (!d.has_yank_msg || !d.can_yank || d.has_yank) {
		// can't act it out here (Auto mod deleted it already, if it should go; an OBSPETS Chat
		// drops a crossed-out line by itself)
		d.has_yank_msg = false;
		if (d.can("swat_chat"))
			mood_swat_chat(d);
		return;
	}
	// an OBSPETS Chat finds the line now, while the pet runs over (fling picks up the answer)
	static long long reqs = 0;
	d.yank_msg.req = 0;
	if (d.chat_kind == ChatKind::Ours) {
		long long req = ++reqs;
		if (with_chat(d, [&](ChatSource &c) { c.ask_yank(req, d.yank_msg.msg_id, d.yank_msg.user, d.yank_msg.strike); }))
			d.yank_msg.req = req;
	}
	face_chat(d);
	// slow on purpose once it lands: chat should get a few seconds to read what it's about to destroy
	chain(d, {wake, to_chat("run"), crouch(rnd(0.35, 0.55)), paw_fling, watch_it, hold("look", 1), get_down_if_up, stalk,
		  hold("look", 0.6), bat_it, watch_it, hold("look", 0.7), crouch(rnd(1, 1.3)), pounce_it, say("cheer"),
		  loops("clean", 1), hold("idle", 0.8)});
}

const char *const MOOD_NAMES[] = {"angry", "swat_chat", "hop_on", "hop_off", "love",  "happy",    "greet",
				  "curious", "judge", "sleepy", "zoomies", "play", "show_off", "beg",
				  "sulk", "scared", "vibe", "startled", "yank"};
const int MOOD_COUNT = sizeof(MOOD_NAMES) / sizeof(MOOD_NAMES[0]);

bool run_mood(Pet &d, const std::string &k)
{
	static const std::map<std::string, void (*)(Pet &)> M = {
		{"angry", mood_angry},     {"hop_on", mood_hop_on},   {"hop_off", mood_hop_off}, {"love", mood_love},
		{"happy", mood_happy},     {"curious", mood_curious}, {"sleepy", mood_sleepy},   {"zoomies", mood_zoomies},
		{"play", mood_play},       {"sulk", mood_sulk},       {"vibe", mood_vibe},       {"startled", mood_startled},
		{"swat_chat", mood_swat_chat}, {"greet", mood_greet}, {"judge", mood_judge},     {"scared", mood_scared},
		{"show_off", mood_show_off}, {"beg", mood_beg},       {"yank", mood_yank},
	};
	auto it = M.find(k);
	if (it == M.end())
		return false;
	it->second(d);
	return true;
}

// ------------------------------------------------------------------ label, pets, react

bool with_chat(Pet &d, const std::function<void(ChatSource &)> &f)
{
	if (d.chat_kind != ChatKind::Ours || d.chat_name.empty())
		return false;
	obs_source_t *s = obs_get_source_by_name(d.chat_name.c_str());
	ChatSource *c = ChatSource::from(s);
	if (c)
		f(*c);
	obs_source_release(s);
	return c != nullptr;
}

void set_label(Pet &d, const std::string &user, const std::string &color, const std::string *rest)
{
	if (!d.lname || !d.lrest)
		return;
	set_text(d.lname, user, color.empty() ? std::string("#a6c1db") : color);
	d.label_solo = rest && rest->empty();
	set_text(d.lrest, d.label_solo ? std::string(" ") : rest ? *rest : d.label_after, d.label_color);
	d.label_on = true;
	d.label_t = 0;
	d.label_side = d.x < d.W / 2 ? 1 : -1;
}

void petted(Pet &d, const std::string &user, const std::string &color)
{
	set_label(d, user.empty() ? "someone" : user, color);
	hearts(d, 3);
	sound(d, "pet", false, "pet");   // asleep too: it purrs in its sleep (whatever else it's doing)
	d.bobbing = true, d.bob = 0;
	if (d.busy || d.anim == "sleep" || d.mode != Mode::Ground)
		return;
	d.act = "pet";   // (its hop and landing are the channel points' too)
	double r = rnd(0, 1);
	if (r < 0.45)
		chain(d, {hold("idle", 0.3), loops("clean", 1), hold("idle", 1)});
	else if (r < 0.75)
		chain(d, {hold("idle", 0.25), hop(9, 0), hold("idle", 1.2)});
	else
		chain(d, {hold("idle", 0.3), paw(1), hold("idle", 1)});
}

static double napped(Pet &d) { return d.anim == "sleep" ? d.now - d.nap_at : 0; }
static bool waker(const std::string &k) { return k == "angry" || k == "yank" || k == "startled" || k == "scared"; }

void react(Pet &d)
{
	if (d.leaving)
		return;   // (on its way off screen: nothing else)
	bool new_msg = d.rep_seq != d.seen_rep;
	d.seen_rep = d.rep_seq;
	if (new_msg) {
		d.heat += 1;
		d.quiet = 0;
		d.burst_n += 1;
		// a sudden rush of messages wakes it, once it has had a bit of a nap
		if (d.burst_n > 6 && napped(d) > NAP_GRACE && d.anim == "sleep" && d.mode == Mode::Ground && !d.busy && d.can("startled")) {
			d.act = "startled";
			startled(d);
			d.bobbing = true, d.bob = 0;
		}
	}
	if (d.has_pet) {
		d.has_pet = false;
		petted(d, d.next_pet_user, d.next_pet_color);
	}
	if (d.mode == Mode::Air)
		return;
	if (d.on == On::Perch) {
		if (!d.has_perch) {
			leap(d, nearest_floor_x(d, d.x), d.floorY, On::Floor, 4 * d.S, nullptr);
			d.prep = 0;
		} else if (d.anim == "sleep" && d.mode == Mode::Ground) {
			if (new_msg)
				d.bobbing = true, d.bob = 0;   // rides the stack without waking
		} else if (new_msg && d.perch_y < d.y - 3 * d.S) {
			// a message made the stack taller: launched up with it
			leap(d, clampd(d.x, d.perch_x0, d.perch_x1), d.perch_y, On::Perch, 16 * d.S, startled);
			d.land_sfx.clear();
			d.prep = 0;
			d.sq = 0.28;
			d.fixed = "rise";
		} else if (d.perch_y > d.y + 12 * d.S) {
			leap(d, clampd(d.x, d.perch_x0, d.perch_x1), d.perch_y, On::Perch, 0, startled);
			d.land_sfx.clear();
			d.prep = 0;
		} else {
			if (new_msg)
				d.bobbing = true, d.bob = 0;
			if (d.mode != Mode::Move && (d.x < d.perch_x0 - 1 || d.x > d.perch_x1 + 1))
				walk_to(d, clampd(d.x, d.perch_x0, d.perch_x1), "walk", idle_then_decide);
		}
	} else if (d.on == On::Ledge) {
		if (!d.has_ledge) {   // it went, or was hidden: down to the floor
			interrupt(d);
			leap(d, nearest_floor_x(d, d.x), d.floorY, On::Floor, 4 * d.S, nullptr);
			d.prep = 0;
		} else if (std::abs(d.on_ledge.y - d.y) > 12 * d.S) {   // it was moved a long way: hops after it
			interrupt(d);
			leap(d, clampd(d.x, d.on_ledge.x0, d.on_ledge.x1), d.on_ledge.y, On::Ledge, 0, startled);
			d.land_sfx.clear();
			d.prep = 0;
		} else if (d.mode != Mode::Move && (d.x < d.on_ledge.x0 - 1 || d.x > d.on_ledge.x1 + 1)) {
			walk_to(d, clampd(d.x, d.on_ledge.x0, d.on_ledge.x1), "walk", idle_then_decide);
		}
		// alerts and chat reactions happen on the floor: down first, and they run once it lands
		// (not for one it sleeps through, or a purr or a yawn in its sleep, which happen right there)
		const MoodReq &m = d.next_mood;
		bool asleep = d.anim == "sleep";
		bool in_place = asleep && (m.kind == "love" || m.kind == "sleepy");
		bool slept = !m.test && asleep && napped(d) < NAP_GRACE && !waker(m.kind);
		bool mood = d.has_mood && !d.busy && !in_place && !slept;
		if (d.mode == Mode::Ground && (d.has_alert || mood)) {
			interrupt(d);
			off_ledge(d, nullptr);
			return;
		}
	} else if (d.on == On::Floor && d.mode != Mode::Move && !d.segs.empty() && !seg_at(d, d.x)) {
		walk_to(d, nearest_floor_x(d, d.x), "run", idle_then_decide);
	}
	// an alert waits until it is on its feet, then cuts in on whatever it does
	if (d.has_alert && d.mode != Mode::Air) {
		d.has_alert = false;
		Alert a = d.next_alert;
		interrupt(d);
		d.voice = "alert";
		d.act = a.kind;
		if (!run_alert(d, a.kind, a.n))
			idle_then_decide(d);
	}
	// a mood waits for alerts and for the reaction before it to finish, and is dropped if that
	// takes too long (chat has moved on)
	if (d.has_mood && d.mode != Mode::Air && !d.busy && !d.has_alert) {
		d.has_mood = false;
		MoodReq m = d.next_mood;
		if (d.now - m.at >= 12) {
			if (!m.test)
				Hub::get().mood_not_shown(m.kind);
			return;
		}
		if (!m.test && d.anim == "sleep" && d.mode == Mode::Ground && napped(d) < NAP_GRACE && !waker(m.kind) &&
		    m.kind != "love" && m.kind != "sleepy") {
			info("pet: mood %s (slept through it)", m.kind.c_str());
			Hub::get().mood_not_shown(m.kind);
			d.bobbing = true, d.bob = 0;
			return;
		}
		info("pet: mood %s", m.kind.c_str());
		d.voice = "mood";
		d.act = m.kind == "yank" && m.strike ? "automod" : m.kind;
		if (!(d.anim == "sleep" && (m.kind == "love" || m.kind == "sleepy")))
			interrupt(d);
		if (m.kind == "yank") {
			if (Hub::get().gone(m.msg_id))   // a mod deleted it while it waited: nothing to show
				m.text.clear();
			d.has_yank_msg = !m.text.empty();
			d.yank_msg = {m.user, m.color, m.text, "", m.msg_id, m.fx, m.fy, m.strike};
			mood_yank(d);
		} else {
			run_mood(d, m.kind);
			if (!m.user.empty()) {
				static const std::string none;
				set_label(d, m.user, m.color, &none);
			}
		}
		// (a chain that didn't start leaves it standing: keep it going)
		if (!d.has_timer && d.mode == Mode::Ground && !d.cb)
			idle_then_decide(d);
	}
}

// ------------------------------------------------------------------ the tick

void tick_motion(Pet &d, double dt)
{
	// things that happen on one frame of an animation
	if (!d.on_frame.empty() && d.fixed.empty()) {
		const Anim &an = d.pack->anim(d.anim);
		int col = (int)std::fmod(std::max(0.0, std::floor(d.at * an.fps)), (double)std::max(1, an.n()));
		if (col != d.of_col) {
			auto fs = d.on_frame;
			for (auto &f : fs)
				if (col == f.col)
					f.fn(d);
		}
		d.of_col = col;
	} else {
		d.of_col = -1;
	}

	if (d.mode == Mode::Move) {
		double step = d.speed * dt;
		if (std::abs(d.tx - d.x) <= step) {
			d.x = d.tx;
			d.mode = Mode::Ground;
			Next f = std::move(d.done);
			d.done = nullptr;
			if (f)
				f(d);
		} else {
			d.x += d.tx > d.x ? step : -step;
		}
	} else if (d.mode == Mode::Air) {
		if (d.prep > 0) {
			d.prep -= dt;
			d.sq = -0.14;
		} else {
			if (d.dest == On::Perch && d.has_perch)
				d.ay1 = d.perch_y;
			if (d.dest == On::Ledge && d.has_ledge)
				d.ay1 = d.on_ledge.y;
			if (d.dest == On::Floor)
				d.ay1 = d.floorY;
			d.air += dt;
			double p = std::min(1.0, d.air / d.T);
			d.x = d.ax0 + (d.ax1 - d.ax0) * p;
			d.y = d.ay0 + (d.ay1 - d.ay0) * p - d.peak * 4 * p * (1 - p);
			double vy = (d.ay1 - d.ay0) - d.peak * 4 * (1 - 2 * p);
			d.fixed = vy < 0 ? "rise" : "fall";
			if (p >= 1)
				land(d);
		}
	} else if (d.has_timer) {
		d.timer -= dt;
		if (d.timer <= 0) {
			d.has_timer = false;
			Next f = std::move(d.cb);
			d.cb = nullptr;
			if (f)
				f(d);
		}
	}

	d.sq *= std::exp(-dt * 9);
	// the butt wiggle before a pounce
	if (d.wiggling) {
		d.wiggle += dt;
		d.sq = -0.12 + 0.05 * std::sin(d.wiggle * 22);
		d.jx = std::sin(d.wiggle * 28) * 0.7 * d.S;
	} else {
		d.jx = 0;
	}

	if (d.anim == "sleep" && d.mode == Mode::Ground && d.fixed.empty()) {
		d.z_t -= dt;
		if (d.z_t <= 0) {
			d.z_t = 1.8;
			Part p;
			p.k = Part::K::Z;
			p.life = 3.2;
			p.x = d.x + d.face * (d.pack->head + 1) * d.px;
			p.y = d.y - (d.pack->top_asleep + 2) * d.px;
			p.f = d.face;
			spawn(d, p);
		}
	}
	for (auto &p : d.parts)
		p.t += dt;
	d.parts.erase(std::remove_if(d.parts.begin(), d.parts.end(), [](const Part &p) { return p.t > p.life; }), d.parts.end());

	// a yanked chat message: falls, bounces off the floor and the ends of the floor, slides to rest
	if (d.has_yank) {
		Yank &y = d.yank;
		double floor = d.floorY - (y.hh * std::abs(std::cos(y.a)) + y.hw * std::abs(std::sin(y.a)));
		if (!y.rest) {
			if (!y.ground) {
				y.vy += 2600 * dt;
				y.x += y.vx * dt;
				y.y += y.vy * dt;
				y.a += y.va * dt;
				if (y.y >= floor && y.vy > 0) {
					y.y = floor;
					y.vy = -y.vy * 0.42;
					y.vx *= 0.7;
					y.va = -y.va * 0.5 + rnd(-1.5, 1.5);
					if (std::abs(y.vy) < 160)
						y.ground = true;
				}
			} else {
				y.y = floor;
				y.vy = 0;
				y.x += y.vx * dt;
				y.vx *= std::exp(-dt * 5);
				if (std::abs(y.vx) < 20)
					y.rest = true;
			}
			if (y.x - y.hw < y.lo && y.vx < 0)
				y.vx = -y.vx * 0.6;
			else if (y.x + y.hw > y.hi && y.vx > 0)
				y.vx = -y.vx * 0.6;
		}
		if (y.ground)
			y.a *= std::exp(-dt * 8);   // settles flat
		if (d.pounce_yank && d.mode == Mode::Air && d.air >= d.T * 0.85) {
			d.pounce_yank = false;
			burst(d, y.x, y.y);
			sound(d, "pop", false, y.strike ? "automod" : "yank");
			d.has_yank = false;
		} else if (d.now - y.t0 > 20 || (!y.msg_id.empty() && Hub::get().gone(y.msg_id))) {
			// left lying (an alert cut in), or deleted by a mod while it flies: it fizzles out
			burst(d, y.x, y.y);
			sound(d, "pop", false, y.strike ? "automod" : "yank");
			d.has_yank = false;
		}
	}
	if (d.pounce_yank && d.mode != Mode::Air)
		d.pounce_yank = false;
}

// ------------------------------------------------------------------ coming on and going off

void leave_stage(Pet &d, int dir)
{
	interrupt(d);
	d.has_alert = d.has_mood = d.has_pet = d.has_yank_msg = false;
	d.label_on = false;
	d.busy = true;
	d.leaving = true;
	d.act.clear();
	auto jump = [dir](Pet &p) {
		sound(p, "notice");
		leap(p, dir < 0 ? -p.half * 3 : p.W + p.half * 3, p.floorY, On::Floor, 34 * p.S, [](Pet &q) {
			q.on_stage = q.leaving = q.started = q.busy = false;
			q.has_yank = false;
		});
		p.land_sfx.clear();   // (it lands out of sight)
	};
	// on the floor it runs to the end of its stretch first; from the chat or mid-air it just goes
	const Seg *s = d.on == On::Floor && d.mode != Mode::Air ? seg_at(d, d.x) : nullptr;
	if (s)
		walk_to(d, dir < 0 ? s->a : s->b, "run", jump);
	else
		jump(d);
}

void enter_stage(Pet &d, int dir)
{
	scan(d);
	d.started = true;
	d.leaving = d.busy = false;
	d.has_alert = d.has_mood = d.has_pet = d.has_yank_msg = d.has_yank = false;
	d.label_on = false;
	d.act.clear();
	interrupt(d);
	d.mode = Mode::Ground;
	d.on = On::Floor;
	d.x = dir < 0 ? -d.half * 3 : d.W + d.half * 3;
	d.y = d.floorY;
	d.face = -dir;
	play(d, "idle");
	double to = rnd(0.08, 0.32);   // (several coming on at once land apart)
	leap(d, nearest_floor_x(d, d.W * (dir < 0 ? to : 1 - to)), d.floorY, On::Floor, 34 * d.S, nullptr);
}

} // namespace obspet
