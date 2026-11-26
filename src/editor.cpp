// The character editor's side in the plugin (see editor.hpp). It hands the page what a pack
// holds, stores the files the page makes, and tells the Pets when a character changed.
//
// It only ever writes inside the user's packs folder (plugin_config/obspet/packs/<id>/), only
// files with the names a pack made here uses, and moves anything it replaces or deletes into
// that pack's .history/ instead of throwing it away.
#include "editor.hpp"
#include "pack.hpp"
#include "pet.hpp"
#include "util.hpp"

#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <mutex>
#include <regex>
#include <set>

namespace obspet {

namespace {

bool good_id(const std::string &id)
{
	static const std::regex re("^[a-z0-9][a-z0-9_-]{0,39}$");
	return std::regex_match(id, re);
}

// the only files a pack made in the editor has (source/sheet.png: the sprite sheet its animations
// were picked from, so the sheet dialog opens again; Export leaves it out)
bool good_path(const std::string &p)
{
	static const std::regex re("^(pack\\.json|pack\\.lua|sheet\\.png|source/sheet\\.png|anims/[a-z0-9]{1,16}\\.png|"
				   "sounds/[a-z]{1,16}/[a-z0-9][a-z0-9_-]{0,47}\\.wav)$");
	return std::regex_match(p, re);
}

bool good_test(const std::string &t)
{
	static const std::regex re("^(follow|sub|pet|gift:[0-9]{1,3}|bits:[0-9]{1,6}|raid:[0-9]{1,5}|mood:[a-z_]{1,20})$");
	return std::regex_match(t, re);
}

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

std::string num(double v)
{
	char b[32];
	snprintf(b, sizeof b, "%.6g", v);
	return b;
}

std::string read_all(const std::string &path)
{
	FILE *f = os_fopen(path.c_str(), "rb");
	if (!f)
		return "";
	std::string out;
	char buf[16384];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		out.append(buf, n);
	fclose(f);
	return out;
}

bool write_all(const std::string &path, const std::string &data)
{
	std::string tmp = path + ".tmp";
	FILE *f = os_fopen(tmp.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	ok = fclose(f) == 0 && ok;
	if (!ok || os_rename(tmp.c_str(), path.c_str()) != 0) {
		os_unlink(tmp.c_str());
		return false;
	}
	return true;
}

std::string stamp(time_t t = time(nullptr))
{
	struct tm tm_;
	localtime_s(&tm_, &t);
	char b[32];
	strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm_);
	return b;
}

std::string parent(const std::string &path)
{
	size_t i = path.find_last_of('/');
	return i == std::string::npos ? "" : path.substr(0, i);
}

// the names of the folders in `dir` (not the hidden ones), sorted
std::vector<std::string> subdirs(const std::string &dir)
{
	std::vector<std::string> out;
	if (os_dir_t *d = os_opendir(dir.c_str())) {
		while (os_dirent *e = os_readdir(d)) {
			std::string n = e->d_name;
			if (e->directory && !n.empty() && n[0] != '.')
				out.push_back(n);
		}
		os_closedir(d);
	}
	std::sort(out.begin(), out.end());
	return out;
}

// a symbolic link or a junction (deleting one removes only the link, never what it points at)
bool is_link(const std::string &path)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n : 1, L'\0');
	if (n > 0)
		MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
	DWORD a = GetFileAttributesW(w.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// deletes a folder and everything in it, without following links out of it
void rm_tree(const std::string &dir)
{
	if (is_link(dir)) {
		if (os_rmdir(dir.c_str()) != 0)
			os_unlink(dir.c_str());
		return;
	}
	if (os_dir_t *d = os_opendir(dir.c_str())) {
		while (os_dirent *e = os_readdir(d)) {
			std::string n = e->d_name;
			if (n == "." || n == "..")
				continue;
			std::string p = dir + "/" + n;
			if (e->directory)
				rm_tree(p);
			else
				os_unlink(p.c_str());
		}
		os_closedir(d);
	}
	os_rmdir(dir.c_str());
}

// .history keeps how a pack looked before each editing session, not every autosave: a session
// is one folder, a file goes in it only the first time it's replaced, and only the newest few
// sessions stay
const int KEEP_SESSIONS = 10;
const double SESSION_IDLE_S = 10 * 60;   // a pause this long starts a new session
const int TRASH_DAYS = 30;               // a deleted character is gone for good after this

struct Session {
	std::string when;             // its .history/<when>/ folder, made on the first file kept
	std::set<std::string> seen;   // files kept (or first written) this session
	double last = 0;
};
std::mutex sessions_mu;
std::map<std::string, Session> sessions;   // by pack folder

// one change to the packs folder at a time (two saves of one file at once would share its .tmp)
std::mutex write_mu;

// a file the editor is about to replace or delete goes to <pack>/.history/<session>/<path>,
// unless this session already kept (or made) it
void keep_old(const std::string &dir, const std::string &rel)
{
	std::string from = dir + rel;
	std::lock_guard<std::mutex> g(sessions_mu);
	Session &s = sessions[dir];
	double t = now_s();
	if (s.last == 0 || t - s.last > SESSION_IDLE_S)
		s = Session();
	s.last = t;
	bool first = s.seen.insert(rel).second;
	if (!file_exists(from))
		return;   // (one this session makes: there's nothing before it to keep)
	if (!first) {
		os_unlink(from.c_str());
		return;
	}
	// sheet.png and pack.lua are made from the rest, once the editor has a pack: before that
	// (a pack written by hand that the editor takes over) they're the user's own work and kept
	bool made = file_exists(dir + "pack.json");
	if (rel.rfind("anims/", 0) != 0 && rel.rfind("sounds/", 0) != 0 && (made || rel == "pack.json")) {
		os_unlink(from.c_str());
		return;
	}
	bool fresh = s.when.empty();
	if (fresh)
		s.when = stamp();
	std::string to = dir + ".history/" + s.when + "/" + rel;
	os_mkdirs(parent(to).c_str());
	if (os_rename(from.c_str(), to.c_str()) != 0)
		os_unlink(from.c_str());
	if (fresh && !is_link(dir + ".history")) {
		auto all = subdirs(dir + ".history");
		for (size_t i = 0; i + KEEP_SESSIONS < all.size(); i++)
			rm_tree(dir + ".history/" + all[i]);
	}
}

// deleted characters older than TRASH_DAYS go for good (each folder ends in -<when> it was deleted)
void empty_trash(const std::string &udir)
{
	std::string trash = udir + ".trash/";
	if (is_link(udir + ".trash"))
		return;   // (only ever empties a folder of its own)
	std::string cutoff = stamp(time(nullptr) - (time_t)TRASH_DAYS * 24 * 3600);
	static const std::regex re("^.+-([0-9]{8}-[0-9]{6})$");
	for (auto &n : subdirs(trash)) {
		std::smatch m;
		if (std::regex_match(n, m, re) && m[1].str() < cutoff)
			rm_tree(trash + n);
	}
}

const PackEntry *find_pack(const std::vector<PackEntry> &all, const std::string &id)
{
	for (auto &p : all)
		if (p.id == id)
			return &p;
	return nullptr;
}

HttpResp json(const std::string &body, int status = 200)
{
	HttpResp r;
	r.status = status;
	r.body = body;
	return r;
}
HttpResp fail(int status, const std::string &why)
{
	return json("{\"error\":" + jstr(why) + "}", status);
}
// what the editor hands the page from a pack: only pictures and .wav files, checked by their first
// bytes. A pack.lua can name any file on the PC as its "sheet" or a sound; this keeps one from
// reading anything else into the editor (and from there into an exported .zip).
bool media(const std::string &b)
{
	auto starts = [&](const char *m, size_t n, size_t at = 0) { return b.size() >= at + n && b.compare(at, n, m, n) == 0; };
	return starts("\x89PNG\r\n\x1a\n", 8) || starts("GIF8", 4) || starts("\xff\xd8\xff", 3) || starts("BM", 2) ||
	       (starts("RIFF", 4) && (starts("WAVE", 4, 8) || starts("WEBP", 4, 8)));
}

HttpResp bytes(std::string body, const std::string &path)
{
	HttpResp r;
	std::string ext = lower(path.size() > 4 ? path.substr(path.size() - 4) : path);
	r.type = ext == ".png" ? "image/png" : ext == ".wav" ? "audio/wav" : ext == "json" ? "application/json; charset=utf-8" : "text/plain; charset=utf-8";
	r.body = std::move(body);
	return r;
}

// what a pack holds, for the page: everything pack.lua said, plus pack.json when the editor made it
std::string pack_json(const PackEntry &e)
{
	std::string err;
	auto P = load_pack(e.path, err);
	std::string editor = read_all(e.path + "pack.json");
	if (!editor.empty() && !data_json(editor))   // (only ever handed on as it is when it is JSON)
		editor.clear();
	std::string o = "{\"id\":" + jstr(e.id) + ",\"user\":" + (e.user ? "true" : "false");
	o += ",\"editor\":" + (editor.empty() ? std::string("null") : editor);
	if (!P)
		return o + ",\"error\":" + jstr(err) + "}";
	o += ",\"name\":" + jstr(P->name) + ",\"describe\":" + jstr(P->describe) + ",\"pronouns\":" + jstr(P->pronouns) +
	     ",\"personality\":" + jstr(P->personality);
	o += ",\"faces\":\"" + std::string(P->faces_left ? "left" : "right") + "\"";
	o += ",\"cell\":[" + num(P->cw) + "," + num(P->ch) + "],\"feet\":" + num(P->feet);
	{
		const Look &L = P->look;
		auto hex = [](uint32_t c) {
			char b[8];
			snprintf(b, sizeof b, "#%06x", (unsigned)(c & 0xffffff));
			return std::string(b);
		};
		o += ",\"hue\":" + num(L.hue) + ",\"saturation\":" + num(L.saturation) + ",\"contrast\":" + num(L.contrast) +
		     ",\"lightness\":" + num(L.lightness) + ",\"tint\":" + jstr(hex_of(L.tint)) + ",\"tint_amount\":" + num(L.tint_amount) +
		     ",\"recolor\":{";
		bool f1 = true;
		for (auto &kv : L.recolor) {
			o += (f1 ? "" : ",") + jstr(hex(kv.first)) + ":" + jstr(hex(kv.second));
			f1 = false;
		}
		o += "}";
	}
	o += ",\"body\":{\"half\":" + num(P->half) + ",\"top\":" + num(P->top) + ",\"top_asleep\":" + num(P->top_asleep) +
	     ",\"head\":" + num(P->head) + ",\"paw\":[" + num(P->paw_x) + "," + num(P->paw_y) + "],\"shadow\":" + num(P->shadow) + "}";
	o += ",\"speed\":{\"walk\":" + num(P->walk) + ",\"run\":" + num(P->run) + "}";
	o += ",\"anims\":{";
	bool first = true;
	for (auto &k : P->own_anims) {
		const Anim &a = P->anims.at(k);
		o += (first ? "" : ",") + jstr(k) + ":{\"fps\":" + num(a.fps) + ",\"whoosh\":" + num(a.whoosh) + ",\"hit\":" + num(a.hit) + ",\"cells\":[";
		for (size_t i = 0; i < a.cells.size(); i++)
			o += (i ? ",[" : "[") + num(a.cells[i].col) + "," + num(a.cells[i].row) + "]";
		o += "]}";
		first = false;
	}
	o += "},\"stands\":{";
	first = true;
	for (auto &kv : P->stand_ins) {
		o += (first ? "" : ",") + jstr(kv.first) + ":" + jstr(kv.second);
		first = false;
	}
	o += "},\"poses\":{";
	first = true;
	for (auto &k : P->own_poses) {
		const Cell &c = P->poses.at(k);
		o += (first ? "" : ",") + jstr(k) + ":[" + num(c.col) + "," + num(c.row) + "]";
		first = false;
	}
	o += "},\"sounds\":{";
	first = true;
	std::string dflt;
	for (auto &kv : P->sounds) {
		if (kv.second.fallback) {
			dflt += (dflt.empty() ? "" : ",") + jstr(kv.first);
			continue;
		}
		o += (first ? "" : ",") + jstr(kv.first) + ":[";
		for (size_t i = 0; i < kv.second.files.size(); i++) {
			const std::string &f = kv.second.files[i];
			size_t sl = f.find_last_of('/');
			o += (i ? "," : "") + jstr(sl == std::string::npos ? f : f.substr(sl + 1));
		}
		o += "]";
		first = false;
	}
	o += "},\"default_sounds\":[" + dflt + "],\"sound_set\":" + jstr(P->sound_set) + ",\"colors\":{";
	first = true;
	for (auto &kv : P->colors) {
		o += (first ? "" : ",") + jstr(kv.first) + ":" + jstr(hex_of(kv.second));
		first = false;
	}
	o += "},\"warnings\":[";
	for (size_t i = 0; i < P->warnings.size(); i++)
		o += (i ? "," : "") + jstr(P->warnings[i]);
	return o + "]}";
}

// every pack, and the ones a Pet shows from a folder of its own ("Or use a character folder from
// anywhere"), so the editor lists what's on screen. Those open like built-in ones: a change saves
// the user's own copy, and the folder stays as it was.
std::vector<PackEntry> editor_packs()
{
	auto all = list_packs();
	std::vector<std::pair<std::string, std::string>> shown;   // id, dir
	with_pets([&](PetSource &p) {
		for (auto &pk : p.packs_shown())
			shown.emplace_back(pk->id, pk->dir);
	});
	for (auto &s : shown) {
		bool known = false;
		for (auto &e : all)
			known = known || e.id == s.first;
		if (!known && !s.first.empty() && file_exists(s.second + "pack.lua"))
			all.push_back({s.first, s.second, false, true});
	}
	std::sort(all.begin(), all.end(), [](const PackEntry &a, const PackEntry &b) { return a.id < b.id; });
	return all;
}

// the characters the Pets in OBS show right now
std::vector<std::string> on_stream()
{
	std::vector<std::string> ids;
	with_pets([&](PetSource &p) {
		for (auto &pk : p.packs_shown())
			ids.push_back(pk->id);
	});
	return ids;
}

int reload(const std::string &id)
{
	int n = 0;
	with_pets([&](PetSource &p) {
		n++;
		p.reload_pack(id);
	});
	return n;
}

} // namespace

HttpResp editor_api(const HttpReq &r)
{
	std::string what = r.path.substr(12);   // after /editor/api/
	std::string id = r.q("id");
	std::string udir = user_packs_dir();
	bool get = r.method == "GET", post = r.method == "POST";

	if (what == "packs" && get) {
		empty_trash(udir);
		auto all = editor_packs();
		auto live = on_stream();
		std::string o = "{\"packs\":[";
		for (size_t i = 0; i < all.size(); i++) {
			std::string err;
			auto P = load_pack(all[i].path, err);
			bool here = file_exists(all[i].path + "pack.json");
			std::string name = P ? P->name : all[i].id;
			if (!P && here) {   // (one the editor is still making: no idle yet)
				Data d = data_json(read_all(all[i].path + "pack.json"));
				if (d && !str(d.get(), "name").empty())
					name = str(d.get(), "name");
			}
			bool shown = std::find(live.begin(), live.end(), all[i].id) != live.end();
			o += (i ? "," : "") + std::string("{\"id\":") + jstr(all[i].id) + ",\"name\":" + jstr(name) +
			     ",\"user\":" + (all[i].user ? "true" : "false") + ",\"folder\":" + (all[i].folder ? "true" : "false") +
			     ",\"made_here\":" + (here ? "true" : "false") +
			     ",\"on_stream\":" + (shown ? "true" : "false") + ",\"ok\":" + (P ? "true" : "false") + "}";
		}
		o += "],\"pets\":" + std::to_string(live.size()) + "}";
		return json(o);
	}

	// the plugin's sound sets, for roles a character leaves to them: {"set":{"role":["file.wav",...]}}
	if (what == "defaults" && get) {
		std::string o = "{";
		bool fs = true;
		for (auto &set : sound_sets()) {
			o += (fs ? "" : ",") + jstr(set.first) + ":{";
			bool first = true;
			for (auto &kv : set.second) {
				o += (first ? "" : ",") + jstr(kv.first) + ":[";
				for (size_t i = 0; i < kv.second.files.size(); i++) {
					const std::string &f = kv.second.files[i];
					o += (i ? "," : "") + jstr(f.substr(f.find_last_of('/') + 1));
				}
				o += "]";
				first = false;
			}
			o += "}";
			fs = false;
		}
		return json(o + "}");
	}
	if (what == "default-sound" && get) {
		auto &all = sound_sets();
		auto set = all.find(r.q("set").empty() ? "default" : r.q("set"));
		const SoundRole *snd = nullptr;
		if (set != all.end()) {
			auto it = set->second.find(r.q("role"));
			if (it != set->second.end())
				snd = &it->second;
		}
		int i = std::atoi(r.q("i").c_str());
		if (!snd || i < 0 || i >= (int)snd->files.size())
			return fail(404, "no such sound");
		return bytes(read_all(snd->files[i]), snd->files[i]);
	}

	if (!good_id(id))
		return fail(400, "bad id");
	auto all = editor_packs();
	const PackEntry *e = find_pack(all, id);
	std::string mine = udir + id + "/";   // where this id lives (or would) in the user's folder
	bool is_mine = e && e->user;

	if (get) {
		if (what == "pack") {
			if (!e)
				return fail(404, "no such character");
			return json(pack_json(*e));
		}
		if (what == "sheet") {
			if (!e)
				return fail(404, "no such character");
			std::string err;
			auto P = load_pack(e->path, err);
			if (!P)
				return fail(404, err);
			std::string b = read_all(P->sheet);
			if (!media(b))
				return fail(404, "the sprite sheet isn't a picture");
			return bytes(std::move(b), P->sheet);
		}
		if (what == "sound") {
			if (!e)
				return fail(404, "no such character");
			std::string err;
			auto P = load_pack(e->path, err);
			if (!P)
				return fail(404, err);
			auto it = P->sounds.find(r.q("role"));
			int i = std::atoi(r.q("i").c_str());
			if (it == P->sounds.end() || i < 0 || i >= (int)it->second.files.size())
				return fail(404, "no such sound");
			std::string b = read_all(it->second.files[i]);
			if (!media(b))
				return fail(404, "that sound isn't a .wav file");
			return bytes(std::move(b), it->second.files[i]);
		}
		if (what == "file") {
			std::string path = r.q("path");
			if (!is_mine || !good_path(path))
				return fail(404, "no such file");
			std::string body = read_all(mine + path);
			if (body.empty())
				return fail(404, "no such file");
			return bytes(std::move(body), path);
		}
		return fail(404, "unknown");
	}
	if (!post)
		return fail(400, "method");

	std::lock_guard<std::mutex> one_at_a_time(write_mu);

	// writing: only into the user's own folder, and never over a bundled pack's id
	if (what == "put" || what == "del") {
		std::string path = r.q("path");
		if (!good_path(path))
			return fail(400, "bad path");
		if (e && !e->user)
			return fail(403, "that's a built-in character: make a copy to change it");
		if (is_link(udir + id) || (path.find('/') != std::string::npos && is_link(mine + parent(path))))
			return fail(403, "that character's folder is a link: change it by hand");
		if (what == "del") {
			keep_old(mine, path);
			return json("{\"ok\":true}");
		}
		if (file_exists(mine + path) && read_all(mine + path) == r.body)
			return json("{\"ok\":true}");   // (unchanged: nothing to write or keep)
		os_mkdirs(parent(mine + path).c_str());
		keep_old(mine, path);
		if (!write_all(mine + path, r.body))
			return fail(500, "couldn't write " + path);
		return json("{\"ok\":true}");
	}
	if (what == "reveal") {   // a file of the user's own character, selected in its folder, to edit elsewhere
		std::string path = r.q("path");
		if (!is_mine || !good_path(path) || !file_exists(mine + path))
			return fail(404, "no such file");
		reveal_file(mine + path);
		return json("{\"ok\":true}");
	}
	if (what == "done") {   // the page finished writing: Pets showing it reload it
		reload(id);
		return json("{\"ok\":true}");
	}
	if (what == "use") {   // every OBSPETS source's first pet shows this character
		if (!e)
			return fail(404, "no such character");
		int n = 0;
		with_pets([&](PetSource &p) {
			n++;
			p.post([id](PetSource &q) {
				Data s(obs_source_get_settings(q.source));
				Pet &lead = *q.lead_;
				std::string nm = trim(str(s.get(), "name"));
				if (lead.pack && nm == lead.pack->name)
					obs_data_set_string(s.get(), "name", "");   // (it takes the new one's name)
				obs_data_set_string(s.get(), "pack", id.c_str());
				obs_data_set_string(s.get(), "pack_dir", "");
				lead.pack_key.clear();
				lead.update(s.get());
				q.profile_changed();
				q.status_dirty_ = true;
				q.refresh_properties();
			});
		});
		return json("{\"ok\":true,\"pets\":" + std::to_string(n) + "}");
	}
	if (what == "try") {
		std::string t = r.q("what");
		if (!good_test(t))
			return fail(400, "bad reaction");
		int n = 0;
		with_pets([&](PetSource &p) {
			bool shows = false;
			for (auto &pk : p.packs_shown())
				shows = shows || pk->id == id;
			if (shows) {
				p.test(t, id);
				n++;
			}
		});
		return json("{\"ok\":true,\"pets\":" + std::to_string(n) + "}");
	}
	if (what == "remove") {
		if (!is_mine)
			return fail(403, "only your own characters can be removed");
		std::string trash = udir + ".trash/";
		os_mkdirs(trash.c_str());
		std::string from = mine.substr(0, mine.size() - 1);
		if (os_rename(from.c_str(), (trash + id + "-" + stamp()).c_str()) != 0)
			return fail(500, "couldn't move it to the .trash folder");
		{
			std::lock_guard<std::mutex> g(sessions_mu);
			sessions.erase(mine);
		}
		empty_trash(udir);
		return json("{\"ok\":true}");
	}
	return fail(404, "unknown");
}

void open_editor(const std::string &pack)
{
	if (!chat_server::start())
		return;
	// the editor is the settings page's Characters view (#character:<id>, or #characters for the
	// one it picks); ids are [a-z0-9_-], so they go in the URL as they are
	std::string url = chat_server::base_url() + "/settings/?k=" + chat_server::key();
	url += good_id(pack) ? "#character:" + pack : std::string("#characters");
	open_url(url);
}

} // namespace obspet
