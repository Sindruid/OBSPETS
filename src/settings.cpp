// The settings page's side in the plugin (see settings.hpp). Serves live state for every OBSPETS
// and OBSPETS Chat source, applies partial updates through obs_source_update(), lists installed
// fonts, and runs source/account actions from the browser.
#include "settings.hpp"
#include "chat-source.hpp"
#include "hub.hpp"
#include "pack.hpp"
#include "pet.hpp"
#include "util.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace obspet {

namespace {

std::string jstr(const std::string &s)
{
	std::string o = "\"";
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			o += "\\\"";
			break;
		case '\\':
			o += "\\\\";
			break;
		case '\n':
			o += "\\n";
			break;
		case '\r':
			o += "\\r";
			break;
		case '\t':
			o += "\\t";
			break;
		default:
			if (c < 0x20) {
				char b[8];
				snprintf(b, sizeof b, "\\u%04x", c);
				o += b;
			} else {
				o += (char)c;
			}
		}
	}
	return o + "\"";
}

// {"angry":"Angry",...}: the reactions this character can't do, and the animations they need
static std::string cant_json(const Pack *P)
{
	std::string o = "{";
	if (P)
		for (int i = 0; i < NREACT_NEEDS; i++) {
			std::string need = P->lacks(REACT_NEEDS[i].reaction);
			if (!need.empty())
				o += (o.size() > 1 ? "," : "") + jstr(REACT_NEEDS[i].reaction) + ":" + jstr(need);
		}
	return o + "}";
}

std::string url_encode(const std::string &s)
{
	std::string out;
	char hex[4];
	for (unsigned char c : s) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			out += (char)c;
		} else {
			snprintf(hex, sizeof hex, "%%%02X", c);
			out += hex;
		}
	}
	return out;
}

HttpResp json_resp(const std::string &body, int status = 200)
{
	HttpResp r;
	r.status = status;
	r.body = body;
	return r;
}

HttpResp fail(int status, const std::string &why)
{
	return json_resp("{\"error\":" + jstr(why) + "}", status);
}

const char *reward_str(Reward r)
{
	switch (r) {
	case Reward::Checking:
		return "checking";
	case Reward::Missing:
		return "missing";
	case Reward::There:
		return "there";
	case Reward::Made:
		return "made";
	case Reward::NotAllowed:
		return "not_allowed";
	case Reward::Failed:
		return "failed";
	default:
		return "unknown";
	}
}

const char *chat_kind_str(ChatKind k)
{
	switch (k) {
	case ChatKind::Ours:
		return "ours";
	case ChatKind::Other:
		return "other";
	default:
		return "none";
	}
}

bool good_test(const std::string &t)
{
	static const std::regex re("^(follow|sub|pet|gift:[0-9]{1,3}|bits:[0-9]{1,6}|raid:[0-9]{1,5}|mood:[a-z_]{1,20})$");
	return std::regex_match(t, re);
}

// Runs `fn` on OBS's UI thread and waits up to 3 s, aborting promptly if the server is stopping
// (so obs_module_unload on the UI thread can never deadlock waiting for a request thread).
bool run_on_ui(std::function<void()> fn)
{
	if (chat_server::is_stopping())
		return false;
	struct State {
		std::mutex mu;
		std::condition_variable cv;
		bool done = false;
		std::function<void()> fn;
	};
	auto st = std::make_shared<State>();
	st->fn = std::move(fn);
	auto *heap = new std::shared_ptr<State>(st);
	obs_queue_task(
		OBS_TASK_UI,
		[](void *p) {
			auto *hp = static_cast<std::shared_ptr<State> *>(p);
			std::shared_ptr<State> s = std::move(*hp);
			delete hp;
			s->fn();
			{
				std::lock_guard<std::mutex> g(s->mu);
				s->done = true;
			}
			s->cv.notify_all();
		},
		heap, false);
	std::unique_lock<std::mutex> lk(st->mu);
	for (int i = 0; i < 30; i++) {
		if (st->cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return st->done; }))
			return true;
		if (chat_server::is_stopping())
			return false;
	}
	return st->done;
}

std::string narrow(const wchar_t *w)
{
	if (!w || !*w)
		return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return "";
	std::string out(n - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
	return out;
}

// Installed font family names, cached once per run.
const std::vector<std::string> &system_fonts()
{
	static std::once_flag once;
	static std::vector<std::string> fonts;
	std::call_once(once, [] {
		std::set<std::string> seen;
		HDC hdc = GetDC(nullptr);
		if (hdc) {
			LOGFONTW lf{};
			lf.lfCharSet = DEFAULT_CHARSET;
			EnumFontFamiliesExW(
				hdc, &lf,
				[](const LOGFONTW *f, const TEXTMETRICW *, DWORD, LPARAM lp) -> int {
					if (f && f->lfFaceName[0] && f->lfFaceName[0] != L'@')
						static_cast<std::set<std::string> *>(reinterpret_cast<void *>(lp))->insert(narrow(f->lfFaceName));
					return 1;
				},
				reinterpret_cast<LPARAM>(&seen), 0);
			ReleaseDC(nullptr, hdc);
		}
		fonts.assign(seen.begin(), seen.end());
		std::sort(fonts.begin(), fonts.end(), [](const std::string &a, const std::string &b) { return lower(a) < lower(b); });
	});
	return fonts;
}

// Native Windows folder picker on a short-lived STA thread.
bool pick_folder_dialog(std::string &out_path)
{
	bool picked = false;
	std::thread t([&] {
		if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
			return;
		IFileOpenDialog *dlg = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg))) && dlg) {
			DWORD opts = 0;
			if (SUCCEEDED(dlg->GetOptions(&opts)))
				dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
			dlg->SetTitle(L"Choose a character folder");
			if (SUCCEEDED(dlg->Show(nullptr))) {
				IShellItem *item = nullptr;
				if (SUCCEEDED(dlg->GetResult(&item)) && item) {
					PWSTR p = nullptr;
					if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
						out_path = narrow(p);
						picked = true;
						CoTaskMemFree(p);
					}
					item->Release();
				}
			}
			dlg->Release();
		}
		CoUninitialize();
	});
	t.join();
	return picked;
}

std::vector<std::string> read_avoid_items(obs_data_t *s)
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

std::string account_json(const HubStatus &st)
{
	std::string o = "{";
	o += "\"signed_in\":" + std::string(st.signed_in ? "true" : "false");
	o += ",\"linking\":" + std::string(st.linking ? "true" : "false");
	o += ",\"link_platform\":" + jstr(st.link_platform);
	o += ",\"login\":" + jstr(st.login);
	o += ",\"kick_login\":" + jstr(st.kick_login);
	o += ",\"twitch_live\":" + std::string(st.twitch_live ? "true" : "false");
	o += ",\"kick_live\":" + std::string(st.kick_live ? "true" : "false");
	o += ",\"code\":" + jstr(st.code);
	o += ",\"link_url\":" + jstr(st.link_url);
	o += ",\"message\":" + jstr(st.message);
	o += ",\"twitch_missing\":" + jstr(st.twitch_missing);
	o += ",\"kick_missing\":" + jstr(st.kick_missing);
	o += ",\"moods_server_on\":" + std::string(st.moods_server_on ? "true" : "false");
	o += ",\"mood_note\":" + jstr(st.mood_note);
	o += ",\"reward_title\":" + jstr(st.reward_title);
	o += ",\"reward\":" + jstr(reward_str(st.reward));
	o += ",\"reward_msg\":" + jstr(st.reward_msg);
	o += ",\"kick_reward\":" + jstr(reward_str(st.kick_reward));
	o += ",\"kick_reward_msg\":" + jstr(st.kick_reward_msg);
	o += std::string(",\"automod_on\":") + (st.automod_on ? "true" : "false");
	o += ",\"automod\":[";
	for (size_t i = 0; i < st.automod.size(); i++)
		o += (i ? "," : "") + jstr(st.automod[i]);
	o += "]";
	o += ",\"ai_gap\":" + std::to_string((int)std::lround(st.ai_gap));
	o += std::string(",\"react_on\":") + (st.react_on ? "true" : "false");
	o += ",\"daily_cap\":" + std::to_string(st.daily_cap);
	return o + "}";
}

HttpResp handle_state(const HttpReq &r)
{
	bool do_check = r.q("check") == "1";
	HubStatus st = Hub::get().status();

	std::string sources_json = "[";
	std::string pics_json = "[";
	std::string first_reward;

	bool ok = run_on_ui([&] {
		struct Pic {
			std::string name;
			bool is_chat = false;
		};
		struct SrcInfo {
			std::string kind, name, settings_json, meta_json;
			bool active = false;
		};
		std::vector<Pic> pics;
		std::vector<SrcInfo> srcs;

		struct Ctx {
			std::vector<Pic> *pics;
			std::vector<SrcInfo> *srcs;
			std::string *first_reward;
		} ctx{&pics, &srcs, &first_reward};

		obs_enum_sources(
			[](void *p, obs_source_t *s) {
				auto *c = static_cast<Ctx *>(p);
				const char *raw_name = obs_source_get_name(s);
				if (!raw_name || !*raw_name)
					return true;
				std::string name = raw_name;
				const char *id = obs_source_get_unversioned_id(s);
				bool is_pet = id && strcmp(id, PET_SOURCE_ID) == 0;
				bool is_chat = id && strcmp(id, CHAT_SOURCE_ID) == 0;
				if ((obs_source_get_output_flags(s) & OBS_SOURCE_VIDEO) && !is_pet)
					c->pics->push_back({name, is_chat});

				if (is_pet) {
					PetSource *pet = PetSource::from(s);
					Data cfg(obs_source_get_settings(s));
					const char *sj = cfg ? obs_data_get_json_with_defaults(cfg.get()) : nullptr;

					PackPtr P;
					std::string pack_err, auto_chat, auto_alert;
					bool moved = false;
					if (pet) {
						std::lock_guard<std::mutex> g(pet->mu_);
						P = pet->ui.pack;
						pack_err = pet->ui.pack_err;
						auto_chat = pet->ui.auto_chat;
						auto_alert = pet->ui.auto_alert;
						moved = pet->ui.moved;
					}
					std::string chat_name;
					ChatKind ck = pet_chat_kind(pet, cfg.get(), chat_name);
					std::string eff_name = pet_name_now(pet, cfg.get());
					std::string rew = pet_reward_now(pet, cfg.get());
					if (c->first_reward->empty() && !rew.empty())
						*c->first_reward = rew;

					auto avoids = read_avoid_items(cfg.get());
					std::string avoid_arr = "[";
					for (size_t i = 0; i < avoids.size(); i++)
						avoid_arr += (i ? "," : "") + jstr(avoids[i]);
					avoid_arr += "]";

					std::string meta = "{";
					meta += "\"pack_ok\":" + std::string(P ? "true" : "false");
					meta += ",\"pack_id\":" + jstr(P ? P->id : "");
					meta += ",\"pack_name\":" + jstr(P ? P->name : "");
					meta += ",\"pack_err\":" + jstr(pack_err);
					meta += ",\"pack_status\":" + jstr(pet ? pet->pack_status() : "");
					meta += ",\"pack_cant\":" + cant_json(P.get());
					meta += ",\"pack_has_sounds\":" + std::string(P && P->nsounds > 0 ? "true" : "false");
					meta += ",\"auto_chat\":" + jstr(auto_chat);
					meta += ",\"auto_alert\":" + jstr(auto_alert);
					meta += ",\"moved\":" + std::string(moved ? "true" : "false");
					meta += ",\"chat_kind\":" + jstr(chat_kind_str(ck));
					meta += ",\"chat_name\":" + jstr(chat_name);
					meta += ",\"effective_name\":" + jstr(eff_name);
					meta += ",\"reward_now\":" + jstr(rew);
					meta += ",\"avoid_items\":" + avoid_arr;
					meta += "}";
					c->srcs->push_back({"pet", name, sj ? sj : "{}", meta, obs_source_active(s)});
				} else if (is_chat) {
					ChatSource *chat = ChatSource::from(s);
					Data cfg(obs_source_get_settings(s));
					const char *sj = cfg ? obs_data_get_json_with_defaults(cfg.get()) : nullptr;
					std::string meta = "{";
					meta += "\"status\":" + jstr(chat ? chat->status_text() : "");
					meta += ",\"has_browser\":" + std::string(chat && chat->has_browser() ? "true" : "false");
					meta += "}";
					c->srcs->push_back({"chat", name, sj ? sj : "{}", meta, obs_source_active(s)});
				}
				return true;
			},
			&ctx);

		std::sort(pics.begin(), pics.end(), [](const Pic &a, const Pic &b) { return lower(a.name) < lower(b.name); });
		// Pets first, then Chats, sorted by name within each kind
		std::sort(srcs.begin(), srcs.end(), [](const SrcInfo &a, const SrcInfo &b) {
			if (a.kind != b.kind)
				return a.kind == "pet";
			return lower(a.name) < lower(b.name);
		});

		for (size_t i = 0; i < srcs.size(); i++) {
			auto &it = srcs[i];
			sources_json += (i ? "," : "") + std::string("{\"kind\":") + jstr(it.kind) + ",\"name\":" + jstr(it.name) +
					",\"active\":" + (it.active ? "true" : "false") + ",\"settings\":" + it.settings_json +
					",\"meta\":" + it.meta_json + "}";
		}
		sources_json += "]";

		for (size_t i = 0; i < pics.size(); i++) {
			pics_json += (i ? "," : "") + std::string("{\"name\":") + jstr(pics[i].name) +
				     ",\"is_obspet_chat\":" + (pics[i].is_chat ? "true" : "false") + "}";
		}
		pics_json += "]";
	});

	if (!ok)
		return fail(503, "OBS is busy or closing");

	if (do_check && st.signed_in && !st.linking && !first_reward.empty())
		Hub::get().check_reward(first_reward);

	auto packs = list_packs();
	std::string packs_json = "[";
	for (size_t i = 0; i < packs.size(); i++) {
		std::string err;
		auto P = load_pack(packs[i].path, err);
		std::string name = P ? P->name : packs[i].id;
		packs_json += (i ? "," : "") + std::string("{\"id\":") + jstr(packs[i].id) + ",\"name\":" + jstr(name) +
			      ",\"user\":" + (packs[i].user ? "true" : "false") + ",\"ok\":" + (P ? "true" : "false") +
			      ",\"nsounds\":" + std::to_string(P ? P->nsounds : 0) + ",\"cant\":" + cant_json(P.get()) + "}";
	}
	packs_json += "]";

	std::string o = "{";
	o += "\"version\":" + jstr(PLUGIN_VERSION);
	o += ",\"backend\":" + jstr(Hub::backend());
	o += std::string(",\"no_echo\":") + (obs_prevents_monitoring_echo() ? "true" : "false");
	o += ",\"default_bots\":" + jstr(DEFAULT_BOTS);
	o += ",\"account\":" + account_json(st);
	o += ",\"sources\":" + sources_json;
	o += ",\"picture_sources\":" + pics_json;
	o += ",\"packs\":" + packs_json;
	o += "}";
	return json_resp(o);
}

HttpResp handle_fonts()
{
	const auto &fonts = system_fonts();
	std::string o = "{\"fonts\":[";
	for (size_t i = 0; i < fonts.size(); i++)
		o += (i ? "," : "") + jstr(fonts[i]);
	o += "]}";
	return json_resp(o);
}

HttpResp handle_update(const HttpReq &r)
{
	std::string name = r.q("name");
	if (name.empty())
		return fail(400, "missing source name");
	Data patch = data_json(r.body);
	if (!patch)
		return fail(400, "invalid json");

	std::string err, renamed, check_rew;
	bool ok = run_on_ui([&] {
		SourceRef src(obs_get_source_by_name(name.c_str()));
		if (!src) {
			err = "source not found";
			return;
		}
		PetSource *pet = PetSource::from(src.get());
		ChatSource *chat = ChatSource::from(src.get());
		if (!pet && !chat) {
			err = "not an OBSPETS source";
			return;
		}
		if (pet) {
			// Sanitize avoid_list and clear legacy comma-separated "avoid" when updated
			if (obs_data_has_user_value(patch.get(), "avoid_list")) {
				obs_data_array_t *in = obs_data_get_array(patch.get(), "avoid_list");
				obs_data_array_t *out = obs_data_array_create();
				std::set<std::string> seen;
				for (size_t i = 0; in && i < obs_data_array_count(in); i++) {
					obs_data_t *it = obs_data_array_item(in, i);
					std::string val = trim(str(it, "value"));
					obs_data_release(it);
					if (!val.empty() && seen.insert(val).second) {
						Data row = data_new();
						obs_data_set_string(row.get(), "value", val.c_str());
						obs_data_array_push_back(out, row.get());
					}
				}
				obs_data_array_release(in);
				obs_data_set_array(patch.get(), "avoid_list", out);
				obs_data_array_release(out);
				obs_data_set_string(patch.get(), "avoid", "");
			}

			// Smart pet 1 rename when switching character (if name wasn't customized)
			if (obs_data_has_user_value(patch.get(), "pack") && !obs_data_has_user_value(patch.get(), "name")) {
				Data cur(obs_source_get_settings(src.get()));
				std::string pdir = obs_data_has_user_value(patch.get(), "pack_dir") ? trim(str(patch.get(), "pack_dir"))
												    : trim(str(cur.get(), "pack_dir"));
				if (pdir.empty()) {
					std::string old_name, cur_id;
					{
						std::lock_guard<std::mutex> g(pet->mu_);
						old_name = pet->ui.pack ? pet->ui.pack->name : "";
						cur_id = pet->ui.pack ? pet->ui.pack->id : "";
					}
					std::string nm = trim(str(cur.get(), "name"));
					std::string new_id = str(patch.get(), "pack");
					if (!new_id.empty() && new_id != cur_id && (nm.empty() || nm == old_name)) {
						std::string want = pet_pack_name(new_id);
						if (want != nm) {
							obs_data_set_string(patch.get(), "name", want.c_str());
							renamed = want;
						}
					}
				}
			}

			bool check = obs_data_has_user_value(patch.get(), "reward") || obs_data_has_user_value(patch.get(), "name") ||
				     !renamed.empty();
			obs_source_update(src.get(), patch.get());
			if (check) {
				Data cur(obs_source_get_settings(src.get()));
				check_rew = pet_reward_now(pet, cur.get());
			}
		} else {
			obs_source_update(src.get(), patch.get());
		}
	});

	if (!ok)
		return fail(503, "OBS is busy or closing");
	if (!err.empty())
		return fail(404, err);
	if (!check_rew.empty())
		Hub::get().check_reward(check_rew);

	std::string o = "{\"ok\":true";
	if (!renamed.empty())
		o += ",\"renamed\":" + jstr(renamed);
	return json_resp(o + "}");
}

HttpResp handle_action(const HttpReq &r)
{
	Data d = data_json(r.body);
	if (!d)
		return fail(400, "invalid json");
	std::string act = str(d.get(), "action");
	std::string source = str(d.get(), "source");

	if (act == "sign_in") {
		std::string pf = str(d.get(), "platform");
		if (pf != "twitch" && pf != "kick")
			return fail(400, "bad platform");
		Hub::get().sign_in(pf);
		return json_resp("{\"ok\":true}");
	}
	if (act == "sign_out") {
		Hub::get().sign_out();
		return json_resp("{\"ok\":true}");
	}
	if (act == "cancel_sign_in") {
		Hub::get().cancel_sign_in();
		return json_resp("{\"ok\":true}");
	}
	if (act == "open_sign_in_page") {
		Hub::get().open_sign_in_page();
		return json_resp("{\"ok\":true}");
	}
	if (act == "open_packs_folder") {
		std::string dir = user_packs_dir();
		os_mkdirs(dir.c_str());
		open_folder(dir);
		return json_resp("{\"ok\":true}");
	}
	if (act == "pick_folder") {
		std::string chosen;
		if (!pick_folder_dialog(chosen))
			return json_resp("{\"ok\":true,\"cancelled\":true}");
		return json_resp("{\"ok\":true,\"cancelled\":false,\"path\":" + jstr(chosen) + "}");
	}
	if (act == "add_pet") {
		std::string added;
		if (!run_on_ui([&] { added = add_pet_to_scene(); }))
			return fail(503, "OBS is busy or closing");
		if (added.empty())
			return fail(400, "couldn't add pet to the current scene");
		return json_resp("{\"ok\":true,\"name\":" + jstr(added) + "}");
	}
	if (act == "add_chat") {
		std::string added;
		if (!run_on_ui([&] {
			    SourceRef src(source.empty() ? nullptr : obs_get_source_by_name(source.c_str()));
			    PetSource *pet = src ? PetSource::from(src.get()) : nullptr;
			    added = add_chat_for_pet(pet);
		    }))
			return fail(503, "OBS is busy or closing");
		if (added.empty())
			return fail(400, "couldn't add OBSPETS Chat to the current scene");
		return json_resp("{\"ok\":true,\"name\":" + jstr(added) + "}");
	}

	// Actions targeting a specific source
	if (source.empty())
		return fail(400, "missing source");

	if (act == "test") {
		std::string what = str(d.get(), "what");
		if (!good_test(what))
			return fail(400, "bad reaction");
		std::string err;
		if (!run_on_ui([&] {
			    SourceRef src(obs_get_source_by_name(source.c_str()));
			    PetSource *pet = src ? PetSource::from(src.get()) : nullptr;
			    if (!pet) {
				    err = "pet source not found";
				    return;
			    }
			    pet->test(what);
		    }))
			return fail(503, "OBS is busy or closing");
		return err.empty() ? json_resp("{\"ok\":true}") : fail(404, err);
	}
	if (act == "try_chat") {
		std::string text = trim(str(d.get(), "text")), user = trim(str(d.get(), "user"));
		bool play = obs_data_get_bool(d.get(), "play");
		if (text.empty())
			return fail(400, "type a chat message to try");
		text = text.substr(0, 300);
		if (!user.empty() && user[0] == '@')
			user = user.substr(1);
		if (user.empty() || user.size() > 25)
			user = "viewer";
		PetSource *pet = nullptr;
		std::string err;
		if (!run_on_ui([&] {
			    SourceRef src(obs_get_source_by_name(source.c_str()));
			    pet = src ? PetSource::from(src.get()) : nullptr;
			    if (!pet)
				    err = "pet source not found";
		    }))
			return fail(503, "OBS is busy or closing");
		if (!err.empty())
			return fail(404, err);
		ChatTry t = Hub::get().try_chat(pet, user, text);   // (pet: only a key into the hub's profiles)
		if (!t.error.empty())
			return json_resp("{\"ok\":false,\"error\":" + jstr(t.error) + "}");
		bool played = false;
		if (play && t.kind != "none" && !t.prof.automod_only)
			run_on_ui([&] {
				SourceRef src(obs_get_source_by_name(source.c_str()));
				if (PetSource *ps = src ? PetSource::from(src.get()) : nullptr) {
					ps->try_mood(t.kind, user, text, t.del);
					played = true;
				}
			});
		std::string o = "{\"ok\":true,\"kind\":" + jstr(t.kind) + ",\"why\":" + jstr(t.why);
		o += std::string(",\"delete\":") + (t.del ? "true" : "false");
		o += std::string(",\"played\":") + (played ? "true" : "false");
		o += std::string(",\"moods_on\":") + (t.prof.on ? "true" : "false");
		o += std::string(",\"automod_only\":") + (t.prof.automod_only ? "true" : "false");
		o += std::string(",\"allow_delete\":") + (t.prof.allow_delete ? "true" : "false");
		return json_resp(o + "}");
	}
	if (act == "set_automod") {
		// {on, cats: ["bots", ...]}: the account's Auto mod (only known category names go through)
		static const std::set<std::string> CATS = {"bots", "scams", "slurs", "threats", "sexual", "self_promo", "flooding"};
		std::vector<std::string> cats;
		obs_data_array_t *arr = obs_data_get_array(d.get(), "cats");
		for (size_t i = 0; arr && i < obs_data_array_count(arr); i++) {
			obs_data_t *it = obs_data_array_item(arr, i);
			std::string k = str(it, "value");
			obs_data_release(it);
			if (CATS.count(k) && std::find(cats.begin(), cats.end(), k) == cats.end())
				cats.push_back(k);
		}
		obs_data_array_release(arr);
		Hub::get().set_automod(obs_data_get_bool(d.get(), "on"), cats);
		return json_resp("{\"ok\":true}");
	}
	if (act == "set_reacting") {
		// {on, gap, daily_cap}: the account's reactions to chat (Hub clamps the numbers)
		Hub::get().set_reacting(obs_data_get_bool(d.get(), "on"), (int)obs_data_get_int(d.get(), "gap"),
					(int)obs_data_get_int(d.get(), "daily_cap"));
		return json_resp("{\"ok\":true}");
	}
	if (act == "check_reward" || act == "make_reward") {
		std::string rew, err;
		if (!run_on_ui([&] {
			    SourceRef src(obs_get_source_by_name(source.c_str()));
			    PetSource *pet = src ? PetSource::from(src.get()) : nullptr;
			    if (!pet) {
				    err = "pet source not found";
				    return;
			    }
			    Data cfg(obs_source_get_settings(src.get()));
			    rew = pet_reward_now(pet, cfg.get());
		    }))
			return fail(503, "OBS is busy or closing");
		if (!err.empty())
			return fail(404, err);
		if (act == "make_reward")
			Hub::get().ensure_reward(rew);
		else
			Hub::get().check_reward(rew, true);
		return json_resp("{\"ok\":true}");
	}
	if (act == "fit_to_screen") {
		bool fitted = false;
		std::string err;
		if (!run_on_ui([&] {
			    SourceRef src(obs_get_source_by_name(source.c_str()));
			    PetSource *pet = src ? PetSource::from(src.get()) : nullptr;
			    if (!pet) {
				    err = "pet source not found";
				    return;
			    }
			    fitted = fit_to_screen(src.get());
			    pet->status_dirty_ = true;
		    }))
			return fail(503, "OBS is busy or closing");
		if (!err.empty())
			return fail(404, err);
		return json_resp(std::string("{\"ok\":") + (fitted ? "true" : "false") +
				 (fitted ? "" : ",\"error\":\"couldn't fit it to the screen (is it in a moved group?)\"") + "}");
	}
	if (act == "reload_pack") {
		std::string err;
		if (!run_on_ui([&] {
			    SourceRef src(obs_get_source_by_name(source.c_str()));
			    PetSource *pet = src ? PetSource::from(src.get()) : nullptr;
			    if (!pet) {
				    err = "pet source not found";
				    return;
			    }
			    pet->reload_pack();
		    }))
			return fail(503, "OBS is busy or closing");
		return err.empty() ? json_resp("{\"ok\":true}") : fail(404, err);
	}
	if (act == "reset_defaults") {
		std::string err;
		if (!run_on_ui([&] {
			    SourceRef src(obs_get_source_by_name(source.c_str()));
			    if (!src || (!PetSource::from(src.get()) && !ChatSource::from(src.get()))) {
				    err = "source not found";
				    return;
			    }
			    Data empty = data_new();
			    obs_source_reset_settings(src.get(), empty.get());
		    }))
			return fail(503, "OBS is busy or closing");
		return err.empty() ? json_resp("{\"ok\":true}") : fail(404, err);
	}
	return fail(400, "unknown action");
}

} // namespace

HttpResp settings_api(const HttpReq &r)
{
	std::string what = r.path.substr(14);   // after /settings/api/
	if (r.method == "GET") {
		if (what == "state")
			return handle_state(r);
		if (what == "fonts")
			return handle_fonts();
		return fail(404, "unknown");
	}
	if (r.method == "POST") {
		if (what == "update")
			return handle_update(r);
		if (what == "action")
			return handle_action(r);
		return fail(404, "unknown");
	}
	return fail(405, "method");
}

void open_settings(const std::string &kind, const std::string &name)
{
	if (!chat_server::start())
		return;
	std::string url = chat_server::base_url() + "/settings/?k=" + chat_server::key();
	if (!kind.empty() && !name.empty())
		url += "#" + kind + ":" + url_encode(name);
	open_url(url);
}

} // namespace obspet
