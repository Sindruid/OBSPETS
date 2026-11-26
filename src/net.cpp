#include "net.hpp"
#include "util.hpp"

#include <plugin-support.h>

#include <curl/curl.h>
#include <curl/websockets.h>

#include <winsock2.h>

namespace obspet {

// who's asking, for the servers' logs (PLUGIN_VERSION is only known at run time)
static const char *user_agent()
{
	static const std::string ua = std::string("OBSPETS/") + PLUGIN_VERSION + " (+https://sindruid.com/streamtools)";
	return ua.c_str();
}

static std::atomic<bool> g_abort{false};

void net_init()
{
	g_abort = false;
	curl_global_init(CURL_GLOBAL_DEFAULT);
}

void net_abort()
{
	g_abort = true;
}

// curl calls this often while it works (also while it connects): nonzero ends the transfer
static int on_progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return g_abort ? 1 : 0;
}

static void abortable(CURL *c)
{
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
}

void net_shutdown()
{
	curl_global_cleanup();
}

static const size_t MAX_ANSWER = 16u << 20;    // no answer we ask for is anywhere near this
static const size_t MAX_MESSAGE = 4u << 20;    // nor any WebSocket message

static size_t collect(char *p, size_t size, size_t n, void *ud)
{
	auto *out = static_cast<std::string *>(ud);
	if (out->size() + size * n > MAX_ANSWER)
		return 0;   // (curl stops: CURLE_WRITE_ERROR)
	out->append(p, size * n);
	return size * n;
}

HttpResult http(const std::string &method, const std::string &url, const std::vector<std::string> &headers,
		const std::string &body, long timeout_s)
{
	HttpResult r;
	if (g_abort) {
		r.error = "closing";
		return r;
	}
	CURL *c = curl_easy_init();
	if (!c) {
		r.error = "curl_easy_init failed";
		return r;
	}
	curl_slist *hs = nullptr;
	for (auto &h : headers)
		hs = curl_slist_append(hs, h.c_str());
	curl_easy_setopt(c, CURLOPT_URL, url.c_str());
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
	curl_easy_setopt(c, CURLOPT_USERAGENT, user_agent());
	curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout_s);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
	abortable(c);
	if (method == "POST") {
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
	} else if (method != "GET") {
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
		if (!body.empty()) {
			curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
			curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
		}
	}
	CURLcode rc = curl_easy_perform(c);
	if (rc == CURLE_OK)
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
	else
		r.error = curl_easy_strerror(rc);
	curl_slist_free_all(hs);
	curl_easy_cleanup(c);
	return r;
}

WebSocket::~WebSocket()
{
	close();
}

bool WebSocket::connect(const std::string &url, std::string &err)
{
	close();
	if (g_abort) {
		err = "closing";
		return false;
	}
	curl_ = curl_easy_init();
	if (!curl_) {
		err = "curl_easy_init failed";
		return false;
	}
	curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl_, CURLOPT_PROTOCOLS_STR, "ws,wss");
	curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);   // 2 = WebSocket
	curl_easy_setopt(curl_, CURLOPT_USERAGENT, user_agent());
	curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
	abortable(curl_);
	CURLcode rc = curl_easy_perform(curl_);
	if (rc != CURLE_OK) {
		err = curl_easy_strerror(rc);
		close();
		return false;
	}
	partial_.clear();
	return true;
}

void WebSocket::close()
{
	if (curl_) {
		size_t sent = 0;
		curl_ws_send(curl_, "", 0, &sent, 0, CURLWS_CLOSE);
		curl_easy_cleanup(curl_);
		curl_ = nullptr;
	}
}

bool WebSocket::send(const std::string &text)
{
	if (!curl_)
		return false;
	size_t sent = 0;
	return curl_ws_send(curl_, text.data(), text.size(), &sent, 0, CURLWS_TEXT) == CURLE_OK;
}

int WebSocket::recv(std::string &out, int wait_ms)
{
	if (!curl_)
		return -1;
	char buf[16384];
	double until = now_s() + wait_ms / 1000.0;
	for (;;) {
		size_t got = 0;
		const curl_ws_frame *meta = nullptr;
		CURLcode rc = curl_ws_recv(curl_, buf, sizeof buf, &got, &meta);
		if (rc == CURLE_AGAIN) {
			double left = until - now_s();
			if (left <= 0)
				return 0;
			curl_socket_t sock = CURL_SOCKET_BAD;
			curl_easy_getinfo(curl_, CURLINFO_ACTIVESOCKET, &sock);
			if (sock == CURL_SOCKET_BAD)
				return -1;
			fd_set rd;
			FD_ZERO(&rd);
			FD_SET(sock, &rd);
			timeval tv;
			int ms = (int)(left * 1000);
			if (ms > 250)
				ms = 250;   // wake up now and then so the caller can stop
			tv.tv_sec = ms / 1000;
			tv.tv_usec = (ms % 1000) * 1000;
			select((int)sock + 1, &rd, nullptr, nullptr, &tv);
			continue;
		}
		if (rc != CURLE_OK)
			return -1;
		if (!meta)
			continue;
		if (meta->flags & CURLWS_CLOSE)
			return -1;
		if (meta->flags & CURLWS_PING) {
			size_t sent = 0;
			curl_ws_send(curl_, buf, got, &sent, 0, CURLWS_PONG);
			continue;
		}
		if (meta->flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT)) {
			if (partial_.size() + got > MAX_MESSAGE)
				return -1;   // (the caller reconnects)
			partial_.append(buf, got);
			if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
				out.swap(partial_);
				partial_.clear();
				return 1;
			}
		}
		if (now_s() > until)
			return 0;
	}
}

} // namespace obspet
