// A tiny HTTP server on 127.0.0.1 for OBSPETS Chat pages. A browser source can't call into a plugin
// any other way: the plugin talks to a page with obs-browser's `javascript_event`, and the page
// answers here (where its messages are, the message the pet is about to yank).
//
// It serves the page itself (data/chat/) and hands every request with a path under /chat/api/ to
// chat_api() (chat-source.cpp). Those need the per-run key the page got in its URL, so another web
// page open on this PC can't talk to it. One thread accepts; each connection gets a short-lived
// thread of its own (Chromium opens spare connections it may never use).
#pragma once

#include <map>
#include <string>

namespace obspet {

struct HttpReq {
	std::string method, path, body;
	std::string host;   // the Host header, lower case
	std::map<std::string, std::string> query;
	std::string q(const char *key) const
	{
		auto it = query.find(key);
		return it == query.end() ? std::string() : it->second;
	}
};

struct HttpResp {
	int status = 200;
	std::string type = "application/json; charset=utf-8";
	std::string body;
	std::string location;   // a redirect (302)
};

// chat-source.cpp: every /chat/api/... request with the right key (any thread)
HttpResp chat_api(const HttpReq &r);

namespace chat_server {
bool start();   // once; later calls return whether it runs
void stop();
int port();
const std::string &key();
std::string base_url();   // http://127.0.0.1:<port>
} // namespace chat_server

} // namespace obspet
