// HTTP and WebSocket over libcurl. Everything here blocks: call it from a worker thread, never
// from OBS's graphics or UI thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

typedef void CURL;

namespace obspet {

struct HttpResult {
	long status = 0;        // 0 = no answer (offline, timeout)
	std::string body, error;
	bool ok() const { return status >= 200 && status < 300; }
};

HttpResult http(const std::string &method, const std::string &url, const std::vector<std::string> &headers = {},
		const std::string &body = "", long timeout_s = 15);

// GETs `url` into the file `path` (UTF-8), reporting progress (bytes so far, total or 0 if unknown).
// `progress` returning false cancels. A failed or refused download leaves no file behind; the
// result's body holds the server's answer only when it wasn't a success.
HttpResult http_download(const std::string &url, const std::vector<std::string> &headers, const std::string &path,
			 const std::function<bool(int64_t, int64_t)> &progress, long timeout_s = 600);

// A WebSocket client (text messages only; enough for Twitch EventSub).
class WebSocket {
public:
	~WebSocket();
	bool connect(const std::string &url, std::string &err);
	// Waits up to `wait_ms` for a whole message. 1 = got one, 0 = nothing yet, -1 = closed or failed.
	int recv(std::string &out, int wait_ms);
	bool send(const std::string &text);
	void close();
	bool open() const { return curl_ != nullptr; }

private:
	CURL *curl_ = nullptr;
	std::string partial_;
};

void net_init();
void net_abort();   // ends every request and connect in flight, and fails new ones (OBS is closing)
void net_shutdown();

} // namespace obspet
