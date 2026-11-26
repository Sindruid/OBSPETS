// The loopback HTTP server for OBSPETS Chat pages (see chat-server.hpp).
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static const sock_t BAD_SOCK = INVALID_SOCKET;
static void close_sock(sock_t s) { closesocket(s); }

#include "chat-server.hpp"
#include "editor.hpp"
#include "settings.hpp"
#include "updater.hpp"
#include "util.hpp"

#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace obspet {

namespace {

std::mutex mu;
std::thread acceptor;
std::atomic<bool> stopping{false};
std::atomic<int> active{0};   // connection threads still running
sock_t listener = BAD_SOCK;
int the_port = 0;
std::string the_key;
bool started = false, ok = false;

const size_t MAX_HEAD = 32 * 1024;
const size_t MAX_BODY = 24 * 1024 * 1024;        // a character's sprite sheet or a sound from the editor
const size_t MAX_CHAT_BODY = 8 * 1024 * 1024;    // a chat page's report (a yanked line's picture at most)
const size_t MAX_SETTINGS_BODY = 1 * 1024 * 1024;

// The pages' files: only these, from the plugin's data/chat/, data/editor/ and data/settings/.
struct File {
	const char *dir, *name, *type;
} FILES[] = {
	{"chat", "index.html", "text/html; charset=utf-8"},
	{"chat", "chat.js", "text/javascript; charset=utf-8"},
	{"chat", "chat.css", "text/css; charset=utf-8"},
	{"editor", "index.html", "text/html; charset=utf-8"},
	{"editor", "editor.js", "text/javascript; charset=utf-8"},
	{"editor", "editor.css", "text/css; charset=utf-8"},
	{"settings", "index.html", "text/html; charset=utf-8"},
	{"settings", "settings.js", "text/javascript; charset=utf-8"},
	{"settings", "settings.css", "text/css; charset=utf-8"},
	{"update", "update.js", "text/javascript; charset=utf-8"},
};

std::string url_decode(const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == '+') {
			out += ' ';
		} else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
			out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

std::string read_file(const std::string &path)
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

const char *reason(int status)
{
	switch (status) {
	case 200:
		return "OK";
	case 302:
		return "Found";
	case 204:
		return "No Content";
	case 400:
		return "Bad Request";
	case 403:
		return "Forbidden";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	case 413:
		return "Payload Too Large";
	default:
		return "Error";
	}
}

// the key, compared in constant time (no early exit on the first wrong letter)
bool key_ok(const std::string &k)
{
	if (the_key.empty() || k.size() != the_key.size())
		return false;
	unsigned char d = 0;
	for (size_t i = 0; i < k.size(); i++)
		d |= (unsigned char)(k[i] ^ the_key[i]);
	return d == 0;
}

// Only a browser that asked for this PC's own address: a page on another site that points its own
// name at 127.0.0.1 (DNS rebinding) sends its own name as the Host, and gets nothing.
bool host_ok(const std::string &host)
{
	std::string p = ":" + std::to_string(the_port);
	return host == "127.0.0.1" + p || host == "localhost" + p;
}

// A browser names the page that sent a POST (Origin). Only our own pages may send one.
bool origin_ok(const std::string &origin)
{
	std::string p = ":" + std::to_string(the_port);
	return origin.empty() || origin == "http://127.0.0.1" + p || origin == "http://localhost" + p;
}

void send_all(sock_t s, const std::string &data)
{
	size_t at = 0;
	while (at < data.size()) {
		int n = send(s, data.data() + at, (int)std::min<size_t>(data.size() - at, 1 << 20), 0);
		if (n <= 0)
			return;
		at += (size_t)n;
	}
}

void respond(sock_t s, const HttpResp &r)
{
	std::string head = "HTTP/1.1 " + std::to_string(r.status) + " " + reason(r.status) + "\r\n";
	head += "Content-Type: " + r.type + "\r\n";
	head += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
	head += "Cache-Control: no-store\r\n";
	// The pages carry the key in their address: never hand it on (Referer), never let another site
	// show them in a frame (to trick a click), and never let it read what they answer.
	head += "Referrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n";
	head += "Content-Security-Policy: frame-ancestors 'none'\r\nCross-Origin-Resource-Policy: same-origin\r\n";
	if (!r.location.empty())
		head += "Location: " + r.location + "\r\n";
	head += "Connection: close\r\n\r\n";
	send_all(s, head + r.body);
}

HttpResp route(const HttpReq &r)
{
	HttpResp out;
	// The website's "Open the character editor" link only knows the port: redirect to the editor
	// with its key. serve() already rejected any other Host (see host_ok), so a rebinding page can't
	// read the key.
	if (r.method == "GET" && r.path == "/editor/open") {
		out.status = 302;
		out.type = "text/plain; charset=utf-8";
		out.location = "/editor/?k=" + the_key;
		return out;
	}
	// (?s=automod: opens on that page, for a pet; letters only)
	if (r.method == "GET" && r.path == "/settings/open") {
		std::string sec = r.q("s");
		bool ok = !sec.empty() && sec.size() < 20 &&
			  std::all_of(sec.begin(), sec.end(), [](char c) { return c >= 'a' && c <= 'z'; });
		out.status = 302;
		out.type = "text/plain; charset=utf-8";
		out.location = "/settings/?k=" + the_key + (ok ? "&s=" + sec : "");
		return out;
	}
	// (serve() checked the key of these before it read a body)
	// The Update card on the settings page and in the editor (updater.cpp)
	if (r.path == "/settings/api/self-update" || r.path == "/editor/api/self-update")
		return updater_api(r, r.path[1] == 's' ? "settings" : "editor");
	if (r.path.rfind("/chat/api/", 0) == 0)
		return chat_api(r);
	if (r.path.rfind("/editor/api/", 0) == 0)
		return editor_api(r);
	if (r.path.rfind("/settings/api/", 0) == 0)
		return settings_api(r);
	size_t sl = r.path.find('/', 1);
	if (r.method == "GET" && sl != std::string::npos) {
		std::string dir = r.path.substr(1, sl - 1), name = r.path.substr(sl + 1);
		if (name.empty())
			name = "index.html";
		for (auto &f : FILES)
			if (dir == f.dir && name == f.name) {
				std::string body = read_file(module_data_path((std::string(f.dir) + "/" + f.name).c_str()));
				if (body.empty())
					break;
				out.type = f.type;
				out.body = std::move(body);
				return out;
			}
	}
	out.status = 404;
	out.type = "text/plain; charset=utf-8";
	out.body = "not found";
	return out;
}

void serve(sock_t s)
{
	DWORD tmo = 5000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof tmo);
	std::string buf;
	char chunk[16384];
	size_t head_end = std::string::npos;
	while (head_end == std::string::npos) {
		int n = recv(s, chunk, sizeof chunk, 0);
		if (n <= 0 || stopping)
			return;
		buf.append(chunk, (size_t)n);
		head_end = buf.find("\r\n\r\n");
		if (head_end == std::string::npos && buf.size() > MAX_HEAD)
			return;
	}
	HttpReq r;
	std::string head = buf.substr(0, head_end);
	size_t eol = head.find("\r\n");
	std::string first = head.substr(0, eol);
	size_t sp1 = first.find(' '), sp2 = first.find(' ', sp1 + 1);
	if (sp1 == std::string::npos || sp2 == std::string::npos)
		return;
	r.method = first.substr(0, sp1);
	std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
	size_t qm = target.find('?');
	r.path = url_decode(target.substr(0, qm));
	if (qm != std::string::npos)
		for (auto &kv : split(target.substr(qm + 1), '&')) {
			size_t eq = kv.find('=');
			r.query[url_decode(kv.substr(0, eq))] = eq == std::string::npos ? "" : url_decode(kv.substr(eq + 1));
		}
	size_t len = 0;
	bool chunked = false;
	std::string origin;
	for (auto &line : split(head.substr(eol == std::string::npos ? head.size() : eol + 2), '\n')) {
		std::string l = lower(line);
		if (l.rfind("content-length:", 0) == 0) {
			std::string v = trim(l.substr(15));
			if (v.empty() || v.size() > 12 || v.find_first_not_of("0123456789") != std::string::npos) {
				respond(s, HttpResp{400, "text/plain", "bad length"});
				return;
			}
			len = (size_t)std::strtoull(v.c_str(), nullptr, 10);
		} else if (l.rfind("transfer-encoding:", 0) == 0) {
			chunked = true;
		} else if (l.rfind("host:", 0) == 0) {
			r.host = trim(l.substr(5));
		} else if (l.rfind("origin:", 0) == 0) {
			origin = trim(l.substr(7));
		}
	}
	// All of this is turned away before a body is read, so no web page can make OBS take in big
	// bodies it would only throw away.
	if (chunked) {
		respond(s, HttpResp{400, "text/plain", "no chunked bodies"});
		return;
	}
	if (!host_ok(r.host) || !origin_ok(origin)) {
		respond(s, HttpResp{403, "text/plain; charset=utf-8", "Open this from 127.0.0.1."});
		return;
	}
	bool chat = r.path.rfind("/chat/api/", 0) == 0, editor = r.path.rfind("/editor/api/", 0) == 0,
	     settings = r.path.rfind("/settings/api/", 0) == 0;
	if ((chat || editor || settings) && !key_ok(r.q("k"))) {
		respond(s, HttpResp{403, "application/json; charset=utf-8", "{\"error\":\"key\"}"});
		return;
	}
	if (r.method != "GET" && r.method != "POST") {
		respond(s, HttpResp{405, "text/plain", "method"});
		return;
	}
	if (len > (editor ? MAX_BODY : chat ? MAX_CHAT_BODY : settings ? MAX_SETTINGS_BODY : 0)) {
		respond(s, HttpResp{413, "text/plain", "too big"});
		return;
	}
	r.body = buf.substr(head_end + 4);
	while (r.body.size() < len) {
		int n = recv(s, chunk, sizeof chunk, 0);
		if (n <= 0 || stopping)
			return;
		r.body.append(chunk, (size_t)n);
	}
	r.body.resize(len);
	respond(s, route(r));
}

void accept_loop()
{
	while (!stopping) {
		fd_set rd;
		FD_ZERO(&rd);
		FD_SET(listener, &rd);
		timeval tv{0, 250000};
		int n = select((int)listener + 1, &rd, nullptr, nullptr, &tv);
		if (n <= 0)
			continue;
		sock_t c = accept(listener, nullptr, nullptr);
		if (c == BAD_SOCK)
			continue;
		if (active >= 32) {   // (nobody local needs this many at once)
			close_sock(c);
			continue;
		}
		active++;
		std::thread([c] {
			serve(c);
			close_sock(c);
			active--;
		}).detach();
	}
}

} // namespace

namespace chat_server {

bool start()
{
	std::lock_guard<std::mutex> g(mu);
	if (started)
		return ok;
	started = true;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		warn("chat server: no sockets");
		return false;
	}
	// Restarted by an update (updater.cpp): the same port and key as before, so the settings page
	// and the editor that were open find OBS again by themselves. Else a new key, on any free port.
	int want_port = 0;
	std::string want_key;
	bool resumed = updater_take_handoff(want_port, want_key) && want_key.size() == 64;
	if (resumed) {
		the_key = want_key;
	} else {
		std::random_device rd;   // (the OS's CSPRNG on MSVC)
		char hex[65];
		for (int i = 0; i < 32; i++)
			snprintf(hex + i * 2, 3, "%02x", (unsigned)(rd() & 0xFF));
		the_key = hex;
	}

	auto listen_on = [](int port) {
		sock_t l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (l == BAD_SOCK)
			return BAD_SOCK;
		sockaddr_in a{};
		a.sin_family = AF_INET;
		a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // this PC only
		a.sin_port = htons((u_short)port);            // 0: any free port
		if (bind(l, (sockaddr *)&a, sizeof a) != 0 || listen(l, 16) != 0) {
			close_sock(l);
			return BAD_SOCK;
		}
		return l;
	};
	listener = BAD_SOCK;
	if (resumed && want_port >= 1024 && want_port <= 65535)
		for (int i = 0; i < 10 && listener == BAD_SOCK; i++) {   // (the old OBS may only just have let go)
			if (i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			listener = listen_on(want_port);
		}
	bool same_port = listener != BAD_SOCK;
	if (!same_port)
		listener = listen_on(0);
	sockaddr_in a{};
	socklen_t al = sizeof a;
	if (listener == BAD_SOCK || getsockname(listener, (sockaddr *)&a, &al) != 0) {
		warn("chat server: couldn't listen on 127.0.0.1");
		if (listener != BAD_SOCK)
			close_sock(listener);
		listener = BAD_SOCK;
		return false;
	}
	the_port = ntohs(a.sin_port);
	if (resumed)
		updater_resumed(same_port);
	stopping = false;
	acceptor = std::thread(accept_loop);
	ok = true;
	info("chat server on 127.0.0.1:%d", the_port);
	return true;
}

void stop()
{
	std::lock_guard<std::mutex> g(mu);
	if (!ok)
		return;
	stopping = true;
	if (acceptor.joinable())
		acceptor.join();
	close_sock(listener);
	listener = BAD_SOCK;
	// connection threads end within their 5 s receive timeout; don't unload under them
	for (int i = 0; i < 60 && active > 0; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	ok = false;
	started = false;
	WSACleanup();
}

bool is_stopping() { return stopping.load(); }
int port() { return the_port; }
const std::string &key() { return the_key; }
std::string base_url() { return "http://127.0.0.1:" + std::to_string(the_port); }

} // namespace chat_server

} // namespace obspet
