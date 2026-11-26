// The OBSPETS source (PetSource) and the pets in it (Pet). One source holds up to MAX_PETS pets
// that live along the bottom of a scene; some or all are on screen, and they can take turns. The
// source hears alerts, channel-point redemptions and chat moods from the hub (Twitch, Kick and
// sindruid.com) and hands each to the pet its rules pick.
//
// Threads: video_tick, video_render and update run on OBS's graphics thread (OBS defers a video
// source's updates to its tick). Hub events and property buttons come from other threads and go
// through the source's `inbox_` (under `mu_`), which its tick empties.
#pragma once

#include "audio.hpp"
#include "hub.hpp"
#include "pack.hpp"
#include "util.hpp"

#include <graphics/graphics.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace obspet {

struct Pet;
struct PetSource;
inline const int MAX_PETS = 6;
using Next = std::function<void(Pet &)>;
using Step = std::function<void(Pet &, Next)>;

struct Box {
	double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct Seg {
	double a = 0, b = 0;
};

struct Part {
	enum class K { Z, Heart, Mark, Spark } k = K::Heart;
	double t = 0, life = 1;
	double x = 0, y = 0, vx = 0, vy = 0, ph = 0;
	int f = 1;
	// the glyph's name in the pack (nullptr: the kind's own). A name, not a pointer into the pack: the
	// pack can be reloaded (the editor saves) while this is still on screen.
	const char *g = nullptr;
	std::string c;              // color role
	bool big = false, throb = false, side = false;
};

struct YankMsg {
	std::string user, color, text, img, msg_id;
	double fx = 0.5, fy = 0.9;   // where it is in the chat, as fractions (an OBSPETS Chat says exactly)
	bool strike = false;  // Auto mod deleted it: it flies crossed out
	long long req = 0;    // what it asked an OBSPETS Chat about this message (0: nothing)
};
struct Yank {
	double x = 0, y = 0, a = 0, hw = 120, hh = 16, t0 = 0, vx = 0, vy = 0, va = 0, lo = 0, hi = 0;
	double k = 1;   // the picture's scale: an OBSPETS Chat's px to the canvas (the chat may be scaled)
	bool ground = false, rest = false;
	bool strike = false;   // crossed out (Auto mod deleted it), drawn over the text unless the picture has it
	std::string msg_id;   // the chat message it is (a mod deleting it pops it at once)
};

struct Alert {
	std::string kind;
	int n = 1;
};
struct MoodReq {
	std::string kind;
	double at = 0;
	std::string user, color, text, msg_id;
	double fx = 0.5, fy = 0.9;
	bool strike = false;   // Auto mod deleted it (already): a "yank" flings it crossed out
	bool test = false;   // from "Try it": always plays, even through a nap
};

// A chat or alert source picked as "none" in the properties (empty means: find it by its name).
inline const char *NO_SOURCE = "(none)";

// What kind of chat it has, which decides what it can do with it:
// Ours: an OBSPETS Chat. It says where its messages are and hands over the line the pet yanks.
// Other: any other chat overlay. The pet walks around it; sitting on it (perch_box, by reading
//        its pixels) and yanking from it (other_yank) are off unless ticked.
enum class ChatKind { None, Ours, Other };

// Who hears the pet (the "hear" setting): the streamer (OBS's audio monitoring) and the stream.
enum class Hear { Both, Stream, Me, Off };

enum class Mode { Ground, Move, Air };
// Perch: on top of the chat. Ledge: on top of a kept-off source it may sit on ("sit_on").
enum class On { Floor, Perch, Ledge };

// One pet. Its settings are the source's, with its own character, name, size and chance (`slot`
// 0 is the first pet: the keys "pack", "name", ...; slot 1 reads "pack2", "name2", ...).
struct Pet {
	Pet(PetSource *owner, int slot, obs_data_t *settings);
	~Pet();
	Pet(const Pet &) = delete;
	Pet &operator=(const Pet &) = delete;

	void update(obs_data_t *s);
	void tick(double dt);
	void render();
	std::string k(const char *base) const;   // this pet's settings key

	PetSource *owner;
	int slot;
	obs_source_t *source = nullptr;   // the OBSPETS source it's in
	PackPtr pack;
	std::string pack_err, pack_key;

	// graphics
	gs_effect_t *effect = nullptr;
	gs_eparam_t *p_image = nullptr, *p_frame = nullptr, *p_texsize = nullptr, *p_flip = nullptr, *p_color = nullptr,
		    *p_alpha = nullptr;
	gs_texture_t *tex = nullptr, *yimg = nullptr;
	gs_texrender_t *tr = nullptr, *ytr = nullptr;
	uint32_t tw = 0, th = 0, yw = 0, yh = 0;
	obs_source_t *lname = nullptr, *lrest = nullptr, *yname = nullptr, *ytext = nullptr;
	std::string font_key;

	// settings
	double W = 1920, H = 1080, px = 5, S = 5, floor_px = 24;
	std::string perch_name, alert_name;
	std::vector<std::string> avoid_names;
	std::set<std::string> sit_names;   // the kept-off sources it may also sit on top of
	double visit = 0.5, naps = 1, energy = 1;
	bool reactions = true, twitch_alerts = true, perch_box = false, other_yank = false;
	bool automod = true;         // Auto mod runs for this source (the account's categories)
	bool automod_show = true;    // ... and a pet rips what it deletes out of the chat on stream
	std::string reward;   // the channel-point reward that pets it ({name} filled in)
	std::string name, label_after;
	uint32_t label_color = 0xFFDEE4EC;
	Hear hear = Hear::Both;
	Mixer *mixer = nullptr;                  // the source's audio
	std::map<std::string, ClipPtr> clips;    // decoded sounds by path (nullptr: won't play)
	MoodProfile mood;
	std::vector<std::string> ignore_own;     // its own "never react to" list (the chat's hidden chatters join it)
	std::string chat_hide;                   // its OBSPETS Chat's hidden chatters, as last merged

	// the scene, in the pet's own coordinates: its source is canvas-sized, and if its box was moved
	// or resized, everything around it is mapped into it (scan)
	double floorY = 1056, half = 45, alert_x = 960;
	long long scene_seen = -1;   // the source's scene lookup this pet last worked from (PetSource::scene_n)
	bool moved = false;   // its box isn't the whole canvas (the properties offer to fix that)
	std::vector<Seg> segs;
	bool has_chat = false, has_perch = false, can_yank = false;
	ChatKind chat_kind = ChatKind::None;
	std::string chat_name;   // the chat source in use right now
	Box chat;
	long long rep_seq = 0, seen_rep = 0;   // messages counted by an OBSPETS Chat, or seen arriving in another chat
	// another chat's pixels (when it may sit on it): the scan asks, the render reads them
	bool want_px = false;
	std::string px_src;
	double px_top = -1;   // the highest drawn row, as a fraction of the source's height up from its bottom (-1: none yet)
	double px_at = -1e9, px_prev = -1;
	gs_texrender_t *px_tr = nullptr;
	gs_stagesurf_t *px_stage = nullptr;
	uint32_t px_w = 0, px_h = 0;
	bool px_staged = false;
	double perch_y = 0, perch_x0 = 0, perch_x1 = 0;
	// the tops of the kept-off sources it may sit on, and the one it is on (or jumping to)
	struct Ledge {
		std::string name;
		Box box;              // the source
		double y, x0, x1;     // where it stands on it
	};
	std::vector<Ledge> ledges;
	std::string ledge;        // the one it is on
	bool has_ledge = false;   // ... and it is still there
	Ledge on_ledge;
	std::string state_sent;

	// on screen or not (the source's swap rules)
	double weight = 5;            // its chance, when the rules pick at random (0: never)
	std::set<std::string> wont;   // reactions it's set not to do (REACTS ids)
	bool can(const std::string &kind) const { return !wont.count(kind); }
	bool on_stage = false;        // on screen, or coming on
	bool leaving = false;         // jumping off screen
	bool swap_after = false;      // reacted to an alert: swapped out once it's done ("Swap after an alert")
	double stage_at = 0, react_at = 0;   // the source's clock when it came on, and when it got the alert
	double start_frac = 0.72;     // where it first stands, as a fraction of the canvas width

	// the pet
	bool started = false;
	double now = 0, x = 0, y = 0, at = 0, sq = 0, z_t = 0, jx = 0;
	int face = 1;
	Mode mode = Mode::Ground;
	On on = On::Floor;
	std::string anim = "idle";
	std::string fixed;   // a held pose ("" = the animation)
	struct OnFrame {
		int col;
		Next fn;
	};
	std::vector<OnFrame> on_frame;
	int of_col = -1;
	bool has_timer = false;
	double timer = 0;
	Next cb, done;
	double tx = 0, speed = 0;
	// a jump
	On dest = On::Floor;
	double ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0, air = 0, peak = 0, T = 1, prep = 0;
	std::string land_sfx;
	bool wiggling = false;
	double wiggle = 0;
	bool bobbing = false;
	double bob = 0;
	bool busy = false;
	int last_act = -1;
	double stay_down = 0, nap_at = 0;
	double heat = 0, quiet = 0, burst_n = 0;
	std::string voice;   // whose chain this is: "alert" or "mood"
	double voiced = -1e9;
	std::map<std::string, int> last_file;
	std::vector<Part> parts;
	// the label
	bool label_on = false, label_solo = false;
	double label_t = 0;
	int label_side = 1;
	// yank
	bool has_yank_msg = false, has_yank = false, pounce_yank = false;
	YankMsg yank_msg;
	Yank yank;

	// pending
	bool has_alert = false, has_pet = false, has_mood = false;
	Alert next_alert;
	std::string next_pet_user, next_pet_color;
	MoodReq next_mood;

	// copies other threads read (under mu_)
	std::mutex mu_;
	struct Ui {
		PackPtr pack;
		std::string pack_err;
	} ui;

private:
	void use_pack(const std::string &dir);
};

// The OBSPETS source: its pets, who of them is on screen, and who reacts.
// What's around the pets in their scene: looked up once for all of a source's pets (scan_scene),
// in the pets' own coordinates. Each pet works out its floor and perch from it (scan).
struct SceneView {
	bool moved = false;   // the source's box isn't the whole canvas
	bool have_chat = false, have_alert = false;
	Box chat, alert;
	std::vector<Box> avoid;   // the kept-off sources that are on screen (not the chat)
	std::vector<std::pair<std::string, Box>> sit;   // the ones of them it may sit on
	ChatKind kind = ChatKind::None;
	std::string chat_name;    // the chat source in use
	uint32_t chat_h = 0;      // its height (another chat's pixels are read by each pet)
	long long top = 0, page_h = 0, seq = 0;   // an OBSPETS Chat's layout
	bool have_seq = false;
	std::string hide;         // an OBSPETS Chat's hidden chatters
	std::string auto_chat, auto_alert;   // found by name or URL when the settings name none
};

struct PetSource : PetClient {
	PetSource(obs_data_t *settings, obs_source_t *source);
	~PetSource() override;

	// PetClient (any thread)
	void hub_event(const HubEvent &e) override;
	void hub_status_changed() override;
	bool on_screen() const override;

	// OBS
	void update(obs_data_t *s);
	void tick(float seconds);
	void render();
	obs_properties_t *properties();
	static void defaults(obs_data_t *s);
	uint32_t width() const { return (uint32_t)W; }
	uint32_t height() const { return (uint32_t)H; }

	// any thread
	static PetSource *from(obs_source_t *s);   // nullptr unless it's an OBSPETS source
	void test(const std::string &what, const std::string &pack_id = "");   // pack_id: a pet showing that character
	void try_mood(const std::string &kind, const std::string &user, const std::string &text, bool automod);   // the chat tester
	void act_out(Pet &p, const std::string &kind, const std::string &user, const std::string &text, bool automod);
	void reload_pack(const std::string &id = "");   // "": every pet's
	std::string pack_status();                      // the first pet's character
	std::vector<PackPtr> packs_shown();
	void post(std::function<void(PetSource &)> f);
	void refresh_properties();

	// graphics thread
	void profile_changed();          // the hub's mood profile: the pet that reacts to chat next
	std::vector<Pet *> stage();      // on screen and settled (not coming or going)
	Pet *pick(const std::string &kind = "");             // one of them that does `kind`, by the rules
	std::vector<Pet *> reactors(const std::string &kind);   // who reacts to an alert or channel points

	obs_source_t *source = nullptr;
	double W = 1920, H = 1080, now = 0;
	std::unique_ptr<Mixer> mixer;
	std::vector<std::unique_ptr<Pet>> pets;   // by slot (changed on the graphics thread, under pets_mu_)
	std::mutex pets_mu_;
	Pet *lead_ = nullptr;   // slot 0, there as long as the source is

	// the scene around the pets, looked up 4 times a second for all of them (scan_scene)
	SceneView scene;
	long long scene_n = 0;   // how many lookups so far (a pet rescans when this changes)
	double scene_t = 0, auto_t = 0;

	// the rules
	enum class Who { Turns, Random, All } who = Who::Turns;
	int at_once = MAX_PETS;
	double swap_every = 0;      // seconds (0: never)
	bool swap_alert = false;
	bool next_random = false;   // who comes on next: at random by chance (else the next in line)
	int turn = -1;              // the slot that reacted last ("Take turns")
	int next_chat = -1;         // the slot the next chat reaction goes to (its profile is the hub's)
	int last_in = -1, last_out = -1, exit_dir = 1;
	double swap_t = 0, entered_at = -1e9;
	bool staged = false, swap_due = false;

	Hear hear = Hear::Both;
	bool hear_set = false, setup_audio = false;

	std::mutex mu_;
	std::vector<std::function<void(PetSource &)>> inbox_;
	std::atomic<bool> status_dirty_{false};
	// when the properties were last edited (OBS's update callback): a due refresh waits until
	// the typing has stopped, because rebuilding the properties takes the cursor out of the box
	std::atomic<uint64_t> last_edit_ns_{0};
	bool refresh_due();
	// copies the UI thread reads (under mu_)
	struct Ui {
		PackPtr pack;                        // the first pet's
		std::string pack_err, name;
		std::string auto_chat, auto_alert;   // what "find it by itself" found
		bool moved = false;                  // its box was moved or resized
	} ui;

private:
	Pet *at_slot(int slot);
	void balance(double dt);
	bool send_out(Pet *only = nullptr);
};

// the source (pet-source.cpp)
extern const char *PET_SOURCE_ID;
void register_pet_source();
void with_pets(const std::function<void(PetSource &)> &f);   // any thread: holds the list's lock, so only the "any thread" methods and ui (under mu_) are safe
void add_account_properties(obs_properties_t *p, const std::string &pet_name = "", const std::string &prefix = "");
std::string pet_name_now(PetSource *pet, obs_data_t *s);
std::string pet_reward_now(PetSource *pet, obs_data_t *s);
std::string pet_pack_name(const std::string &id);
ChatKind pet_chat_kind(PetSource *pet, obs_data_t *s, std::string &name);
std::string add_chat_for_pet(PetSource *pet);   // UI thread: creates OBSPETS Chat in current scene (links to pet if non-null)
std::string add_pet_to_scene();                 // UI thread: finds or creates OBSPETS in current scene

// behavior (pet.cpp)
void scan_scene(PetSource &ps);   // the scene lookup all of a source's pets share
void scan(Pet &d);                 // a pet's own floor and perch, from that
void decide(Pet &d);
void react(Pet &d);
void merge_ignore(Pet &d);                 // mood.ignore: its own list and its chat's hidden chatters
bool fit_to_screen(obs_source_t *self);   // UI thread: its box in the program scene back to the whole canvas
void set_monitoring(obs_source_t *src, Hear h);   // (pet-source.cpp)
bool obs_prevents_monitoring_echo();             // OBS 32+ (pet-source.cpp)
void leave_stage(Pet &d, int dir);   // runs and jumps off the canvas (dir: -1 left, 1 right), then is off screen
void enter_stage(Pet &d, int dir);   // jumps in from that side
void play(Pet &d, const std::string &a);
void after(Pet &d, double secs, Next fn);
bool sound(Pet &d, const std::string &key, bool from_mood = false);   // -> it played
void act_visit(Pet &d);
void interrupt(Pet &d);
void set_label(Pet &d, const std::string &user, const std::string &color, const std::string *rest = nullptr);
void tick_motion(Pet &d, double dt);
bool run_mood(Pet &d, const std::string &kind);
bool run_alert(Pet &d, const std::string &kind, int n);
void petted(Pet &d, const std::string &user, const std::string &color);
class ChatSource;
bool with_chat(Pet &d, const std::function<void(ChatSource &)> &f);   // the OBSPETS Chat it uses, if it is one
extern const char *const MOOD_NAMES[];
extern const int MOOD_COUNT;

// drawing (pet-render.cpp)
extern const char *EFFECT;
void set_text(obs_source_t *src, const std::string &text, uint32_t color);
void set_text(obs_source_t *src, const std::string &text, const std::string &hex);
obs_source_t *text_source(const char *name, obs_data_t *font);
const char *text_id();
void load_yank_img(Pet &d, const std::string &b64);
void read_chat_pixels(Pet &d);   // graphics thread, in render
void free_gfx(Pet &d);

} // namespace obspet
