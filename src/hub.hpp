// The hub: what every Pet source shares. One Stream Tools account (sindruid.com/streamtools), signed in
// with Twitch, Kick or both; one connection to each; and one chat-mood engine for the whole OBS.
// Events from either platform go out to every Pet the same way; each Pet decides with its own settings
// whether to react.
//
// Threads: a worker (sign-in, config, deletes, rewards, badges), a mood thread (asks sindruid.com
// about chat, which can take seconds, so it never holds up the worker), a Twitch thread (EventSub)
// and a Kick thread (Kick's chat socket, Pusher). Pets talk to the hub from OBS's threads; the hub
// calls PetClient::hub_event from its own threads.
#pragma once

#include "util.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace obspet {

struct HubEvent {
	enum class T { Alert, Redeem, Chat, Mood } t = T::Alert;
	std::string kind;    // Alert: follow / sub / gift / bits / raid. Mood: a mood from MOOD.
	int n = 1;           // how many (gifts, bits or Kicks, raiders)
	std::string user, color, text;
	std::string title;   // Redeem: the reward's title
	std::string msg_id;  // Mood "yank": the chat message
	bool remove = false; // Auto mod caught it: it's deleted already, so the pet flings it crossed out
};

// What the mood engine needs from a Pet. One Pet acts out each chat reaction: one that's on screen.
struct MoodProfile {
	bool on = false, allow_delete = true;
	bool automod_only = false;   // no chat reactions: chat is only checked for what Auto mod deletes
	bool show_deletes = true;    // a pet rips what Auto mod deletes out of the chat on stream (else it just goes)
	double gap = 15;     // s it lets chat be after a reaction
	std::string name, describe, pronouns, personality, state;
	std::vector<std::string> ignore;   // chatters it never reacts to (bots), lower case
	std::vector<std::string> skip;     // chat reactions it's set not to do (the model doesn't offer them)
};

// What the AI makes of one line the streamer typed into the settings page's chat tester.
struct ChatTry {
	std::string error;        // set: it couldn't ask (not signed in, paused, no credit, ...): say this
	std::string kind = "none", why;
	bool del = false;         // Auto mod would delete the message
	MoodProfile prof;         // the pet it asked for (its switches: on, automod_only, allow_delete)
};

class PetClient {
public:
	virtual ~PetClient() = default;
	virtual void hub_event(const HubEvent &e) = 0;   // any thread
	virtual void hub_status_changed() = 0;           // any thread
	virtual bool on_screen() const = 0;              // any thread: shown in the program right now
};

// The channel-point reward that pets it, on one platform, as far as this OBS knows.
enum class Reward { Unknown, Checking, Missing, There, Made, NotAllowed, Failed };

struct HubStatus {
	bool signed_in = false, linking = false;
	std::string link_platform;            // "twitch" or "kick", while signing in
	std::string login, kick_login;        // signed in with Twitch as / with Kick as ("" = not)
	bool twitch_live = false, kick_live = false;
	std::string code, link_url, message;   // link_url: the sign-in page's short address, to type in
	// what a sign-in made before a feature needed it can't do yet ("" = all there): signing in with
	// that platform again turns it on
	std::string twitch_missing, kick_missing;
	// Kick's chat socket, for OBSPETS Chat: the key and cluster Kick uses now, and the signed-in
	// channel's ids (so the page needn't look them up on kick.com)
	std::string kick_key, kick_cluster, kick_channel_id, kick_room;
	bool moods_server_on = true;
	std::string mood_note;    // e.g. "paused: this month's budget is used up"
	std::string reward_title;   // the reward the states below are about
	Reward reward = Reward::Unknown, kick_reward = Reward::Unknown;
	std::string reward_msg, kick_reward_msg;   // what went wrong, when it did
	// Auto mod, for the whole account (sindruid.com keeps it): on or off, and what it deletes
	bool automod_on = true;
	std::vector<std::string> automod;
	double ai_gap = 5;   // the account asks the AI about chat at most this often (s)
	// the account's own React to chat switch (moods_server_on is also off while the service is paused)
	// and its daily limit on reactions
	bool react_on = true;
	int daily_cap = 0;
};

class Hub {
public:
	static Hub &get();
	void start();
	void stop();

	void add(PetClient *p);
	void remove(PetClient *p);
	void set_profile(PetClient *p, const MoodProfile &m);

	HubStatus status();
	std::string api_token();   // this OBS's Stream Tools token ("" = signed out)
	void sign_in(const std::string &platform);   // "twitch" or "kick": signs in, or adds it to the account
	void sign_out();
	void cancel_sign_in();
	void open_sign_in_page();   // the browser again, at the page for the current code
	void delete_message(const std::string &msg_id);   // a message Auto mod caught, now (on its own platform)
	bool gone(const std::string &msg_id);   // deleted from chat by a mod, a ban or a clear (not by the pet): never show it again
	void set_automod(bool on, const std::vector<std::string> &cats);   // saves Auto mod for the account
	// saves the account's reactions to chat: on or off, the time between them (s), a daily limit
	void set_reacting(bool on, int gap_s, int daily_cap);
	void check_reward(const std::string &title, bool again = false);   // looks whether the channel-point reward is there (again: even if known)
	void ensure_reward(const std::string &title);     // makes the channel-point reward where it's missing
	// The chat tester: how `pet` would take `text` from a viewer, asked right away (blocks up to
	// ~5 s; not the UI thread). Nothing is deleted and the pet does nothing: the page shows the answer.
	ChatTry try_chat(PetClient *pet, const std::string &user, const std::string &text);
	std::string chat_badges(const std::string &channel);   // OBSPETS Chat's badge pictures (blocks; not the UI thread)

	static std::string backend();   // https://sindruid.com, or OBSPET_BACKEND for testing

private:
	// One platform's sign-in: its tokens (renewed through sindruid.com) and who it is.
	struct Platform {
		std::string client_id, access, refresh, user_id, login;
		double expires_at = 0;                 // wall clock (s since epoch)
		std::string channel_id, chatroom_id;   // Kick: the ids its chat socket needs
		bool on() const { return !access.empty(); }
	};
	struct Account {
		std::string api_token;
		Platform twitch, kick;
		bool valid() const { return !api_token.empty(); }
	};
	struct Line {
		std::string user, text, color, id, platform;
		bool trusted = false, is_new = true;
	};
	struct RewardState {
		Reward r = Reward::Unknown;
		std::string msg;
	};

	Platform &plat(const std::string &pf) { return pf == "kick" ? acct_.kick : acct_.twitch; }   // (with m_ held)
	std::vector<std::string> platforms_on();   // the platforms signed in

	void worker_loop();
	void mood_loop();
	void twitch_loop();
	void kick_loop();
	void post_task(std::function<void()> f);
	void notify();
	void broadcast(const HubEvent &e);
	void load_account();
	void load_account_from(obs_data_t *d);
	void save_account();
	bool refresh(const std::string &pf, const std::string &stale);   // worker or a platform's thread
	void drop(const std::string &pf, const std::string &why);        // forget one platform's sign-in
	long helix(const std::string &method, const std::string &path, const std::string &body, std::string *out);
	long kick_api(const std::string &method, const std::string &path, const std::string &body, std::string *out);
	void poll_link();
	void fetch_config();
	void mood_tick();
	void hear(const std::string &pf, const std::string &user, const std::string &text, const std::string &color,
		  const std::string &id, bool trusted);
	void handle_notification(obs_data_t *payload);
	bool subscribe_all(const std::string &session);
	std::string chat_color(const std::string &user_id);
	bool kick_ids();   // the channel's ids for Kick's chat socket, from kick.com
	void check_scopes(const std::string &pf);   // what the platform's token may do: sets *_missing_
	PetClient *mood_pet();   // (with m_ held) the Pet that acts out chat reactions: one with them on, on screen if any (else an Auto-mod-only one)
	void kick_event(const std::string &event, const std::string &data);
	// A message (id), everything a chatter said (user), or all of chat (both empty) was deleted.
	void mark_gone(const std::string &id, const std::string &user);
	Reward look_reward(const std::string &pf, const std::string &title);
	void make_reward(const std::string &pf, const std::string &title);

	std::mutex m_, refresh_m_;
	std::condition_variable cv_;
	std::thread worker_, mood_, twitch_, kick_;
	std::atomic<bool> stop_{false}, running_{false};
	std::deque<std::function<void()>> tasks_;
	std::set<PetClient *> pets_;
	std::map<PetClient *, MoodProfile> profiles_;

	Account acct_;
	// bump when that platform's sign-in changes, so its thread reconnects
	std::atomic<int> twitch_gen_{0}, kick_gen_{0};
	// device sign-in in progress
	std::string link_device_, link_code_, link_url_, link_url_full_, link_platform_;
	double link_expires_ = 0, link_next_ = 0, link_every_ = 5;
	std::string message_;
	bool twitch_live_ = false, kick_live_ = false;
	int twitch_fails_ = 0, kick_fails_ = 0;   // connections in a row that didn't work
	std::string twitch_missing_, kick_missing_;   // see HubStatus
	std::atomic<bool> scopes_due_{true};          // look at what the tokens may do (start, sign-in, refresh)
	std::string kick_key_, kick_cluster_;         // Kick's chat socket, as kick.com last said
	// the channel-point reward, per platform
	std::string reward_title_;
	std::map<std::string, RewardState> rewards_;
	double reward_checked_ = -1e9;   // when it last looked (a failed look waits a minute)
	void set_reward(const std::string &pf, const std::string &title, Reward r, const std::string &msg = "");
	void signed_out(const std::string &why);   // forget the account, say why
	// the account's settings, kept by sindruid.com (changed on the settings page)
	bool cfg_moods_on_ = true, cfg_allow_delete_ = true;
	std::vector<std::string> cfg_automod_{"bots", "scams", "slurs"};   // (the site's defaults, until it says)
	double cfg_gap_ = 5, cfg_at_ = -1e9;
	bool cfg_react_on_ = true;
	int cfg_daily_cap_ = 0;
	// moods
	std::deque<Line> lines_;
	double last_ask_ = -1e9, paused_until_ = 0, last_react_at_ = -1e9;
	std::string last_kind_, mood_note_;
	std::map<std::string, std::string> yanked_;       // message id -> its platform, until deleted
	std::map<std::string, std::string> colors_;       // user id -> chat color
	std::map<std::string, std::pair<double, std::string>> badges_;   // channel -> (when, badge JSON)
	std::set<std::string> kick_gifts_;   // Kick sends a big gift in chunks: each counts once
	struct Heard {
		std::string id, user;   // user: lower case
	};
	std::deque<Heard> recent_;      // the last few hundred messages, to know whose a ban deletes
	std::set<std::string> gone_;    // ids of deleted messages
	std::set<std::string> ours_;    // the ones the pet itself deleted (it's still flinging those)
};

} // namespace obspet
