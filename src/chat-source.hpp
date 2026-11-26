// OBSPETS Chat: an on-screen Twitch and Kick chat overlay. It is a page (data/chat/) in a private
// browser source, with the usual overlay options. Unlike other overlays it reports where its
// messages are, so a Pet can sit on the newest one and rip the real line out when it yanks one.
//
// Talking to the page: the plugin -> page with obs-browser's `javascript_event` (events named
// obspet_*), the page -> plugin over the loopback server (chat-server.hpp).
//
// Threads: the OBS callbacks as usual; a Pet calls the "for Pets" methods from the graphics thread;
// the server thread writes what the page reports into `Shared`, under its mutex.
#pragma once

#include "util.hpp"

#include <obs-module.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace obspet {

// What a page answered when a Pet asked about a message it is about to yank.
struct YankReply {
	bool found = false;
	double fx = 0.5, fy = 0.9;   // its middle, as fractions of the page
	double page_w = 0;           // the page's width in px (the picture is in these px)
	std::string text, img;       // the line as text, and as a picture in the chat's own look (base64 PNG)
};

class ChatSource {
public:
	ChatSource(obs_data_t *settings, obs_source_t *source);
	~ChatSource();

	void update(obs_data_t *s);
	void render();
	void tick(float seconds);
	obs_source_t *self() const { return self_; }
	uint32_t width() const { return w_; }
	uint32_t height() const { return h_; }
	obs_properties_t *properties();
	static void defaults(obs_data_t *s);
	void enum_active(obs_source_enum_proc_t cb, void *param);

	// for Pets (any thread)
	static ChatSource *from(obs_source_t *s);   // nullptr unless it's an OBSPETS Chat
	// The top of the messages, in px up from the bottom of the page, the page's height, and a count
	// of messages shown so far. False until the page has reported.
	bool layout(long long &top, long long &h, long long &seq);
	void ask_yank(long long req, const std::string &msg_id, const std::string &user, bool strike = false);
	bool yank_reply(long long req, YankReply &out);   // false: no answer yet
	void yank_done(long long req, bool took);         // took: the pet has it, so the line goes
	std::string hidden();   // the chatters it hides (bots), lowercased and comma separated: its Pet ignores them too
	std::string add_test_line(const std::string &user, const std::string &text, const std::string &color);
	// Auto mod deleted this message on its platform: every OBSPETS Chat keeps the line, crossed out,
	// for a Pet to rip out (and drops it by itself if none does). Any thread.
	static void strike_all(const std::string &msg_id);
	void strike(const std::string &msg_id);   // the same, on this chat only (the Pet's "Try a reaction")

	struct Shared {
		std::mutex mu;
		std::string config;   // JSON the page asks for when it loads
		bool reported = false;
		long long top = 0, h = 0, seq = 0;
		std::string status;   // the page's own words: connected, can't connect, ...
		std::string hide;     // hide_users, lowercased
		// A new status shows in the properties, but rebuilding them takes the cursor out of the
		// box being typed in (and each edit makes the page reconnect, so a new status follows every
		// letter). So it only marks them due, and tick() rebuilds them once the edits have stopped.
		bool refresh_due = false;
		uint64_t last_edit_ns = 0;
		std::map<long long, YankReply> yanks;
	};

private:
	void send(const char *event, obs_data_t *payload);
	std::string make_config(obs_data_t *s);
	void refresh_properties();

	obs_source_t *self_ = nullptr, *browser_ = nullptr;
	std::shared_ptr<Shared> sh_;
	std::string id_;
	uint32_t w_ = 450, h_ = 700;
	float since_check_ = 0;
	std::string url_;
};

void register_chat_source();
extern const char *CHAT_SOURCE_ID;

} // namespace obspet
