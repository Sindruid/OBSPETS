#include "hub.hpp"
#include "chat-server.hpp"
#include "chat-source.hpp"
#include "net.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <future>
#include <thread>

namespace obspet {

static const char *HELIX = "https://api.twitch.tv/helix";
static const char *EVENTSUB = "wss://eventsub.wss.twitch.tv/ws";
static const char *KICK_API = "https://api.kick.com/public/v1";
// Kick's chat socket (Pusher). Kick changes the key now and then: kick_loop asks kick.com for the
// current one, and uses this one if it can't.
static const char *KICK_PUSHER_KEY = "34bf7a0ff419a2a775b9";
static const char *KICK_PUSHER_CLUSTER = "us2";
static const double CONFIG_EVERY = 300;   // s between fetching the page's settings

static double wall() { return (double)time(nullptr); }

// An id or a channel name that goes into an API's address or a JSON string as it is: only letters,
// digits, '-' and '_' (every real one), so nothing in it can change the request.
static bool safe_id(const std::string &s, size_t max = 64)
{
	if (s.empty() || s.size() > max)
		return false;
	for (unsigned char c : s)
		if (!isalnum(c) && c != '-' && c != '_')
			return false;
	return true;
}
// A wss:// address on twitch.tv (where EventSub may send us to reconnect)
static bool twitch_wss(const std::string &url)
{
	if (url.rfind("wss://", 0) != 0)
		return false;
	size_t end = url.find_first_of("/:?#", 6);
	std::string host = lower(url.substr(6, end == std::string::npos ? std::string::npos : end - 6));
	const std::string tv = ".twitch.tv";
	return host.size() > tv.size() && host.compare(host.size() - tv.size(), tv.size(), tv) == 0 &&
	       host.find('@') == std::string::npos;
}
// A count from an event (bits, gifts, raiders): never negative, never absurd.
static int count_of(long long n) { return (int)std::clamp<long long>(n, 0, 1000000); }
static std::string pname(const std::string &pf) { return pf == "kick" ? "Kick" : "Twitch"; }

static const char *CANT_LISTEN = "Can't connect to your Twitch channel right now.";
static const char *CANT_LISTEN_KICK = "Can't connect to your Kick channel right now.";
static const char *UNLINKED = "You were signed out: this OBS was removed from your account on sindruid.com. To sign in again, "
			     "click Sign in with Twitch or Sign in with Kick.";
static const char *TOO_LONG = "Sign-in took too long, so it stopped. Click Sign in with Twitch or Sign in with Kick to start again.";

static void nap(double s, const std::atomic<bool> &stop, std::function<bool()> go_on = nullptr)
{
	for (int i = 0; i < s * 4 && !stop && (!go_on || go_on()); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

Hub &Hub::get()
{
	static Hub h;
	return h;
}

std::string Hub::backend()
{
	const char *b = getenv("OBSPET_BACKEND");
	return b && *b ? b : "https://sindruid.com";
}

void Hub::start()
{
	if (running_.exchange(true))
		return;
	stop_ = false;
	net_init();
	load_account();
	scopes_due_ = true;
	{
		std::lock_guard<std::mutex> g(m_);
		kick_key_ = KICK_PUSHER_KEY;
		kick_cluster_ = KICK_PUSHER_CLUSTER;
	}
	worker_ = std::thread([this] { worker_loop(); });
	mood_ = std::thread([this] { mood_loop(); });
	twitch_ = std::thread([this] { twitch_loop(); });
	kick_ = std::thread([this] { kick_loop(); });
}

void Hub::stop()
{
	if (!running_.exchange(false))
		return;
	stop_ = true;
	net_abort();   // (a request in flight ends now, so OBS never waits on the network to close)
	cv_.notify_all();
	for (auto *t : {&worker_, &mood_, &twitch_, &kick_})
		if (t->joinable())
			t->join();
	net_shutdown();
}

void Hub::add(PetClient *p)
{
	std::lock_guard<std::mutex> g(m_);
	pets_.insert(p);
}

void Hub::remove(PetClient *p)
{
	std::lock_guard<std::mutex> g(m_);
	pets_.erase(p);
	profiles_.erase(p);
}

void Hub::set_profile(PetClient *p, const MoodProfile &m)
{
	std::lock_guard<std::mutex> g(m_);
	profiles_[p] = m;
}

// A streamer may have several Pets (one per scene, say): the one acting out chat must be one people
// can see. Any with chat reactions on that's in the program now; else an Auto-mod-only one (it only
// asks about spam, so nothing is spent on reactions nobody sees); else any with them on.
PetClient *Hub::mood_pet()
{
	PetClient *any = nullptr, *mod_only = nullptr;
	for (auto &kv : profiles_) {
		if (!kv.second.on)
			continue;
		if (kv.second.automod_only) {
			if (!mod_only)
				mod_only = kv.first;
			continue;
		}
		if (kv.first->on_screen())
			return kv.first;
		if (!any)
			any = kv.first;
	}
	return mod_only ? mod_only : any;
}

HubStatus Hub::status()
{
	std::lock_guard<std::mutex> g(m_);
	HubStatus s;
	s.signed_in = acct_.valid();
	s.login = acct_.twitch.on() ? acct_.twitch.login : "";
	s.kick_login = acct_.kick.on() ? acct_.kick.login : "";
	s.linking = !link_device_.empty();
	s.link_platform = link_platform_;
	s.code = link_code_;
	s.link_url = link_url_;
	s.message = message_;
	s.twitch_live = twitch_live_;
	s.kick_live = kick_live_;
	s.moods_server_on = cfg_moods_on_;
	s.automod_on = cfg_allow_delete_;
	s.automod = cfg_automod_;
	s.ai_gap = cfg_gap_;
	s.react_on = cfg_react_on_;
	s.daily_cap = cfg_daily_cap_;
	s.mood_note = mood_note_;
	s.reward_title = reward_title_;
	s.reward = rewards_["twitch"].r;
	s.reward_msg = rewards_["twitch"].msg;
	s.kick_reward = rewards_["kick"].r;
	s.kick_reward_msg = rewards_["kick"].msg;
	s.twitch_missing = acct_.twitch.on() ? twitch_missing_ : "";
	s.kick_missing = acct_.kick.on() ? kick_missing_ : "";
	s.kick_key = kick_key_;
	s.kick_cluster = kick_cluster_;
	if (acct_.kick.on()) {
		s.kick_channel_id = acct_.kick.channel_id;
		s.kick_room = acct_.kick.chatroom_id;
	}
	return s;
}

std::vector<std::string> Hub::platforms_on()
{
	std::lock_guard<std::mutex> g(m_);
	std::vector<std::string> out;
	if (acct_.valid() && acct_.twitch.on())
		out.push_back("twitch");
	if (acct_.valid() && acct_.kick.on())
		out.push_back("kick");
	return out;
}

void Hub::post_task(std::function<void()> f)
{
	{
		std::lock_guard<std::mutex> g(m_);
		tasks_.push_back(std::move(f));
	}
	cv_.notify_all();
}

// (under the lock, like broadcast: once remove() returns, no hub thread is inside a Pet that's
// being destroyed. hub_status_changed only sets a flag, and never calls back into the hub.)
void Hub::notify()
{
	std::lock_guard<std::mutex> g(m_);
	for (auto *p : pets_)
		p->hub_status_changed();
}

void Hub::broadcast(const HubEvent &e)
{
	std::lock_guard<std::mutex> g(m_);
	for (auto *p : pets_)
		p->hub_event(e);
}

// ------------------------------------------------------------------ the account

// account.json is {"protected": "<base64>"}: the account's JSON, encrypted for this Windows user
// (DPAPI). Before 0.5.1 it was the JSON itself; such a file is read once and saved encrypted.
void Hub::load_account()
{
	std::string path = config_path("account.json");
	Data f(obs_data_create_from_json_file(path.c_str()));
	if (!f)
		return;
	Data d;
	bool plain = !obs_data_has_user_value(f.get(), "protected");
	if (plain) {
		d = std::move(f);
	} else {
		std::string json;
		if (!unprotect(str(f.get(), "protected"), json)) {
			// (copied from another PC or Windows user: it can't be read here, so this OBS signs in again)
			warn("account: the sign-in was saved by another Windows user or PC, so it can't be read here");
			return;
		}
		d = Data(obs_data_create_from_json(json.c_str()));
		if (!d)
			return;
	}
	load_account_from(d.get());
	if (plain) {
		save_account();
		os_unlink((path + ".bak").c_str());   // (the plain copy obs_data_save_json_safe kept)
	}
}

void Hub::load_account_from(obs_data_t *src)
{
	obs_data_addref(src);
	Data d(src);
	std::lock_guard<std::mutex> g(m_);
	acct_.api_token = str(d.get(), "api_token");
	// Twitch at the top (as 0.3 kept it), Kick in its own object
	acct_.twitch.client_id = str(d.get(), "client_id");
	acct_.twitch.access = str(d.get(), "access_token");
	acct_.twitch.refresh = str(d.get(), "refresh_token");
	acct_.twitch.user_id = str(d.get(), "user_id");
	acct_.twitch.login = str(d.get(), "login");
	acct_.twitch.expires_at = obs_data_get_double(d.get(), "expires_at");
	Data k(obs_data_get_obj(d.get(), "kick"));
	if (k) {
		acct_.kick.access = str(k.get(), "access_token");
		acct_.kick.refresh = str(k.get(), "refresh_token");
		acct_.kick.user_id = str(k.get(), "user_id");
		acct_.kick.login = str(k.get(), "login");
		acct_.kick.expires_at = obs_data_get_double(k.get(), "expires_at");
		acct_.kick.channel_id = str(k.get(), "channel_id");
		acct_.kick.chatroom_id = str(k.get(), "chatroom_id");
	}
}

// The account file sits in OBS's per-user plugin config, outside scene collections (so it never
// ends up in a scene export or a repo).
void Hub::save_account()
{
	Account a;
	{
		std::lock_guard<std::mutex> g(m_);
		a = acct_;
	}
	std::string path = config_path("account.json");
	if (!a.valid()) {
		os_unlink(path.c_str());
		return;
	}
	os_mkdirs(config_path("").c_str());
	Data d = data_new();
	obs_data_set_string(d.get(), "api_token", a.api_token.c_str());
	if (a.twitch.on()) {
		obs_data_set_string(d.get(), "client_id", a.twitch.client_id.c_str());
		obs_data_set_string(d.get(), "access_token", a.twitch.access.c_str());
		obs_data_set_string(d.get(), "refresh_token", a.twitch.refresh.c_str());
		obs_data_set_string(d.get(), "user_id", a.twitch.user_id.c_str());
		obs_data_set_string(d.get(), "login", a.twitch.login.c_str());
		obs_data_set_double(d.get(), "expires_at", a.twitch.expires_at);
	}
	if (a.kick.on()) {
		Data k = data_new();
		obs_data_set_string(k.get(), "access_token", a.kick.access.c_str());
		obs_data_set_string(k.get(), "refresh_token", a.kick.refresh.c_str());
		obs_data_set_string(k.get(), "user_id", a.kick.user_id.c_str());
		obs_data_set_string(k.get(), "login", a.kick.login.c_str());
		obs_data_set_double(k.get(), "expires_at", a.kick.expires_at);
		obs_data_set_string(k.get(), "channel_id", a.kick.channel_id.c_str());
		obs_data_set_string(k.get(), "chatroom_id", a.kick.chatroom_id.c_str());
		obs_data_set_obj(d.get(), "kick", k.get());
	}
	std::string sealed = protect(obs_data_get_json(d.get()));
	if (sealed.empty()) {
		warn("account: Windows couldn't encrypt the sign-in, so it isn't saved (signing in again is needed after OBS restarts)");
		os_unlink(path.c_str());
		return;
	}
	d = data_new();
	obs_data_set_string(d.get(), "protected", sealed.c_str());
	obs_data_save_json_safe(d.get(), path.c_str(), "tmp", "bak");
}

// Signs in with a platform, or, already signed in, adds it to the account: the same device sign-in
// with this install's API token, so the platform joins the account this OBS is in.
void Hub::sign_in(const std::string &platform)
{
	std::string pf = platform == "kick" ? "kick" : "twitch";
	post_task([this, pf] {
		std::string tok;
		{
			std::lock_guard<std::mutex> g(m_);
			tok = acct_.api_token;
		}
		Data body = data_new();
		std::string name = "OBS on " + computer_name();
		obs_data_set_string(body.get(), "product", "obspet");   // (a Stream Tools account serves several products)
		obs_data_set_string(body.get(), "platform", pf.c_str());
		obs_data_set_string(body.get(), "device_name", name.c_str());
		std::vector<std::string> hs = {"Content-Type: application/json"};
		if (!tok.empty())
			hs.push_back("Authorization: Bearer " + tok);
		HttpResult r = http("POST", backend() + "/streamtools/api/device/start", hs, obs_data_get_json(body.get()));
		Data d = data_json(r.body);
		if (r.status == 401) {
			signed_out(UNLINKED);
			return;
		}
		if (!r.ok() || !d) {
			warn("sign-in: sindruid.com answered %ld %s", r.status, r.error.c_str());
			std::string why = d ? str(d.get(), "message") : "";
			std::lock_guard<std::mutex> g(m_);
			message_ = !why.empty() && r.status ? why : "Couldn't reach sindruid.com. Check that this PC is online, then try again.";
		} else {
			std::string url = str(d.get(), "verification_uri_complete");
			{
				std::lock_guard<std::mutex> g(m_);
				link_device_ = str(d.get(), "device_code");
				link_code_ = str(d.get(), "user_code");
				link_url_ = str(d.get(), "verification_uri");
				link_url_full_ = url;
				link_platform_ = pf;
				link_every_ = std::max(2.0, (double)obs_data_get_int(d.get(), "interval"));
				link_expires_ = now_s() + (double)obs_data_get_int(d.get(), "expires_in");
				link_next_ = now_s() + link_every_;
				message_.clear();
			}
			if (!getenv("OBSPET_NO_BROWSER"))   // (tests)
				open_url(url);
		}
		notify();
	});
}

void Hub::sign_out()
{
	post_task([this] {
		std::string tok;
		{
			std::lock_guard<std::mutex> g(m_);
			tok = acct_.api_token;
			acct_ = Account();
			link_device_.clear();
			link_code_.clear();
			message_ = "Signed out. The pet still walks about, but won't react to your channel until you sign in again.";
			rewards_.clear();
			reward_title_.clear();
		}
		if (!tok.empty())
			http("POST", backend() + "/streamtools/api/device/signout", {"Authorization: Bearer " + tok});
		twitch_gen_++;
		kick_gen_++;
		save_account();
		notify();
	});
}

void Hub::cancel_sign_in()
{
	post_task([this] {
		{
			std::lock_guard<std::mutex> g(m_);
			link_device_.clear();
			link_code_.clear();
			message_.clear();
		}
		notify();
	});
}

void Hub::open_sign_in_page()
{
	std::string url;
	{
		std::lock_guard<std::mutex> g(m_);
		url = link_url_full_.empty() ? link_url_ : link_url_full_;
	}
	if (!url.empty())
		open_url(url);
}

// The account is gone (unlinked on the website): forget it and say why.
void Hub::signed_out(const std::string &why)
{
	{
		std::lock_guard<std::mutex> g(m_);
		acct_ = Account();
		message_ = why;
		rewards_.clear();
		reward_title_.clear();
	}
	twitch_gen_++;
	kick_gen_++;
	save_account();
	notify();
}

// One platform's sign-in is over (disconnected on the website, or the platform ended it): forget
// just that one, say why.
void Hub::drop(const std::string &pf, const std::string &why)
{
	{
		std::lock_guard<std::mutex> g(m_);
		plat(pf) = Platform();
		rewards_.erase(pf);
		message_ = why;
	}
	(pf == "kick" ? kick_gen_ : twitch_gen_)++;
	save_account();
	notify();
}

void Hub::poll_link()
{
	std::string device;
	{
		std::lock_guard<std::mutex> g(m_);
		if (link_device_.empty() || now_s() < link_next_)
			return;
		if (now_s() > link_expires_) {
			link_device_.clear();
			link_code_.clear();
			message_ = TOO_LONG;
		} else {
			device = link_device_;
			link_next_ = now_s() + link_every_;
		}
	}
	if (device.empty()) {
		notify();
		return;
	}
	Data body = data_new();
	obs_data_set_string(body.get(), "device_code", device.c_str());
	HttpResult r = http("POST", backend() + "/streamtools/api/device/poll", {"Content-Type: application/json"},
			    obs_data_get_json(body.get()));
	Data d = data_json(r.body);
	if (!d)
		return;   // offline for a moment: the next poll tries again
	std::string err = str(d.get(), "error");
	if (err == "authorization_pending")
		return;
	if (err == "slow_down") {
		std::lock_guard<std::mutex> g(m_);
		link_every_ += 2;
		return;
	}
	std::string got = str(d.get(), "platform");
	if (got.empty())
		got = "twitch";
	Data o(obs_data_get_obj(d.get(), got.c_str()));
	bool fresh = false;
	{
		std::lock_guard<std::mutex> g(m_);
		link_device_.clear();
		link_code_.clear();
		std::string api = str(d.get(), "api_token");
		if (!err.empty() || !r.ok() || !o || (api.empty() && !acct_.valid())) {
			std::string why = str(d.get(), "message");   // (e.g. "not_owned": the account hasn't bought OBSPETS)
			message_ = err == "expired_token" ? TOO_LONG
				   : !why.empty()         ? why
							  : "Sign-in didn't finish. Click Sign in with Twitch or Sign in with Kick to try again.";
		} else {
			if (!api.empty() && api != acct_.api_token) {
				acct_ = Account();   // a new install's sign-in: start clean
				acct_.api_token = api;
				fresh = true;
			}
			Platform &P = plat(got);
			P = Platform();
			P.client_id = str(o.get(), "client_id");
			P.access = str(o.get(), "access_token");
			P.refresh = str(o.get(), "refresh_token");
			P.user_id = str(o.get(), "user_id");
			P.login = str(o.get(), "login");
			P.expires_at = wall() + (double)obs_data_get_int(o.get(), "expires_in");
			message_.clear();
			cfg_at_ = -1e9;   // fetch the page's settings now
			rewards_.erase(got);   // look for the reward on this channel
			reward_checked_ = -1e9;
			(got == "kick" ? kick_fails_ : twitch_fails_) = 0;
			(got == "kick" ? kick_missing_ : twitch_missing_).clear();
			scopes_due_ = true;
			info("signed in to sindruid.com with %s as %s", pname(got).c_str(), P.login.c_str());
		}
	}
	if (fresh || got == "twitch")
		twitch_gen_++;
	if (fresh || got == "kick")
		kick_gen_++;
	save_account();
	notify();
}

// A list of short strings in a JSON object, like "automod":["bots","scams"] (obs_data skips lists
// of plain strings). Category names only: no escapes in them.
static bool string_list(const std::string &json, const char *key, std::vector<std::string> &out)
{
	size_t k = json.find("\"" + std::string(key) + "\"");
	if (k == std::string::npos)
		return false;
	size_t a = json.find('[', k), colon = json.find(':', k);
	if (a == std::string::npos || colon == std::string::npos || colon > a)
		return false;
	size_t b = json.find(']', a);
	if (b == std::string::npos)
		return false;
	out.clear();
	for (size_t i = a; (i = json.find('"', i + 1)) != std::string::npos && i < b;) {
		size_t j = json.find('"', i + 1);
		if (j == std::string::npos || j > b)
			break;
		out.push_back(json.substr(i + 1, j - i - 1));
		i = j;
	}
	return true;
}

std::string Hub::api_token()
{
	std::lock_guard<std::mutex> g(m_);
	return acct_.api_token;
}

void Hub::fetch_config()
{
	std::string tok;
	{
		std::lock_guard<std::mutex> g(m_);
		if (!acct_.valid() || now_s() - cfg_at_ < CONFIG_EVERY)
			return;
		cfg_at_ = now_s();
		tok = acct_.api_token;
	}
	// with the port of this PC's character editor, so sindruid.com/streamtools/obspets can link to it
	// (http://127.0.0.1:<port>/editor/open). Only the port: the editor's key never leaves this PC.
	std::vector<std::string> hs{"Authorization: Bearer " + tok};
	if (chat_server::start())
		hs.push_back("X-OBSPET-Editor-Port: " + std::to_string(chat_server::port()));
	HttpResult r = http("GET", backend() + "/streamtools/api/pet/config", hs);
	if (r.status == 401) {
		signed_out(UNLINKED);
		return;
	}
	Data d = data_json(r.body);
	if (!r.ok() || !d)
		return;
	std::vector<std::string> gone;
	{
		std::lock_guard<std::mutex> g(m_);
		cfg_moods_on_ = obs_data_get_bool(d.get(), "moods_on");
		cfg_allow_delete_ = obs_data_get_bool(d.get(), "allow_delete");
		cfg_gap_ = std::max(3.0, (double)obs_data_get_int(d.get(), "min_gap_s"));
		cfg_react_on_ = obs_data_has_user_value(d.get(), "react_on") ? obs_data_get_bool(d.get(), "react_on") : cfg_moods_on_;
		cfg_daily_cap_ = (int)obs_data_get_int(d.get(), "daily_cap");
		std::vector<std::string> am;
		if (string_list(r.body, "automod", am))
			cfg_automod_ = am;
		if (cfg_moods_on_ && mood_note_.find("turned off") != std::string::npos)
			mood_note_.clear();
		// a platform disconnected on the website (or swapped for another account): stop using it here
		Data pl(obs_data_get_obj(d.get(), "platforms"));
		for (const char *pf : {"twitch", "kick"}) {
			Data one(pl ? obs_data_get_obj(pl.get(), pf) : nullptr);
			if (pl && plat(pf).on() && (!one || str(one.get(), "user_id") != plat(pf).user_id))
				gone.push_back(pf);
		}
	}
	for (auto &pf : gone)
		drop(pf, pname(pf) + " was disconnected from your Stream Tools account on sindruid.com, so this OBS stopped using it.");
	notify();
}

// Access tokens last a few hours. The Worker holds the apps' secrets, so refreshing goes through it.
// `stale` is the token the caller found expired: if another thread renewed it meanwhile, that's
// used (Kick takes each refresh token only once, so two renewals at once would sign it out).
bool Hub::refresh(const std::string &pf, const std::string &stale)
{
	std::lock_guard<std::mutex> one(refresh_m_);
	std::string tok, rt;
	{
		std::lock_guard<std::mutex> g(m_);
		if (plat(pf).access != stale)
			return plat(pf).on();
		tok = acct_.api_token;
		rt = plat(pf).refresh;
	}
	if (tok.empty() || rt.empty())
		return false;
	Data body = data_new();
	obs_data_set_string(body.get(), "refresh_token", rt.c_str());
	HttpResult r = http("POST", backend() + "/streamtools/api/" + pf + "/refresh",
			    {"Content-Type: application/json", "Authorization: Bearer " + tok}, obs_data_get_json(body.get()));
	Data d = data_json(r.body);
	if (!r.ok() || !d) {
		if (r.status == 401 && d && str(d.get(), "error") == "signed_out")
			signed_out(UNLINKED);
		else if (d && str(d.get(), "error") == "not_owned")
			drop(pf, str(d.get(), "message"));
		else if (r.status == 401)
			drop(pf, pname(pf) + " ended this sign-in (this happens after a password change, or when the app is removed in your " +
					 pname(pf) + " settings). Click Sign in with " + pname(pf) + " to sign in with it again.");
		return false;
	}
	{
		std::lock_guard<std::mutex> g(m_);
		Platform &P = plat(pf);
		P.access = str(d.get(), "access_token");
		std::string nr = str(d.get(), "refresh_token");
		if (!nr.empty())
			P.refresh = nr;
		P.expires_at = wall() + (double)obs_data_get_int(d.get(), "expires_in");
	}
	save_account();
	return true;
}

// A Helix call with the streamer's Twitch token; refreshes it once on a 401. -> HTTP status
long Hub::helix(const std::string &method, const std::string &path, const std::string &body, std::string *out)
{
	for (int attempt = 0; attempt < 2; attempt++) {
		std::string cid, access;
		{
			std::lock_guard<std::mutex> g(m_);
			cid = acct_.twitch.client_id;
			access = acct_.twitch.access;
		}
		if (access.empty())
			return 401;
		std::vector<std::string> hs = {"Client-Id: " + cid, "Authorization: Bearer " + access};
		if (!body.empty())
			hs.push_back("Content-Type: application/json");
		HttpResult r = http(method, HELIX + path, hs, body);
		if (r.status == 401 && attempt == 0 && refresh("twitch", access))
			continue;
		if (out)
			*out = r.body;
		return r.status;
	}
	return 401;
}

// The same for Kick's public API.
long Hub::kick_api(const std::string &method, const std::string &path, const std::string &body, std::string *out)
{
	for (int attempt = 0; attempt < 2; attempt++) {
		std::string access;
		{
			std::lock_guard<std::mutex> g(m_);
			access = acct_.kick.access;
		}
		if (access.empty())
			return 401;
		std::vector<std::string> hs = {"Authorization: Bearer " + access, "Accept: application/json"};
		if (!body.empty())
			hs.push_back("Content-Type: application/json");
		HttpResult r = http(method, KICK_API + path, hs, body);
		if (r.status == 401 && attempt == 0 && refresh("kick", access))
			continue;
		if (out)
			*out = r.body;
		return r.status;
	}
	return 401;
}

void Hub::delete_message(const std::string &msg_id)
{
	if (msg_id.empty())
		return;
	post_task([this, msg_id] {
		std::string pf, uid;
		{
			std::lock_guard<std::mutex> g(m_);
			auto it = yanked_.find(msg_id);
			if (it == yanked_.end())
				return;   // already deleted (by us or a mod), or never ours to delete
			pf = it->second;
			yanked_.erase(it);
			if (ours_.size() > 1000)
				ours_.clear();
			ours_.insert(msg_id);   // (its delete event comes back: that's us, not a mod)
			uid = acct_.twitch.user_id;
		}
		if (!safe_id(msg_id) || (pf != "kick" && !safe_id(uid))) {
			warn("not deleting a chat message with an unexpected id");
			return;
		}
		long st = pf == "kick" ? kick_api("DELETE", "/chat/" + msg_id, "", nullptr)
				       : helix("DELETE", "/moderation/chat?broadcaster_id=" + uid + "&moderator_id=" + uid + "&message_id=" + msg_id,
					       "", nullptr);
		if (st == 204 || st == 200)
			info("deleted a spam message from %s chat", pname(pf).c_str());
		else
			warn("couldn't delete a %s chat message (HTTP %ld)", pname(pf).c_str(), st);
	});
}

bool Hub::gone(const std::string &msg_id)
{
	if (msg_id.empty())
		return false;
	std::lock_guard<std::mutex> g(m_);
	return gone_.count(msg_id) > 0 && !ours_.count(msg_id);
}

void Hub::mark_gone(const std::string &id, const std::string &user)
{
	std::string u = lower(user);
	std::lock_guard<std::mutex> g(m_);
	auto hit = [&](const std::string &mid, const std::string &who) {
		return id.empty() && u.empty() ? true : !id.empty() ? mid == id : lower(who) == u;
	};
	if (gone_.size() > 5000)
		gone_.clear();
	if (!id.empty())
		gone_.insert(id);
	for (auto &h : recent_)
		if (!h.id.empty() && hit(h.id, h.user))
			gone_.insert(h.id);
	// the mood model never sees it again, and there's nothing left to delete
	lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [&](const Line &l) { return hit(l.id, l.user); }), lines_.end());
	for (auto &mid : gone_)
		yanked_.erase(mid);
}

// ------------------------------------------------------------------ the channel-point reward

void Hub::set_reward(const std::string &pf, const std::string &title, Reward r, const std::string &msg)
{
	{
		std::lock_guard<std::mutex> g(m_);
		if (title != reward_title_)
			return;   // (the title changed meanwhile)
		rewards_[pf] = {r, msg};
	}
	notify();
}

// Whether a list of rewards ({"data": [{"title": ...}]}, Helix's and Kick's alike) has one with this title (any case).
static bool has_title(const std::string &json, const std::string &title)
{
	Data d = data_json(json);
	bool found = false;
	obs_data_array_t *arr = d ? obs_data_get_array(d.get(), "data") : nullptr;
	for (size_t i = 0; arr && i < obs_data_array_count(arr); i++) {
		obs_data_t *it = obs_data_array_item(arr, i);
		if (lower(trim(str(it, "title"))) == lower(trim(title)))
			found = true;
		obs_data_release(it);
	}
	obs_data_array_release(arr);
	return found;
}

Reward Hub::look_reward(const std::string &pf, const std::string &title)
{
	std::string uid, out;
	{
		std::lock_guard<std::mutex> g(m_);
		uid = plat(pf).user_id;
	}
	if (pf != "kick" && !safe_id(uid))
		return Reward::Unknown;
	long st = pf == "kick" ? kick_api("GET", "/channels/rewards", "", &out)
			       : helix("GET", "/channel_points/custom_rewards?broadcaster_id=" + uid, "", &out);
	if (st == 200)
		return has_title(out, title) ? Reward::There : Reward::Missing;
	if (st == 403)
		return Reward::NotAllowed;
	if (st != 401)
		warn("reading the %s channel's rewards: HTTP %ld %.200s", pname(pf).c_str(), st, out.c_str());
	return Reward::Unknown;   // (tries again next time the window opens)
}

// Auto mod's switch and categories are the account's (sindruid.com keeps them, for every OBS on
// it). Shown at once; the site's answer has the last word.
void Hub::set_automod(bool on, const std::vector<std::string> &cats)
{
	std::string tok;
	{
		std::lock_guard<std::mutex> g(m_);
		if (!acct_.valid())
			return;
		tok = acct_.api_token;
		cfg_allow_delete_ = on;
		cfg_automod_ = cats;
	}
	notify();
	post_task([this, tok, on, cats] {
		// (obs_data can't hold a list of plain strings: the JSON by hand; the names are checked ones)
		std::string list = "[";
		for (size_t i = 0; i < cats.size(); i++)
			list += (i ? ",\"" : "\"") + cats[i] + "\"";
		list += "]";
		std::string json = "{\"allow_delete\":" + std::string(on ? "true" : "false") + ",\"automod\":" + list + "}";
		HttpResult r = http("POST", backend() + "/streamtools/api/pet/config",
				    {"Content-Type: application/json", "Authorization: Bearer " + tok}, json);
		if (r.status == 401) {
			signed_out(UNLINKED);
			return;
		}
		if (!r.ok())
			warn("saving Auto mod: HTTP %ld %.200s", r.status, r.body.c_str());
		{
			std::lock_guard<std::mutex> g(m_);
			cfg_at_ = -1e9;   // (read it back from the site on the next look)
		}
		fetch_config();
		notify();
	});
}

// React to chat, the time between reactions and the daily limit are the account's too (they decide
// what the credit on sindruid.com is spent on). Like Auto mod: shown at once, the site has the last word.
void Hub::set_reacting(bool on, int gap_s, int daily_cap)
{
	gap_s = std::clamp(gap_s, 3, 600);
	daily_cap = std::clamp(daily_cap, 0, 20000);
	std::string tok;
	{
		std::lock_guard<std::mutex> g(m_);
		if (!acct_.valid())
			return;
		tok = acct_.api_token;
		cfg_react_on_ = on;
		cfg_moods_on_ = on;
		cfg_gap_ = gap_s;
		cfg_daily_cap_ = daily_cap;
	}
	notify();
	post_task([this, tok, on, gap_s, daily_cap] {
		std::string json = "{\"moods_on\":" + std::string(on ? "true" : "false") + ",\"min_gap_s\":" +
				   std::to_string(gap_s) + ",\"daily_cap\":" + std::to_string(daily_cap) + "}";
		HttpResult r = http("POST", backend() + "/streamtools/api/pet/config",
				    {"Content-Type: application/json", "Authorization: Bearer " + tok}, json);
		if (r.status == 401) {
			signed_out(UNLINKED);
			return;
		}
		if (!r.ok())
			warn("saving the reactions to chat: HTTP %ld %.200s", r.status, r.body.c_str());
		{
			std::lock_guard<std::mutex> g(m_);
			cfg_at_ = -1e9;   // (read it back from the site on the next look)
		}
		fetch_config();
		notify();
	});
}

void Hub::check_reward(const std::string &title, bool again)
{
	std::vector<std::string> pfs = platforms_on();
	{
		std::lock_guard<std::mutex> g(m_);
		if (pfs.empty() || title.empty())
			return;
		bool known = title == reward_title_ &&
			     std::all_of(pfs.begin(), pfs.end(), [&](const std::string &pf) { return rewards_[pf].r != Reward::Unknown; });
		// asked again (the Check button: say it was deleted or renamed on Twitch): a fresh look,
		// at most every few seconds
		if (again && title == reward_title_ && now_s() - reward_checked_ < 3)
			return;
		if (!again && (known || (title == reward_title_ && now_s() - reward_checked_ < 60)))
			return;
		reward_checked_ = now_s();
		if (title != reward_title_)
			rewards_.clear();
		reward_title_ = title;
		for (auto &pf : pfs)
			if (again || rewards_[pf].r == Reward::Unknown)
				rewards_[pf] = {Reward::Checking, ""};
	}
	post_task([this, title, pfs] {
		for (auto &pf : pfs) {
			bool look;
			{
				std::lock_guard<std::mutex> g(m_);
				look = rewards_[pf].r == Reward::Checking;
			}
			if (look)
				set_reward(pf, title, look_reward(pf, title));
		}
	});
}

void Hub::make_reward(const std::string &pf, const std::string &title)
{
	Reward now = look_reward(pf, title);
	if (now == Reward::There || now == Reward::NotAllowed) {
		set_reward(pf, title, now);
		return;
	}
	if (now != Reward::Missing) {
		set_reward(pf, title, Reward::Failed, "Couldn't reach " + pname(pf) + ". Try again in a moment.");
		return;
	}
	std::string uid, out;
	{
		std::lock_guard<std::mutex> g(m_);
		uid = plat(pf).user_id;
	}
	if (pf != "kick" && !safe_id(uid)) {
		set_reward(pf, title, Reward::Failed, "Sign in again, then try again.");
		return;
	}
	Data b = data_new();
	obs_data_set_string(b.get(), "title", title.c_str());
	obs_data_set_int(b.get(), "cost", 100);
	obs_data_set_bool(b.get(), "should_redemptions_skip_request_queue", true);
	obs_data_set_string(b.get(), "background_color", "#C42A1F");
	long c;
	if (pf == "kick") {
		obs_data_set_bool(b.get(), "is_enabled", true);
		c = kick_api("POST", "/channels/rewards", obs_data_get_json(b.get()), &out);
	} else {
		obs_data_set_bool(b.get(), "is_global_cooldown_enabled", true);
		obs_data_set_int(b.get(), "global_cooldown_seconds", 10);
		c = helix("POST", "/channel_points/custom_rewards?broadcaster_id=" + uid, obs_data_get_json(b.get()), &out);
	}
	if (c == 200 || c == 201) {
		info("made the %s channel-point reward \"%s\"", pname(pf).c_str(), title.c_str());
		set_reward(pf, title, Reward::Made);
	} else if (c == 400 && (out.find("DUPLICATE") != std::string::npos || out.find("already") != std::string::npos)) {
		set_reward(pf, title, Reward::There);
	} else if (c == 403) {
		set_reward(pf, title, Reward::NotAllowed);
	} else {
		warn("making the %s channel-point reward: HTTP %ld %.200s", pname(pf).c_str(), c, out.c_str());
		std::string why = c == 400 && out.find("title") != std::string::npos
					  ? pname(pf) + " didn't accept that title. Try a shorter one (45 letters at most), or another word."
					  : pname(pf) + " said no. Try again in a moment.";
		set_reward(pf, title, Reward::Failed, why);
	}
}

void Hub::ensure_reward(const std::string &title)
{
	std::vector<std::string> pfs = platforms_on();
	{
		std::lock_guard<std::mutex> g(m_);
		if (title != reward_title_)
			rewards_.clear();
		reward_title_ = title;
		for (auto &pf : pfs)
			if (rewards_[pf].r != Reward::There && rewards_[pf].r != Reward::Made)
				rewards_[pf] = {Reward::Checking, ""};
	}
	notify();
	post_task([this, title, pfs] {
		for (auto &pf : pfs) {
			bool make;
			{
				std::lock_guard<std::mutex> g(m_);
				make = rewards_[pf].r == Reward::Checking;
			}
			if (make)
				make_reward(pf, title);
		}
	});
}

ChatTry Hub::try_chat(PetClient *pet, const std::string &user, const std::string &text)
{
	ChatTry out;
	std::string tok, login;
	{
		std::lock_guard<std::mutex> g(m_);
		if (!acct_.valid()) {
			out.error = "Sign in on the Account page first: the AI that reads chat runs on your Stream Tools account.";
			return out;
		}
		auto it = profiles_.find(pet);
		if (it != profiles_.end())
			out.prof = it->second;
		else if (PetClient *mp = mood_pet())
			out.prof = profiles_[mp];
		out.prof.allow_delete = out.prof.allow_delete && cfg_allow_delete_;   // (Auto mod: off here or on the site)
		tok = acct_.api_token;
		login = acct_.twitch.on() ? acct_.twitch.login : acct_.kick.login;
	}
	const MoodProfile &prof = out.prof;
	Data body = data_new();
	obs_data_array_t *arr = obs_data_array_create();
	{
		Data o = data_new();
		obs_data_set_string(o.get(), "user", user.c_str());
		obs_data_set_string(o.get(), "text", text.c_str());
		obs_data_set_bool(o.get(), "new", true);
		obs_data_array_push_back(arr, o.get());
	}
	obs_data_set_array(body.get(), "lines", arr);
	obs_data_array_release(arr);
	obs_data_set_string(body.get(), "name", prof.name.c_str());
	obs_data_set_string(body.get(), "describe", prof.describe.c_str());
	obs_data_set_string(body.get(), "pronouns", prof.pronouns.c_str());
	obs_data_set_string(body.get(), "personality", prof.personality.c_str());
	obs_data_set_string(body.get(), "state", prof.state.c_str());
	if (!prof.skip.empty()) {
		obs_data_array_t *sk = obs_data_array_create();
		for (auto &k : prof.skip) {
			Data o = data_new();
			obs_data_set_string(o.get(), "kind", k.c_str());
			obs_data_array_push_back(sk, o.get());
		}
		obs_data_set_array(body.get(), "skip", sk);
		obs_data_array_release(sk);
	}
	obs_data_set_string(body.get(), "channel", login.c_str());
	obs_data_set_bool(body.get(), "test", true);
	std::string json = obs_data_get_json(body.get());

	// (the settings server waits ~6 s for its requests when OBS closes: stay inside that)
	HttpResult r;
	Data d;
	for (int tries = 0; tries < 2; tries++) {
		r = http("POST", backend() + "/streamtools/api/pet/mood", {"Content-Type: application/json", "Authorization: Bearer " + tok}, json, 5);
		d = data_json(r.body);
		if (r.status == 429 && d && str(d.get(), "error") == "too_soon" && tries == 0) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1500));   // (chat was asked about a moment ago)
			continue;
		}
		break;
	}
	if (r.status == 401) {
		out.error = "This OBS isn't signed in any more. Sign in again on the Account page.";
		return out;
	}
	if (!r.ok()) {
		std::string err = d ? str(d.get(), "error") : "", msg = d ? str(d.get(), "message") : "";
		if (!r.status)
			out.error = "Can't reach sindruid.com right now. Check your connection and try again.";
		else if (err == "paused" && !msg.empty())
			out.error = "Chat reactions are paused: " + msg;
		else if (err == "too_soon")
			out.error = "Chat was just checked. Try again in a couple of seconds.";
		else
			out.error = msg.empty() ? "The AI didn't answer. Try again." : msg;
		return out;
	}
	out.kind = d ? str(d.get(), "kind") : "";
	if (out.kind.empty())
		out.kind = "none";
	out.why = d ? str(d.get(), "why") : "";
	out.del = d && obs_data_get_bool(d.get(), "delete") && prof.allow_delete;
	if (out.del)
		out.kind = "yank";
	info("chat tester: %s%s", out.kind.c_str(), out.del ? " (Auto mod would delete it)" : "");
	return out;
}

// ------------------------------------------------------------------ badges for OBSPETS Chat

// Every badge picture a chat in `channel` can show, as {"set/version": url}: Twitch's global ones,
// then the channel's own (subscriber months, bits, ...). Helix needs a Twitch sign-in, so "{}"
// without one. Asked by a chat page from the chat server's thread; fetched on the worker; kept for an hour.
std::string Hub::chat_badges(const std::string &channel)
{
	if (!channel.empty() && !safe_id(channel, 25))
		return "{}";   // (not a Twitch name)
	{
		std::lock_guard<std::mutex> g(m_);
		if (!acct_.valid() || !acct_.twitch.on())
			return "{}";
		auto it = badges_.find(channel);
		if (it != badges_.end() && now_s() - it->second.first < 3600)
			return it->second.second;
	}
	auto done = std::make_shared<std::promise<std::string>>();
	std::future<std::string> got = done->get_future();
	post_task([this, channel, done] {
		Data out = data_new();
		auto add = [&](const std::string &body) {
			Data d = data_json(body);
			obs_data_array_t *sets = d ? obs_data_get_array(d.get(), "data") : nullptr;
			for (size_t i = 0; sets && i < obs_data_array_count(sets); i++) {
				Data set(obs_data_array_item(sets, i));
				std::string sid = str(set.get(), "set_id");
				obs_data_array_t *vs = obs_data_get_array(set.get(), "versions");
				for (size_t j = 0; vs && j < obs_data_array_count(vs); j++) {
					Data v(obs_data_array_item(vs, j));
					obs_data_set_string(out.get(), (sid + "/" + str(v.get(), "id")).c_str(), str(v.get(), "image_url_2x").c_str());
				}
				obs_data_array_release(vs);
			}
			obs_data_array_release(sets);
		};
		std::string body, uid, login;
		{
			std::lock_guard<std::mutex> g(m_);
			uid = acct_.twitch.user_id;
			login = lower(acct_.twitch.login);
		}
		bool ok = helix("GET", "/chat/badges/global", "", &body) == 200;
		if (ok)
			add(body);
		if (!channel.empty() && channel != login) {
			uid.clear();
			if (helix("GET", "/users?login=" + channel, "", &body) == 200) {
				Data d = data_json(body);
				obs_data_array_t *a = d ? obs_data_get_array(d.get(), "data") : nullptr;
				if (a && obs_data_array_count(a) > 0) {
					Data u(obs_data_array_item(a, 0));
					uid = str(u.get(), "id");
				}
				obs_data_array_release(a);
			}
		}
		if (safe_id(uid) && helix("GET", "/chat/badges?broadcaster_id=" + uid, "", &body) == 200)
			add(body);   // (after the global ones, so the channel's own win)
		std::string json = obs_data_get_json(out.get());
		if (ok) {
			std::lock_guard<std::mutex> g(m_);
			badges_[channel] = {now_s(), json};
		}
		done->set_value(json);
	});
	// (in steps: once the hub stops, nothing answers, and the chat server must not wait on it)
	for (int i = 0; i < 80; i++) {
		if (got.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready)
			return got.get();
		if (stop_)
			break;
	}
	return "{}";
}

// ------------------------------------------------------------------ the worker

void Hub::worker_loop()
{
	while (!stop_) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lk(m_);
			cv_.wait_for(lk, std::chrono::milliseconds(250), [this] { return stop_ || !tasks_.empty(); });
			if (stop_)
				break;
			if (!tasks_.empty()) {
				task = std::move(tasks_.front());
				tasks_.pop_front();
			}
		}
		if (task) {
			task();
			continue;
		}
		poll_link();
		fetch_config();
		if (scopes_due_.exchange(false))
			for (auto &pf : platforms_on())
				check_scopes(pf);
	}
}

void Hub::mood_loop()
{
	while (!stop_) {
		mood_tick();
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
}

// ------------------------------------------------------------------ moods

// A chat line from either platform (pf "" = a test line from the pet's window).
void Hub::hear(const std::string &pf, const std::string &user, const std::string &text, const std::string &color,
	       const std::string &id, bool trusted)
{
	std::string t = trim(text);
	if (t.empty())
		return;
	HubEvent e;
	e.t = HubEvent::T::Chat;
	e.user = user;
	e.text = t;
	broadcast(e);   // (busy chat keeps the pets awake, even without moods)
	std::lock_guard<std::mutex> g(m_);
	if (!id.empty()) {
		recent_.push_back({id, lower(user)});
		while (recent_.size() > 300)
			recent_.pop_front();
	}
	if (t[0] == '!')
		return;   // a command for a bot
	if (PetClient *mp = mood_pet()) {
		auto &ig = profiles_[mp].ignore;
		if (std::find(ig.begin(), ig.end(), lower(user)) != ig.end())
			return;
	}
	lines_.push_back({user, t.substr(0, 300), color, id, pf, trusted, true});
	while (lines_.size() > 25)
		lines_.pop_front();
}

// One question for everything new, as soon as the pace allows: at once when nothing was asked in
// the last few seconds, else when that time is up (with everything that came in meanwhile).
void Hub::mood_tick()
{
	MoodProfile prof;
	std::string tok, login, personality;
	Data body = data_new();
	std::vector<Line> batch;
	std::vector<std::string> only;   // the reactions worth asking about now (empty: every one)
	bool nothing = false;            // none: don't ask at all
	{
		std::lock_guard<std::mutex> g(m_);
		PetClient *mp = mood_pet();
		if (!mp || !acct_.valid() || !cfg_moods_on_)
			return;
		prof = profiles_[mp];
		if (!prof.on)
			return;
		if (prof.automod_only && !(prof.allow_delete && cfg_allow_delete_ && !cfg_automod_.empty()))
			return;   // (only Auto mod would use it, and it can't delete anything)
		bool any = std::any_of(lines_.begin(), lines_.end(), [](const Line &l) { return l.is_new; });
		double t = now_s();
		if (!any || t < last_ask_ + cfg_gap_ || t < paused_until_)
			return;
		last_ask_ = t;
		// Only ask about what could still happen, so the model reads a shorter menu (and nothing is
		// asked when nothing could): off screen, only Auto mod's deletes matter; within the gap after
		// a reaction, only angry and yank get through (see the hold-back below).
		bool can_delete = prof.allow_delete && cfg_allow_delete_;
		bool angry_ok = std::find(prof.skip.begin(), prof.skip.end(), "angry") == prof.skip.end();
		if (!prof.automod_only && !mp->on_screen()) {
			if (can_delete)
				only = {"yank"};
			else
				nothing = true;
		} else if (!prof.automod_only && !last_kind_.empty() && t - last_react_at_ < prof.gap) {
			only = {"yank"};
			if (angry_ok)
				only.push_back("angry");
		}
		if (nothing) {   // (what's new now is context for the next question)
			for (auto &l : lines_)
				l.is_new = false;
			return;
		}
		tok = acct_.api_token;
		login = acct_.twitch.on() ? acct_.twitch.login : acct_.kick.login;
		personality = prof.personality;   // the character's own (set in the character editor)
		obs_data_array_t *arr = obs_data_array_create();
		for (auto &l : lines_) {
			Data o = data_new();
			obs_data_set_string(o.get(), "user", l.user.c_str());
			obs_data_set_string(o.get(), "text", l.text.c_str());
			obs_data_set_bool(o.get(), "new", l.is_new);
			obs_data_array_push_back(arr, o.get());
			if (l.is_new)
				batch.push_back(l);
			l.is_new = false;
		}
		obs_data_set_array(body.get(), "lines", arr);
		obs_data_array_release(arr);
		if (!last_kind_.empty()) {
			Data last = data_new();
			obs_data_set_string(last.get(), "kind", last_kind_.c_str());
			obs_data_set_double(last.get(), "ago", t - last_react_at_);
			obs_data_set_obj(body.get(), "last", last.get());
		}
	}
	obs_data_set_string(body.get(), "name", prof.name.c_str());
	obs_data_set_string(body.get(), "describe", prof.describe.c_str());
	obs_data_set_string(body.get(), "pronouns", prof.pronouns.c_str());
	obs_data_set_string(body.get(), "personality", personality.c_str());
	obs_data_set_string(body.get(), "state", prof.state.c_str());
	if (!prof.skip.empty()) {
		obs_data_array_t *sk = obs_data_array_create();
		for (auto &k : prof.skip) {
			Data o = data_new();
			obs_data_set_string(o.get(), "kind", k.c_str());
			obs_data_array_push_back(sk, o.get());
		}
		obs_data_set_array(body.get(), "skip", sk);
		obs_data_array_release(sk);
	}
	if (!only.empty()) {
		obs_data_array_t *on = obs_data_array_create();
		for (auto &k : only) {
			Data o = data_new();
			obs_data_set_string(o.get(), "kind", k.c_str());
			obs_data_array_push_back(on, o.get());
		}
		obs_data_set_array(body.get(), "only", on);
		obs_data_array_release(on);
	}
	obs_data_set_string(body.get(), "channel", login.c_str());

	HttpResult r = http("POST", backend() + "/streamtools/api/pet/mood", {"Content-Type: application/json", "Authorization: Bearer " + tok},
			    obs_data_get_json(body.get()), 12);
	Data d = data_json(r.body);
	std::string err = d ? str(d.get(), "error") : "";
	if (r.status == 401) {
		signed_out(UNLINKED);
		return;
	}
	if (!r.ok()) {
		std::string note;
		double pause = 30;
		if (err == "paused") {
			note = "Reactions to chat are paused: " + (d ? str(d.get(), "message") : "");
			pause = 300;
			std::lock_guard<std::mutex> g(m_);
			cfg_at_ = -1e9;   // the page may have changed: look again soon
		} else if (err == "too_soon") {
			pause = 3;
		} else {
			note = r.status ? "" : "Can't reach sindruid.com right now, so it isn't reacting to chat. It keeps trying.";
		}
		bool changed;
		{
			std::lock_guard<std::mutex> g(m_);
			paused_until_ = now_s() + pause;
			changed = note != mood_note_;
			mood_note_ = note;
		}
		if (changed)
			notify();
		return;
	}
	bool had;
	{
		std::lock_guard<std::mutex> g(m_);
		had = !mood_note_.empty();
		mood_note_.clear();
	}
	if (had)
		notify();
	std::string kind = str(d.get(), "kind"), who = str(d.get(), "who"), why = str(d.get(), "why");
	bool del = obs_data_get_bool(d.get(), "delete");

	// `who` must be someone in this batch (a model can't make up a name to show). Deletes: only a
	// message in this batch, never the broadcaster, a mod or a VIP, and only if this Pet allows it.
	const Line *viewer = nullptr;
	if (!who.empty())
		for (auto it = batch.rbegin(); it != batch.rend(); ++it)
			if (lower(it->user) == lower(who)) {
				viewer = &*it;
				break;
			}
	bool remove = del && viewer && !viewer->id.empty() && !viewer->platform.empty() && !viewer->trusted && prof.allow_delete;
	if (remove)
		kind = "yank";
	if (kind == "none" || kind.empty()) {
		info("mood: none%s%s", why.empty() ? "" : ": ", why.c_str());
		return;
	}
	if (prof.automod_only && !remove) {
		info("mood: %s (Auto mod only: nothing to act out)", kind.c_str());
		return;
	}
	{
		std::lock_guard<std::mutex> g(m_);
		// deleted while the model thought about it (a mod was quicker): never put it back on screen
		if (viewer && !viewer->id.empty() && gone_.count(viewer->id) && !ours_.count(viewer->id)) {
			info("mood: %s at a message that was deleted meanwhile: skipped", kind.c_str());
			return;
		}
		double t = now_s();
		if (!remove && kind != "angry" && kind != "yank" && t - last_react_at_ < prof.gap) {
			info("mood: %s, held back (reacted %.0f s ago)", kind.c_str(), t - last_react_at_);
			return;
		}
		last_kind_ = kind;
		last_react_at_ = t;
		if (remove)
			yanked_[viewer->id] = viewer->platform;
	}
	info("mood: %s%s%s%s", kind.c_str(), viewer ? " at " : "", viewer ? viewer->user.c_str() : "", remove ? " (deleting it)" : "");
	if (remove) {
		// Auto mod deletes it now, not when the pet gets there: nobody else in chat should read it
		// any longer than it takes to catch. An OBSPETS Chat keeps the line, crossed out, for the
		// pet to rip out (told first, so it never drops it on the platform's delete event). With no
		// pet to come for it (or set not to show it), the chat just drops it on that delete event.
		if (prof.show_deletes)
			ChatSource::strike_all(viewer->id);
		delete_message(viewer->id);
		if (!prof.show_deletes)
			return;
	} else if (prof.automod_only) {
		return;
	}
	HubEvent e;
	e.t = HubEvent::T::Mood;
	e.kind = kind;
	if (viewer) {
		e.user = viewer->user;
		e.color = viewer->color;
		e.text = viewer->text;
		e.msg_id = viewer->id;
	}
	e.remove = remove;
	std::lock_guard<std::mutex> g(m_);
	if (PetClient *mp = mood_pet())
		mp->hub_event(e);   // one pet acts it out (two pets doing the same thing looks odd)
}

// ------------------------------------------------------------------ Twitch

std::string Hub::chat_color(const std::string &user_id)
{
	{
		std::lock_guard<std::mutex> g(m_);
		auto it = colors_.find(user_id);
		if (it != colors_.end())
			return it->second;
	}
	std::string out, color;
	if (safe_id(user_id) && helix("GET", "/chat/color?user_id=" + user_id, "", &out) == 200) {
		Data d = data_json(out);
		obs_data_array_t *arr = d ? obs_data_get_array(d.get(), "data") : nullptr;
		if (arr && obs_data_array_count(arr)) {
			obs_data_t *it = obs_data_array_item(arr, 0);
			color = str(it, "color");
			obs_data_release(it);
		}
		obs_data_array_release(arr);
	}
	std::lock_guard<std::mutex> g(m_);
	if (colors_.size() > 5000)
		colors_.clear();   // (a long stream with many redeemers: start over rather than grow for ever)
	colors_[user_id] = color;
	return color;
}

bool Hub::subscribe_all(const std::string &session)
{
	std::string uid;
	{
		std::lock_guard<std::mutex> g(m_);
		uid = acct_.twitch.user_id;
	}
	struct Sub {
		const char *type, *version;
		std::vector<std::pair<const char *, std::string>> cond;
	};
	std::vector<Sub> subs = {
		{"channel.follow", "2", {{"broadcaster_user_id", uid}, {"moderator_user_id", uid}}},
		{"channel.subscribe", "1", {{"broadcaster_user_id", uid}}},
		{"channel.subscription.message", "1", {{"broadcaster_user_id", uid}}},
		{"channel.subscription.gift", "1", {{"broadcaster_user_id", uid}}},
		{"channel.cheer", "1", {{"broadcaster_user_id", uid}}},
		{"channel.raid", "1", {{"to_broadcaster_user_id", uid}}},
		{"channel.channel_points_custom_reward_redemption.add", "1", {{"broadcaster_user_id", uid}}},
		{"channel.chat.message", "1", {{"broadcaster_user_id", uid}, {"user_id", uid}}},
		// deletions, so the pet never puts a deleted message back on screen (same scope as chat)
		{"channel.chat.message_delete", "1", {{"broadcaster_user_id", uid}, {"user_id", uid}}},
		{"channel.chat.clear_user_messages", "1", {{"broadcaster_user_id", uid}, {"user_id", uid}}},
		{"channel.chat.clear", "1", {{"broadcaster_user_id", uid}, {"user_id", uid}}},
	};
	int ok = 0;
	for (auto &s : subs) {
		Data b = data_new(), cond = data_new(), tr = data_new();
		obs_data_set_string(b.get(), "type", s.type);
		obs_data_set_string(b.get(), "version", s.version);
		for (auto &c : s.cond)
			obs_data_set_string(cond.get(), c.first, c.second.c_str());
		obs_data_set_obj(b.get(), "condition", cond.get());
		obs_data_set_string(tr.get(), "method", "websocket");
		obs_data_set_string(tr.get(), "session_id", session.c_str());
		obs_data_set_obj(b.get(), "transport", tr.get());
		std::string out;
		long st = helix("POST", "/eventsub/subscriptions", obs_data_get_json(b.get()), &out);
		if (st == 202)
			ok++;
		else
			warn("Twitch: couldn't listen for %s (HTTP %ld): %.200s", s.type, st, out.c_str());
	}
	info("Twitch: listening for %d of %d kinds of events", ok, (int)subs.size());
	return ok > 0;
}

void Hub::handle_notification(obs_data_t *payload)
{
	obs_data_t *sub = obs_data_get_obj(payload, "subscription");
	obs_data_t *ev = obs_data_get_obj(payload, "event");
	std::string type = str(sub, "type");
	HubEvent e;
	bool send = true;
	if (type == "channel.follow") {
		e.kind = "follow";
		e.user = str(ev, "user_name");
	} else if (type == "channel.subscribe") {
		send = !obs_data_get_bool(ev, "is_gift");   // gifts come as channel.subscription.gift
		e.kind = "sub";
		e.user = str(ev, "user_name");
	} else if (type == "channel.subscription.message") {
		e.kind = "sub";
		e.user = str(ev, "user_name");
	} else if (type == "channel.subscription.gift") {
		e.kind = "gift";
		e.n = std::max(1, count_of(obs_data_get_int(ev, "total")));
		e.user = str(ev, "user_name");
	} else if (type == "channel.cheer") {
		e.kind = "bits";
		e.n = count_of(obs_data_get_int(ev, "bits"));
		e.user = str(ev, "user_name");
	} else if (type == "channel.raid") {
		e.kind = "raid";
		e.n = count_of(obs_data_get_int(ev, "viewers"));
		e.user = str(ev, "from_broadcaster_user_name");
	} else if (type == "channel.channel_points_custom_reward_redemption.add") {
		e.t = HubEvent::T::Redeem;
		obs_data_t *rw = obs_data_get_obj(ev, "reward");
		e.title = str(rw, "title");
		obs_data_release(rw);
		e.user = str(ev, "user_name");
		e.color = chat_color(str(ev, "user_id"));
	} else if (type == "channel.chat.message") {
		send = false;
		obs_data_t *msg = obs_data_get_obj(ev, "message");
		bool trusted = false;
		obs_data_array_t *badges = obs_data_get_array(ev, "badges");
		for (size_t i = 0; i < obs_data_array_count(badges); i++) {
			obs_data_t *b = obs_data_array_item(badges, i);
			std::string set = str(b, "set_id");
			if (set == "broadcaster" || set == "moderator" || set == "vip")
				trusted = true;
			obs_data_release(b);
		}
		obs_data_array_release(badges);
		hear("twitch", str(ev, "chatter_user_name"), str(msg, "text"), str(ev, "color"), str(ev, "message_id"), trusted);
		obs_data_release(msg);
	} else if (type == "channel.chat.message_delete") {
		send = false;
		if (!str(ev, "message_id").empty())
			mark_gone(str(ev, "message_id"), "");
	} else if (type == "channel.chat.clear_user_messages") {   // a ban or a timeout
		send = false;
		if (!str(ev, "target_user_name").empty())
			mark_gone("", str(ev, "target_user_name"));
	} else if (type == "channel.chat.clear") {
		send = false;
		mark_gone("", "");
	} else {
		send = false;
	}
	obs_data_release(ev);
	obs_data_release(sub);
	if (send) {
		info("Twitch: %s%s%s", e.t == HubEvent::T::Redeem ? "redemption " : e.kind.c_str(),
		     e.t == HubEvent::T::Redeem ? e.title.c_str() : "", e.user.empty() ? "" : (" from " + e.user).c_str());
		broadcast(e);
	}
}

void Hub::twitch_loop()
{
	double retry = 2;
	while (!stop_) {
		int gen = twitch_gen_;
		bool have;
		{
			std::lock_guard<std::mutex> g(m_);
			have = acct_.valid() && acct_.twitch.on();
		}
		if (!have) {
			nap(1, stop_);
			continue;
		}
		std::string url = EVENTSUB;
		bool subscribed = false;
		WebSocket ws;
		double keepalive = 15, last_msg = now_s();
		std::string err;
		if (!ws.connect(url, err)) {
			warn("Twitch: couldn't connect (%s); trying again in %.0f s", err.c_str(), retry);
			nap(retry, stop_);
			retry = std::min(120.0, retry * 2);
			continue;
		}
		while (!stop_ && gen == twitch_gen_) {
			std::string text;
			int got = ws.recv(text, 500);
			if (got < 0) {
				warn("Twitch: connection closed; reconnecting");
				break;
			}
			if (got == 0) {
				if (now_s() - last_msg > keepalive + 10) {
					warn("Twitch: no keepalive; reconnecting");
					break;
				}
				continue;
			}
			last_msg = now_s();
			Data msg = data_json(text);
			if (!msg)
				continue;
			obs_data_t *meta = obs_data_get_obj(msg.get(), "metadata");
			obs_data_t *payload = obs_data_get_obj(msg.get(), "payload");
			std::string mt = str(meta, "message_type");
			if (mt == "session_welcome") {
				obs_data_t *sess = obs_data_get_obj(payload, "session");
				keepalive = (double)std::max<long long>(10, obs_data_get_int(sess, "keepalive_timeout_seconds"));
				std::string id = str(sess, "id");
				obs_data_release(sess);
				if (!subscribed) {   // (after a session_reconnect the subscriptions carry over)
					subscribed = subscribe_all(id);
					if (subscribed) {
						retry = 2;
						std::lock_guard<std::mutex> g(m_);
						twitch_fails_ = 0;
						if (message_.rfind(CANT_LISTEN, 0) == 0)
							message_.clear();
					}
				}
				{
					std::lock_guard<std::mutex> g(m_);
					twitch_live_ = subscribed;
				}
				notify();
			} else if (mt == "notification") {
				handle_notification(payload);
			} else if (mt == "session_reconnect") {
				obs_data_t *sess = obs_data_get_obj(payload, "session");
				std::string re = str(sess, "reconnect_url");
				obs_data_release(sess);
				WebSocket next;
				if (twitch_wss(re) && next.connect(re, err)) {   // (only ever on to Twitch's own servers)
					ws.close();
					ws = std::move(next);
				}
			} else if (mt == "revocation") {
				obs_data_t *sub = obs_data_get_obj(payload, "subscription");
				warn("Twitch stopped sending %s (%s)", str(sub, "type").c_str(), str(sub, "status").c_str());
				obs_data_release(sub);
			}
			obs_data_release(meta);
			obs_data_release(payload);
		}
		ws.close();
		{
			std::lock_guard<std::mutex> g(m_);
			twitch_live_ = false;
			// Twitch closes a session nothing listens on after ~10 s: don't hammer it. One miss
			// is often a blip, so it says so only from the second in a row.
			if (!subscribed && gen == twitch_gen_ && acct_.twitch.on() && ++twitch_fails_ >= 2)
				message_ = std::string(CANT_LISTEN) + " It keeps trying by itself. If this doesn't go away, "
					   "click Sign out, then sign in again.";
		}
		notify();
		double wait = subscribed ? 1 : retry;
		if (!subscribed)
			retry = std::min(300.0, retry * 2);
		nap(wait, stop_, [&] { return gen == twitch_gen_; });
	}
}

// ------------------------------------------------------------------ Kick

// Kick's chat and events come over the same public socket (Pusher) kick.com's own page reads, with
// no sign-in: chat, subs, gifts, hosts, Kicks and channel-point redemptions. (Not follows: Kick only
// sends those to an app's webhook.) Sending (deleting spam, the reward) uses Kick's public API with
// the streamer's token.

// "[emote:123:name] hi" -> "name hi"
static std::string kick_text(const std::string &s)
{
	std::string out;
	size_t i = 0;
	while (i < s.size()) {
		if (s.compare(i, 7, "[emote:") == 0) {
			size_t a = s.find(':', i + 7), b = s.find(']', i + 7);
			if (a != std::string::npos && b != std::string::npos && a < b) {
				out += s.substr(a + 1, b - a - 1);
				i = b + 1;
				continue;
			}
		}
		out += s[i++];
	}
	return out;
}

// A number or a string from Kick's JSON, as a string (ids come both ways).
static std::string idstr(obs_data_t *d, const char *k)
{
	std::string s = str(d, k);
	if (!s.empty())
		return s;
	long long n = obs_data_get_int(d, k);
	return n ? std::to_string(n) : "";
}

// The channel's ids for the chat socket, from kick.com's own channel API (no sign-in; a plain request,
// since Kick blocks a faked browser one). Kept in account.json, for when kick.com can't be reached.
bool Hub::kick_ids()
{
	std::string slug;
	{
		std::lock_guard<std::mutex> g(m_);
		if (!acct_.kick.chatroom_id.empty())
			return true;
		slug = acct_.kick.login;
	}
	if (!safe_id(slug)) {
		warn("Kick: the signed-in name isn't a channel name");
		return false;
	}
	HttpResult r = http("GET", "https://kick.com/api/v2/channels/" + slug, {"Accept: application/json"});
	Data d = data_json(r.body);
	Data room(d ? obs_data_get_obj(d.get(), "chatroom") : nullptr);
	std::string cid = d ? idstr(d.get(), "id") : "", rid = room ? idstr(room.get(), "id") : "";
	if (!r.ok() || !safe_id(cid) || !safe_id(rid)) {
		warn("Kick: couldn't look up kick.com/%s (HTTP %ld %s)", slug.c_str(), r.status, r.error.c_str());
		return false;
	}
	{
		std::lock_guard<std::mutex> g(m_);
		if (acct_.kick.login != slug)
			return false;
		acct_.kick.channel_id = cid;
		acct_.kick.chatroom_id = rid;
	}
	save_account();
	return true;
}

// What each platform's token must be allowed to do, and what it's for, in the words the properties
// use. Keep in step with the scopes the backend requests for OBSPETS.
static const std::pair<const char *, const char *> TWITCH_SCOPES[] = {
	{"moderator:read:followers", "follows"},
	{"channel:read:subscriptions", "subs and gifts"},
	{"bits:read", "bits"},
	{"channel:read:redemptions", "channel points"},
	{"channel:manage:redemptions", "making the channel-point reward"},
	{"user:read:chat", "chat reactions"},
	{"moderator:manage:chat_messages", "Auto mod"},
};
static const std::pair<const char *, const char *> KICK_SCOPES[] = {
	{"channel:rewards:read", "channel points"},
	{"channel:rewards:write", "making the channel-point reward"},
	{"moderation:chat_message:manage", "Auto mod"},
};

// A sign-in made before OBSPETS needed a permission doesn't have it, and what needs it fails
// quietly. This asks the platform what the token may do, and the properties say what signing in
// again would turn on. Anything unclear (offline, an answer it can't read) says nothing.
void Hub::check_scopes(const std::string &pf)
{
	std::string access;
	{
		std::lock_guard<std::mutex> g(m_);
		access = plat(pf).access;
	}
	if (access.empty())
		return;
	std::string granted;   // the scopes, each in quotes or spaces around it
	if (pf == "twitch") {
		HttpResult r = http("GET", "https://id.twitch.tv/oauth2/validate", {"Authorization: OAuth " + access});
		if (r.status == 401 && refresh("twitch", access)) {
			std::lock_guard<std::mutex> g(m_);
			access = acct_.twitch.access;
		}
		if (r.status == 401)
			r = http("GET", "https://id.twitch.tv/oauth2/validate", {"Authorization: OAuth " + access});
		if (!r.ok() || r.body.find("\"scopes\"") == std::string::npos)
			return;
		granted = r.body;   // {"scopes":["bits:read",...], ...}
	} else {
		std::string out;
		if (kick_api("POST", "/token/introspect", "", &out) != 200)
			return;
		Data d = data_json(out);
		Data dd(d ? obs_data_get_obj(d.get(), "data") : nullptr);
		if (!dd || !obs_data_get_bool(dd.get(), "active"))
			return;
		granted = " " + str(dd.get(), "scope") + " ";   // "user:read channel:read ..."
	}
	std::string missing;
	auto look = [&](const std::pair<const char *, const char *> &sc) {
		std::string q = pf == "twitch" ? "\"" + std::string(sc.first) + "\"" : " " + std::string(sc.first) + " ";
		if (granted.find(q) == std::string::npos && missing.find(sc.second) == std::string::npos)
			missing += (missing.empty() ? "" : ", ") + std::string(sc.second);
	};
	if (pf == "twitch")
		for (auto &sc : TWITCH_SCOPES)
			look(sc);
	else
		for (auto &sc : KICK_SCOPES)
			look(sc);
	if (!missing.empty())
		warn("%s: this sign-in can't do %s yet (sign in again to allow it)", pname(pf).c_str(), missing.c_str());
	bool changed;
	{
		std::lock_guard<std::mutex> g(m_);
		std::string &m = pf == "kick" ? kick_missing_ : twitch_missing_;
		changed = m != missing;
		m = missing;
	}
	if (changed)
		notify();
}

void Hub::kick_event(const std::string &event, const std::string &data)
{
	std::string ev = event.substr(event.find_last_of('\\') == std::string::npos ? 0 : event.find_last_of('\\') + 1);
	Data d = data_json(data);
	if (!d)
		return;
	obs_data_t *o = d.get();
	HubEvent e;
	if (ev == "ChatMessageEvent") {
		Data s(obs_data_get_obj(o, "sender"));
		Data id(s ? obs_data_get_obj(s.get(), "identity") : nullptr);
		bool trusted = false;
		obs_data_array_t *badges = id ? obs_data_get_array(id.get(), "badges") : nullptr;
		for (size_t i = 0; badges && i < obs_data_array_count(badges); i++) {
			Data b(obs_data_array_item(badges, i));
			std::string t = str(b.get(), "type");
			if (t == "broadcaster" || t == "moderator" || t == "vip")
				trusted = true;
		}
		obs_data_array_release(badges);
		hear("kick", s ? str(s.get(), "username") : "", kick_text(str(o, "content")), id ? str(id.get(), "color") : "", str(o, "id"),
		     trusted);
		return;
	}
	if (ev == "MessageDeletedEvent") {
		Data m(obs_data_get_obj(o, "message"));
		std::string id = m ? str(m.get(), "id") : "";
		if (!id.empty())
			mark_gone(id, "");
		return;
	}
	if (ev == "UserBannedEvent") {   // bans and timeouts
		Data u(obs_data_get_obj(o, "user"));
		std::string who = u ? str(u.get(), "username") : "";
		if (!who.empty())
			mark_gone("", who);
		return;
	}
	if (ev == "ChatroomClearEvent") {
		mark_gone("", "");
		return;
	}
	if (ev == "SubscriptionEvent") {
		e.kind = "sub";
		e.user = str(o, "username");
	} else if (ev == "GiftedSubscriptionsEvent") {
		Data c(obs_data_get_obj(o, "chunk_details"));
		std::string cid = c ? str(c.get(), "correlation_id") : "";
		if (!cid.empty()) {
			std::lock_guard<std::mutex> g(m_);
			if (!kick_gifts_.insert(cid).second)
				return;
			if (kick_gifts_.size() > 200)
				kick_gifts_.clear();
		}
		obs_data_array_t *names = obs_data_get_array(o, "gifted_usernames");
		long long n = obs_data_get_int(o, "gifted_total");
		if (n <= 0)
			n = names ? (long long)obs_data_array_count(names) : 1;
		obs_data_array_release(names);
		e.kind = "gift";
		e.n = std::max(1, count_of(n));
		e.user = str(o, "gifter_username");
	} else if (ev == "StreamHostedEvent") {
		Data u(obs_data_get_obj(o, "user"));
		Data m(obs_data_get_obj(o, "message"));
		e.kind = "raid";
		e.user = u ? str(u.get(), "username") : str(o, "host_username");
		long long n = obs_data_get_int(o, "number_viewers");
		if (!n && m)
			n = obs_data_get_int(m.get(), "numberOfViewers");
		e.n = count_of(n);
	} else if (ev == "RewardRedeemedEvent") {
		e.t = HubEvent::T::Redeem;
		e.title = str(o, "reward_title");
		e.user = str(o, "username");
	} else if (ev == "KicksGifted") {
		// Kicks are Kick's bits (a hundred to the dollar): the pet reacts the same way
		Data s(obs_data_get_obj(o, "sender"));
		Data g(obs_data_get_obj(o, "gift"));
		e.kind = "bits";
		e.n = g ? count_of(obs_data_get_int(g.get(), "amount")) : 0;
		e.user = s ? str(s.get(), "username") : "";
		e.color = s ? str(s.get(), "username_color") : "";
	} else {
		return;
	}
	info("Kick: %s%s%s", e.t == HubEvent::T::Redeem ? "redemption " : e.kind.c_str(),
	     e.t == HubEvent::T::Redeem ? e.title.c_str() : "", e.user.empty() ? "" : (" from " + e.user).c_str());
	broadcast(e);
}

void Hub::kick_loop()
{
	double retry = 2;
	std::string key = KICK_PUSHER_KEY, cluster = KICK_PUSHER_CLUSTER;
	bool asked_key = false;
	while (!stop_) {
		int gen = kick_gen_;
		bool have;
		{
			std::lock_guard<std::mutex> g(m_);
			have = acct_.valid() && acct_.kick.on();
		}
		if (!have) {
			nap(1, stop_);
			continue;
		}
		auto failed = [&](const char *why) {
			warn("Kick: %s; trying again in %.0f s", why, retry);
			{
				std::lock_guard<std::mutex> g(m_);
				kick_live_ = false;
				if (gen == kick_gen_ && acct_.kick.on() && ++kick_fails_ >= 2)
					message_ = std::string(CANT_LISTEN_KICK) + " It keeps trying by itself.";
			}
			notify();
			nap(retry, stop_, [&] { return gen == kick_gen_; });
			retry = std::min(300.0, retry * 2);
		};
		if (!kick_ids()) {
			failed("couldn't find the channel's chat");
			continue;
		}
		std::string channel, room, slug;
		{
			std::lock_guard<std::mutex> g(m_);
			channel = acct_.kick.channel_id;
			room = acct_.kick.chatroom_id;
			slug = acct_.kick.login;
			if (!safe_id(channel) || !safe_id(room)) {   // (kept in account.json: look them up again)
				acct_.kick.channel_id.clear();
				acct_.kick.chatroom_id.clear();
			}
		}
		if (!safe_id(channel) || !safe_id(room)) {
			failed("the channel's chat ids look wrong");
			continue;
		}
		// Kick's current socket key, once per run (the built-in one otherwise)
		if (!asked_key) {
			asked_key = true;
			HttpResult r = http("POST", "https://web.kick.com/api/v1/realtime/channels/" + channel + "/chat/connection",
					    {"Content-Type: application/json"},
					    "{\"client\":{\"id\":\"obspet\",\"type\":\"web\"},\"capabilities\":{\"accepted_providers\":[{\"provider\":\"pusher\"}]}}");
			Data d = data_json(r.body);
			Data dd(d ? obs_data_get_obj(d.get(), "data") : nullptr);
			obs_data_array_t *cs = dd ? obs_data_get_array(dd.get(), "connections") : nullptr;
			for (size_t i = 0; cs && i < obs_data_array_count(cs); i++) {
				Data c(obs_data_array_item(cs, i));
				Data cr(obs_data_get_obj(c.get(), "credentials"));
				if (str(c.get(), "provider") == "pusher" && cr && safe_id(str(cr.get(), "app_key"))) {
					key = str(cr.get(), "app_key");
					if (safe_id(str(cr.get(), "cluster"), 16))
						cluster = str(cr.get(), "cluster");
				}
			}
			obs_data_array_release(cs);
			std::lock_guard<std::mutex> g(m_);   // (OBSPETS Chat uses the same)
			kick_key_ = key;
			kick_cluster_ = cluster;
		}
		WebSocket ws;
		std::string err;
		if (!ws.connect("wss://ws-" + cluster + ".pusher.com/app/" + key + "?protocol=7&client=js&version=8.4.0&flash=false", err)) {
			failed(err.c_str());
			continue;
		}
		double activity = 120, last_msg = now_s(), pinged = 0;
		bool live = false;
		while (!stop_ && gen == kick_gen_) {
			std::string text;
			int got = ws.recv(text, 500);
			if (got < 0) {
				warn("Kick: connection closed; reconnecting");
				break;
			}
			double t = now_s();
			if (got == 0) {
				// quiet: ping after Pusher's activity timeout, give up 30 s after that
				if (!pinged && t - last_msg > activity) {
					ws.send("{\"event\":\"pusher:ping\",\"data\":{}}");
					pinged = t;
				} else if (pinged && t - pinged > 30) {
					warn("Kick: no answer; reconnecting");
					break;
				}
				continue;
			}
			last_msg = t;
			pinged = 0;
			Data msg = data_json(text);
			if (!msg)
				continue;
			std::string ev = str(msg.get(), "event"), data = str(msg.get(), "data");
			if (ev == "pusher:connection_established") {
				Data dd = data_json(data);
				if (dd && obs_data_get_int(dd.get(), "activity_timeout") > 0)
					activity = (double)obs_data_get_int(dd.get(), "activity_timeout");
				for (std::string ch : {"chatrooms." + room + ".v2", "chatroom_" + room, "chatrooms." + room, "channel_" + channel,
						       "channel." + channel})
					ws.send("{\"event\":\"pusher:subscribe\",\"data\":{\"auth\":\"\",\"channel\":\"" + ch + "\"}}");
			} else if (ev == "pusher_internal:subscription_succeeded") {
				if (!live) {
					live = true;
					retry = 2;
					info("Kick: listening to kick.com/%s", slug.c_str());
					{
						std::lock_guard<std::mutex> g(m_);
						kick_live_ = true;
						kick_fails_ = 0;
						if (message_.rfind(CANT_LISTEN_KICK, 0) == 0)
							message_.clear();
					}
					notify();
				}
			} else if (ev == "pusher:ping") {
				ws.send("{\"event\":\"pusher:pong\",\"data\":{}}");
			} else if (ev == "pusher:error") {
				warn("Kick: socket error %.200s", text.c_str());
			} else if (!ev.empty() && ev.rfind("pusher", 0) != 0) {
				kick_event(ev, data);
			}
		}
		ws.close();
		if (live) {
			{
				std::lock_guard<std::mutex> g(m_);
				kick_live_ = false;
			}
			notify();
			nap(1, stop_, [&] { return gen == kick_gen_; });
		} else if (!stop_ && gen == kick_gen_) {
			failed("couldn't listen to the channel");
		}
	}
}

} // namespace obspet
