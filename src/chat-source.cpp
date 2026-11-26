// OBSPETS Chat (see chat-source.hpp).
#include "chat-source.hpp"
#include "chat-server.hpp"
#include "hub.hpp"

#include <callback/calldata.h>
#include <callback/proc.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>

namespace obspet {

const char *CHAT_SOURCE_ID = "obspet_chat";   // OBS saves sources by this id: changing it orphans them

// ------------------------------------------------------------------ pages, by id

static std::mutex g_reg_mu;
static std::map<std::string, std::weak_ptr<ChatSource::Shared>> g_reg;
static std::map<std::string, obs_weak_source_t *> g_weak;   // to refresh a chat's properties

static std::shared_ptr<ChatSource::Shared> shared_for(const std::string &id)
{
	std::lock_guard<std::mutex> g(g_reg_mu);
	auto it = g_reg.find(id);
	return it == g_reg.end() ? nullptr : it->second.lock();
}

static void refresh_props_of(const std::string &id)
{
	obs_weak_source_t *w = nullptr;
	{
		std::lock_guard<std::mutex> g(g_reg_mu);
		auto it = g_weak.find(id);
		if (it == g_weak.end())
			return;
		w = it->second;
		obs_weak_source_addref(w);
	}
	obs_queue_task(
		OBS_TASK_UI,
		[](void *p) {
			auto *ws = static_cast<obs_weak_source_t *>(p);
			if (obs_source_t *s = obs_weak_source_get_source(ws)) {
				obs_source_update_properties(s);
				obs_source_release(s);
			}
			obs_weak_source_release(ws);
		},
		w, false);
}

// The page's requests (server thread). /chat/api/config, /chat/api/report, /chat/api/badges.
HttpResp chat_api(const HttpReq &r)
{
	HttpResp out;
	std::string what = r.path.substr(10);
	if (what == "badges") {
		out.body = Hub::get().chat_badges(lower(trim(r.q("channel"))));
		return out;
	}
	auto sh = shared_for(r.q("id"));
	if (!sh) {
		out.status = 404;
		out.body = "{\"error\":\"no such chat\"}";
		return out;
	}
	if (what == "config" && r.method == "GET") {
		std::lock_guard<std::mutex> g(sh->mu);
		out.body = sh->config;
		return out;
	}
	if (what == "report" && r.method == "POST") {
		Data d = data_json(r.body);
		if (!d) {
			out.status = 400;
			out.body = "{\"error\":\"json\"}";
			return out;
		}
		std::string kind = str(d.get(), "kind");
		{
			std::lock_guard<std::mutex> g(sh->mu);
			if (kind == "layout") {
				sh->top = obs_data_get_int(d.get(), "top");
				sh->h = obs_data_get_int(d.get(), "h");
				sh->seq = obs_data_get_int(d.get(), "seq");
				sh->reported = true;
			} else if (kind == "yank") {
				YankReply y;
				y.found = obs_data_get_bool(d.get(), "found");
				y.fx = clampd(obs_data_get_double(d.get(), "fx"), 0, 1);
				y.fy = clampd(obs_data_get_double(d.get(), "fy"), 0, 1);
				y.page_w = clampd(obs_data_get_double(d.get(), "page_w"), 0, 16384);
				y.text = str(d.get(), "text");
				y.img = str(d.get(), "img");
				sh->yanks[obs_data_get_int(d.get(), "req")] = std::move(y);
				while (sh->yanks.size() > 8)
					sh->yanks.erase(sh->yanks.begin());
			} else if (kind == "status") {
				std::string st = str(d.get(), "text");
				if (st != sh->status)
					sh->refresh_due = true;   // (tick() refreshes the properties when it's safe)
				sh->status = st;
			}
		}
		out.status = 204;
		return out;
	}
	out.status = 404;
	out.body = "{\"error\":\"what\"}";
	return out;
}

// ------------------------------------------------------------------ settings -> the page's config

// OBS colors are 0xAABBGGRR
static std::string css_color(long long c, bool alpha)
{
	unsigned v = (unsigned)c;
	char buf[48];
	snprintf(buf, sizeof buf, "rgba(%u,%u,%u,%.3f)", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, alpha ? ((v >> 24) & 0xFF) / 255.0 : 1.0);
	return buf;
}

// The channel in the field `key` (a name, "#name", "@name" or a pasted link), or the one this OBS
// is signed in with on that platform if it's empty.
static std::string channel_of(obs_data_t *s, const char *key, const std::string &mine)
{
	std::string c = lower(trim(str(s, key)));
	size_t slash = c.find_last_of('/');
	if (slash != std::string::npos)
		c = c.substr(slash + 1);
	if (!c.empty() && (c[0] == '#' || c[0] == '@'))
		c = c.substr(1);
	if (c.empty())
		c = lower(mine);
	return c;
}

std::string ChatSource::make_config(obs_data_t *s)
{
	Data c = data_new();
	obs_data_t *o = c.get();
	auto b = [&](const char *k) { obs_data_set_bool(o, k, obs_data_get_bool(s, k)); };
	auto i = [&](const char *k) { obs_data_set_int(o, k, obs_data_get_int(s, k)); };
	auto t = [&](const char *k) { obs_data_set_string(o, k, str(s, k).c_str()); };
	auto col = [&](const char *k, bool alpha) { obs_data_set_string(o, k, css_color(obs_data_get_int(s, k), alpha).c_str()); };

	HubStatus st = Hub::get().status();
	obs_data_set_string(o, "channel", channel_of(s, "channel", st.login).c_str());
	std::string kick = channel_of(s, "kick", st.kick_login);
	obs_data_set_string(o, "kick", kick.c_str());
	// Kick's chat socket: the key Kick uses now (the hub keeps it current), and the signed-in
	// channel's ids, so the page needn't ask kick.com (which often blocks it) for them
	obs_data_set_string(o, "kick_key", st.kick_key.c_str());
	obs_data_set_string(o, "kick_cluster", st.kick_cluster.c_str());
	if (!kick.empty() && kick == lower(st.kick_login)) {
		obs_data_set_string(o, "kick_channel_id", st.kick_channel_id.c_str());
		obs_data_set_string(o, "kick_room", st.kick_room.c_str());
	}
	obs_data_set_bool(o, "signed_in", !st.login.empty());   // (Twitch's badge pictures need it)
	b("test");
	b("history");
	i("max");
	i("fade");
	t("order");
	t("align");
	t("anim");
	{
		Data f(obs_data_get_obj(s, "font"));
		obs_data_set_string(o, "font_face", f ? str(f.get(), "face").c_str() : "Segoe UI");
		obs_data_set_int(o, "font_size", f ? std::max<long long>(6, obs_data_get_int(f.get(), "size")) : 24);
		long long fl = f ? obs_data_get_int(f.get(), "flags") : 0;
		obs_data_set_bool(o, "bold", (fl & OBS_FONT_BOLD) != 0);
		obs_data_set_bool(o, "italic", (fl & OBS_FONT_ITALIC) != 0);
	}
	col("text_color", false);
	i("outline");
	col("outline_color", false);
	b("shadow");
	t("name_colors");
	col("name_color", false);
	b("readable");
	b("badges");
	b("pronouns");
	b("name_line");
	b("colon");
	t("bg");
	col("bg_color", true);
	i("radius");
	i("padding");
	i("gap");
	b("emotes_7tv");
	b("emotes_bttv");
	b("emotes_ffz");
	i("emote_scale");
	b("big_emotes");
	b("roles");
	col("role_color", false);
	b("mentions");
	col("mention_color", false);
	b("highlights");
	b("first");
	b("replies");
	b("me_color");
	t("hide_users");
	b("hide_commands");
	b("hide_links");
	t("words");
	t("words_mode");
	t("css");
	return obs_data_get_json(o);
}

void ChatSource::defaults(obs_data_t *s)
{
	obs_data_set_default_string(s, "channel", "");
	obs_data_set_default_string(s, "kick", "");
	obs_data_set_default_int(s, "width", 450);
	obs_data_set_default_int(s, "height", 700);
	obs_data_set_default_bool(s, "test", false);
	obs_data_set_default_bool(s, "history", false);
	obs_data_set_default_int(s, "max", 30);
	obs_data_set_default_int(s, "fade", 0);
	obs_data_set_default_string(s, "order", "up");
	obs_data_set_default_string(s, "align", "left");
	obs_data_set_default_string(s, "anim", "slide");
	Data f = data_new();
	obs_data_set_string(f.get(), "face", "Segoe UI");
	obs_data_set_string(f.get(), "style", "Semibold");
	obs_data_set_int(f.get(), "size", 24);
	obs_data_set_int(f.get(), "flags", 0);
	obs_data_set_default_obj(s, "font", f.get());
	obs_data_set_default_int(s, "text_color", 0xFFDEE4EC);
	obs_data_set_default_int(s, "outline", 3);
	obs_data_set_default_int(s, "outline_color", 0xFF050303);
	obs_data_set_default_bool(s, "shadow", true);
	obs_data_set_default_string(s, "name_colors", "twitch");
	obs_data_set_default_int(s, "name_color", 0xFFDBC1A6);
	obs_data_set_default_bool(s, "readable", true);
	obs_data_set_default_bool(s, "badges", true);
	obs_data_set_default_bool(s, "pronouns", false);
	obs_data_set_default_bool(s, "name_line", false);
	obs_data_set_default_bool(s, "colon", false);
	obs_data_set_default_string(s, "bg", "none");
	obs_data_set_default_int(s, "bg_color", 0xB0140F0F);
	obs_data_set_default_int(s, "radius", 8);
	obs_data_set_default_int(s, "padding", 8);
	obs_data_set_default_int(s, "gap", 6);
	obs_data_set_default_bool(s, "emotes_7tv", true);
	obs_data_set_default_bool(s, "emotes_bttv", true);
	obs_data_set_default_bool(s, "emotes_ffz", true);
	obs_data_set_default_int(s, "emote_scale", 100);
	obs_data_set_default_bool(s, "big_emotes", true);
	obs_data_set_default_bool(s, "roles", true);
	obs_data_set_default_int(s, "role_color", 0xFF3C48E0);
	obs_data_set_default_bool(s, "mentions", true);
	obs_data_set_default_int(s, "mention_color", 0xFF3CB4F0);
	obs_data_set_default_bool(s, "highlights", true);
	obs_data_set_default_bool(s, "first", true);
	obs_data_set_default_bool(s, "replies", true);
	obs_data_set_default_bool(s, "me_color", true);
	obs_data_set_default_string(s, "hide_users", DEFAULT_BOTS);
	obs_data_set_default_bool(s, "hide_commands", true);
	obs_data_set_default_bool(s, "hide_links", false);
	obs_data_set_default_string(s, "words", "");
	obs_data_set_default_string(s, "words_mode", "hide");
	obs_data_set_default_string(s, "css", "");
}

// ------------------------------------------------------------------ lifetime

static std::string new_id()
{
	static std::atomic<unsigned> n{0};
	std::random_device rd;
	char buf[32];
	snprintf(buf, sizeof buf, "%08x%04x", (unsigned)rd(), (n++) & 0xFFFF);
	return buf;
}

ChatSource::ChatSource(obs_data_t *settings, obs_source_t *source) : self_(source), sh_(std::make_shared<Shared>()), id_(new_id())
{
	{
		std::lock_guard<std::mutex> g(g_reg_mu);
		g_reg[id_] = sh_;
		g_weak[id_] = obs_source_get_weak_source(source);
	}
	if (chat_server::start())
		url_ = chat_server::base_url() + "/chat/index.html?id=" + id_ + "&k=" + chat_server::key();
	w_ = (uint32_t)std::clamp<long long>(obs_data_get_int(settings, "width"), 50, 8192);
	h_ = (uint32_t)std::clamp<long long>(obs_data_get_int(settings, "height"), 50, 8192);
	if (!url_.empty() && obs_get_source_output_flags("browser_source")) {
		Data b = data_new();
		obs_data_set_string(b.get(), "url", url_.c_str());
		obs_data_set_bool(b.get(), "is_local_file", false);
		obs_data_set_int(b.get(), "width", w_);
		obs_data_set_int(b.get(), "height", h_);
		obs_data_set_bool(b.get(), "reroute_audio", true);   // (it has none; this keeps it off the desktop)
		obs_data_set_bool(b.get(), "shutdown", false);
		obs_data_set_bool(b.get(), "restart_when_active", false);
		obs_data_set_int(b.get(), "webpage_control_level", 0);
		obs_data_set_string(b.get(), "css", "body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }");
		browser_ = obs_source_create_private("browser_source", "obspet chat page", b.get());
	}
	if (!browser_)
		warn("OBSPETS Chat needs OBS's Browser source (obs-browser), which isn't here");
	update(settings);
}

ChatSource::~ChatSource()
{
	{
		std::lock_guard<std::mutex> g(g_reg_mu);
		g_reg.erase(id_);
		auto it = g_weak.find(id_);
		if (it != g_weak.end()) {
			obs_weak_source_release(it->second);
			g_weak.erase(it);
		}
	}
	if (browser_)
		obs_source_release(browser_);
}

ChatSource *ChatSource::from(obs_source_t *s)
{
	const char *id = s ? obs_source_get_unversioned_id(s) : nullptr;
	if (!id || strcmp(id, CHAT_SOURCE_ID) != 0)
		return nullptr;
	return static_cast<ChatSource *>(obs_obj_get_data(s));
}

void ChatSource::update(obs_data_t *s)
{
	// (obs-websocket can set any number: a browser this big would take all the video memory)
	uint32_t w = (uint32_t)std::clamp<long long>(obs_data_get_int(s, "width"), 50, 8192);
	uint32_t h = (uint32_t)std::clamp<long long>(obs_data_get_int(s, "height"), 50, 8192);
	if (browser_ && (w != w_ || h != h_)) {
		Data b = data_new();
		obs_data_set_int(b.get(), "width", w);
		obs_data_set_int(b.get(), "height", h);
		obs_source_update(browser_, b.get());
	}
	w_ = w;
	h_ = h;
	std::string cfg = make_config(s);
	{
		std::lock_guard<std::mutex> g(sh_->mu);
		sh_->hide = lower(str(s, "hide_users"));
		if (cfg == sh_->config)
			return;
		sh_->config = cfg;
		sh_->last_edit_ns = os_gettime_ns();
	}
	Data d = data_json(cfg);
	send("obspet_config", d.get());
}

// Signing in (or out) later changes "your own channel" and the badges: look now and then.
void ChatSource::tick(float seconds)
{
	// A new status from the page: show it in the properties, but not while someone is typing in
	// them (rebuilding them would take the cursor out of the box). OBS sends an edit 0.5 s after the
	// last key, so 3 s without one means the typing has stopped.
	bool refresh = false;
	{
		std::lock_guard<std::mutex> g(sh_->mu);
		if (sh_->refresh_due && os_gettime_ns() - sh_->last_edit_ns >= 3000000000ULL) {
			sh_->refresh_due = false;
			refresh = true;
		}
	}
	if (refresh)
		refresh_properties();

	since_check_ += seconds;
	if (since_check_ < 2)
		return;
	since_check_ = 0;
	Data s(obs_source_get_settings(self_));
	update(s.get());
}

void ChatSource::render()
{
	if (browser_)
		obs_source_video_render(browser_);
}

void ChatSource::enum_active(obs_source_enum_proc_t cb, void *param)
{
	if (browser_)
		cb(self_, browser_, param);
}

void ChatSource::send(const char *event, obs_data_t *payload)
{
	if (!browser_)
		return;
	proc_handler_t *ph = obs_source_get_proc_handler(browser_);
	if (!ph)
		return;
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", event);
	calldata_set_string(&cd, "jsonString", payload ? obs_data_get_json(payload) : "{}");
	proc_handler_call(ph, "javascript_event", &cd);
	calldata_free(&cd);
}

// ------------------------------------------------------------------ for Pets

std::string ChatSource::hidden()
{
	std::lock_guard<std::mutex> g(sh_->mu);
	return sh_->hide;
}

bool ChatSource::layout(long long &top, long long &h, long long &seq)
{
	std::lock_guard<std::mutex> g(sh_->mu);
	if (!sh_->reported)
		return false;
	top = sh_->top;
	h = sh_->h;
	seq = sh_->seq;
	return true;
}

void ChatSource::ask_yank(long long req, const std::string &msg_id, const std::string &user, bool strike)
{
	Data d = data_new();
	obs_data_set_bool(d.get(), "strike", strike);   // draw the picture crossed out
	obs_data_set_int(d.get(), "req", req);
	obs_data_set_string(d.get(), "id", msg_id.c_str());
	obs_data_set_string(d.get(), "user", user.c_str());
	send("obspet_yank", d.get());
}

bool ChatSource::yank_reply(long long req, YankReply &out)
{
	std::lock_guard<std::mutex> g(sh_->mu);
	auto it = sh_->yanks.find(req);
	if (it == sh_->yanks.end())
		return false;
	out = it->second;
	return true;
}

void ChatSource::yank_done(long long req, bool took)
{
	{
		std::lock_guard<std::mutex> g(sh_->mu);
		sh_->yanks.erase(req);
	}
	Data d = data_new();
	obs_data_set_int(d.get(), "req", req);
	obs_data_set_bool(d.get(), "took", took);
	send("obspet_yank_done", d.get());
}

void ChatSource::strike(const std::string &msg_id)
{
	Data d = data_new();
	obs_data_set_string(d.get(), "id", msg_id.c_str());
	send("obspet_strike", d.get());
}

void ChatSource::strike_all(const std::string &msg_id)
{
	std::vector<obs_source_t *> all;
	{
		std::lock_guard<std::mutex> g(g_reg_mu);
		for (auto &[id, w] : g_weak)
			if (obs_source_t *s = obs_weak_source_get_source(w))
				all.push_back(s);
	}
	Data d = data_new();
	obs_data_set_string(d.get(), "id", msg_id.c_str());
	for (obs_source_t *s : all) {
		if (ChatSource *c = from(s))
			c->send("obspet_strike", d.get());
		obs_source_release(s);
	}
}

std::string ChatSource::add_test_line(const std::string &user, const std::string &text, const std::string &color)
{
	static std::atomic<int> n{0};
	std::string id = "test-" + std::to_string(++n);
	Data d = data_new();
	obs_data_set_string(d.get(), "id", id.c_str());
	obs_data_set_string(d.get(), "user", user.c_str());
	obs_data_set_string(d.get(), "text", text.c_str());
	obs_data_set_string(d.get(), "color", color.c_str());
	send("obspet_line", d.get());
	return id;
}

// ------------------------------------------------------------------ properties

static obs_properties_t *group(obs_properties_t *p, const char *id, const char *title)
{
	obs_properties_t *g = obs_properties_create();
	obs_properties_add_group(p, id, title, OBS_GROUP_NORMAL, g);
	return g;
}

static void tip(obs_property_t *p, const char *text) { obs_property_set_long_description(p, text); }

void ChatSource::refresh_properties() { refresh_props_of(id_); }

obs_properties_t *ChatSource::properties()
{
	obs_properties_t *p = obs_properties_create();
	Data s(obs_source_get_settings(self_));
	HubStatus st = Hub::get().status();
	std::string status;
	{
		std::lock_guard<std::mutex> g(sh_->mu);
		status = sh_->status;
	}

	std::string head =
		"Your Twitch and Kick chat on screen, made for OBSPETS: the pet sits on the newest message, gets bumped up by new "
		"ones, and rips a real message right out of the chat. (Pick this source as the pet's chat, or leave the "
		"pet on \"Find it by itself\".)";
	if (!browser_)
		head += "\n\nThis needs OBS's Browser source, which isn't installed in this OBS.";
	else if (!status.empty())
		head += "\n\n" + status;
	obs_properties_add_text(p, "about", head.c_str(), OBS_TEXT_INFO);

	obs_property_t *ch = obs_properties_add_text(p, "channel", "Twitch channel", OBS_TEXT_DEFAULT);
	tip(ch, !st.login.empty() ? ("Empty: your own channel (" + st.login + ").").c_str()
				  : "A Twitch channel's name. (Empty: yours, once you sign in with Twitch on an OBSPETS source.)");
	ch = obs_properties_add_text(p, "kick", "Kick channel", OBS_TEXT_DEFAULT);
	tip(ch, !st.kick_login.empty() ? ("Empty: your own channel (" + st.kick_login + ").").c_str()
				       : "A Kick channel's name. (Empty: yours, once you sign in with Kick on an OBSPETS source.)");
	obs_properties_add_int(p, "width", "Width", 100, 3840, 10);
	obs_properties_add_int(p, "height", "Height", 100, 2160, 10);
	tip(obs_properties_add_bool(p, "test", "Show test messages"),
	    "Fake messages for styling it. Turn off when live.");

	// ---- messages
	obs_properties_t *g = group(p, "g_msgs", "Messages");
	obs_property_t *l = obs_properties_add_list(g, "order", "Newest message", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(l, "At the bottom (messages move up)", "up");
	obs_property_list_add_string(l, "At the top (messages move down)", "down");
	tip(l, "With the newest at the top, the pet sits on the chat's top edge and isn't bumped by new messages.");
	l = obs_properties_add_list(g, "align", "Line up", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(l, "Left", "left");
	obs_property_list_add_string(l, "Right", "right");
	l = obs_properties_add_list(g, "anim", "New messages", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(l, "Slide in", "slide");
	obs_property_list_add_string(l, "Fade in", "fade");
	obs_property_list_add_string(l, "Pop in", "pop");
	obs_property_list_add_string(l, "Just appear", "none");
	obs_properties_add_int_slider(g, "max", "Messages kept on screen, at most", 1, 100, 1);
	obs_property_t *fd = obs_properties_add_int(g, "fade", "Hide each message after (0: never)", 0, 3600, 5);
	obs_property_int_set_suffix(fd, " s");
	tip(obs_properties_add_bool(g, "history", "Show the last messages when it loads"),
	    "From recent-messages.robotty.de, a free service most chat overlays use, so the chat isn't empty after "
	    "you start OBS or change a setting.");

	// ---- text
	g = group(p, "g_text", "Text");
	obs_properties_add_font(g, "font", "Font");
	obs_properties_add_color(g, "text_color", "Text color");
	obs_property_t *ol = obs_properties_add_int_slider(g, "outline", "Outline (0: none)", 0, 10, 1);
	obs_property_int_set_suffix(ol, " px");
	obs_properties_add_color(g, "outline_color", "Outline color");
	obs_properties_add_bool(g, "shadow", "Soft shadow");

	// ---- names
	g = group(p, "g_names", "Names");
	l = obs_properties_add_list(g, "name_colors", "Name colors", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(l, "Each chatter's Twitch color", "twitch");
	obs_property_list_add_string(l, "One color for everyone", "one");
	obs_properties_add_color(g, "name_color", "Name color (one color, or for chatters who never picked one)");
	tip(obs_properties_add_bool(g, "readable", "Keep Twitch colors readable"),
	    "Lightens dark name colors on light text, and darkens light ones on dark text, so every name stands out.");
	tip(obs_properties_add_bool(g, "badges", "Badges (broadcaster, mod, VIP, sub, ...)"),
	    "Badge pictures come from Twitch with your sign-in. Without signing in, the chat marks the broadcaster, "
	    "mods and VIPs with a colored bar instead.");
	tip(obs_properties_add_bool(g, "pronouns", "Pronouns next to names"),
	    "From pronouns.alejo.io, for chatters who set theirs there.");
	obs_properties_add_bool(g, "colon", "A colon after the name");
	obs_properties_add_bool(g, "name_line", "Name on its own line, above the message");

	// ---- background
	g = group(p, "g_bg", "Background");
	l = obs_properties_add_list(g, "bg", "Background", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(l, "None (just the text)", "none");
	obs_property_list_add_string(l, "A box behind each message", "box");
	obs_property_list_add_string(l, "One panel behind all of them", "panel");
	obs_properties_add_color_alpha(g, "bg_color", "Background color");
	obs_property_t *ra = obs_properties_add_int_slider(g, "radius", "Rounded corners", 0, 40, 1);
	obs_property_int_set_suffix(ra, " px");
	obs_property_t *pa = obs_properties_add_int_slider(g, "padding", "Space inside the box", 0, 40, 1);
	obs_property_int_set_suffix(pa, " px");
	obs_property_t *ga = obs_properties_add_int_slider(g, "gap", "Space between messages", 0, 40, 1);
	obs_property_int_set_suffix(ga, " px");

	// ---- emotes
	g = group(p, "g_emotes", "Emotes");
	obs_properties_add_bool(g, "emotes_7tv", "7TV emotes");
	obs_properties_add_bool(g, "emotes_bttv", "BetterTTV emotes");
	obs_properties_add_bool(g, "emotes_ffz", "FrankerFaceZ emotes");
	obs_property_t *es = obs_properties_add_int_slider(g, "emote_scale", "Emote size", 50, 300, 10);
	obs_property_int_set_suffix(es, " %");
	obs_properties_add_bool(g, "big_emotes", "Bigger emotes in a message that's only emotes");

	// ---- highlights
	g = group(p, "g_marks", "Highlights");
	obs_properties_add_bool(g, "roles", "Mark the broadcaster, mods and VIPs");
	obs_properties_add_color(g, "role_color", "Their mark's color");
	tip(obs_properties_add_bool(g, "mentions", "Highlight messages that mention you"),
	    "Messages with @yourname, or your name, in them.");
	obs_properties_add_color(g, "mention_color", "Mention color");
	obs_properties_add_bool(g, "highlights", "Highlight \"Highlight my message\" channel-point messages");
	obs_properties_add_bool(g, "first", "Mark first-time chatters");
	obs_properties_add_bool(g, "replies", "Show who a reply is to");
	obs_properties_add_bool(g, "me_color", "/me messages in the chatter's color");

	// ---- what it hides
	g = group(p, "g_filter", "What it hides");
	tip(obs_properties_add_text(g, "hide_users", "Hide these chatters (bots), separated by commas", OBS_TEXT_DEFAULT),
	    "Usernames, not display names.");
	obs_properties_add_bool(g, "hide_commands", "Hide commands (messages starting with !)");
	obs_properties_add_bool(g, "hide_links", "Hide messages with links");
	tip(obs_properties_add_text(g, "words", "Words to filter, separated by commas", OBS_TEXT_DEFAULT),
	    "Whole words, any case. Twitch's and Kick's own AutoMod and blocked terms still apply before this.");
	l = obs_properties_add_list(g, "words_mode", "A message with one of them", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(l, "Isn't shown", "hide");
	obs_property_list_add_string(l, "Is shown with the word as ***", "mask");

	// ---- custom CSS
	g = group(p, "g_css", "Custom CSS");
	obs_property_t *css = obs_properties_add_text(g, "css", "CSS", OBS_TEXT_MULTILINE);
	tip(css, "Applies on top of everything above. Classes: .msg (one message; also .mod .vip .host .sub .first "
		 ".highlight .mention .action .reply .big, and .struck: deleted by Auto mod, waiting for the pet), "
		 ".badges, .badge, .pronouns, .name, .colon, .text, .emote, .reply-to. #list holds the messages.");
	obs_properties_add_text(g, "css_note",
				"Custom CSS can change anything about the look. Keep messages inside #list, or the pet can't "
				"find them.",
				OBS_TEXT_INFO);
	return p;
}

// ------------------------------------------------------------------ registration

void register_chat_source()
{
	obs_source_info si = {};
	si.id = CHAT_SOURCE_ID;
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	si.icon_type = OBS_ICON_TYPE_TEXT;
	si.get_name = [](void *) { return "OBSPETS Chat"; };
	si.create = [](obs_data_t *s, obs_source_t *src) -> void * { return new ChatSource(s, src); };
	si.destroy = [](void *d) { delete static_cast<ChatSource *>(d); };
	si.get_defaults = [](obs_data_t *s) { ChatSource::defaults(s); };
	si.get_properties = [](void *d) -> obs_properties_t * {
		return d ? static_cast<ChatSource *>(d)->properties() : obs_properties_create();
	};
	si.update = [](void *d, obs_data_t *s) { static_cast<ChatSource *>(d)->update(s); };
	si.video_render = [](void *d, gs_effect_t *) { static_cast<ChatSource *>(d)->render(); };
	si.get_width = [](void *d) { return static_cast<ChatSource *>(d)->width(); };
	si.get_height = [](void *d) { return static_cast<ChatSource *>(d)->height(); };
	si.enum_active_sources = [](void *d, obs_source_enum_proc_t cb, void *param) {
		static_cast<ChatSource *>(d)->enum_active(cb, param);
	};
	si.video_tick = [](void *d, float t) { static_cast<ChatSource *>(d)->tick(t); };
	obs_register_source(&si);
}

} // namespace obspet
