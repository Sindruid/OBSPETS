// The settings page: a page (data/settings/) on the loopback server (chat-server.cpp) where
// streamers configure their OBSPETS and OBSPETS Chat sources, test reactions and manage their
// sign-in, instead of crowding OBS's properties dialog.
#pragma once

#include "chat-server.hpp"

#include <string>

namespace obspet {

// every /settings/api/... request with the right key (any thread)
HttpResp settings_api(const HttpReq &r);

// opens the settings page in the default browser, optionally focused on one source
// (kind: "pet" or "chat", name: the OBS source's name; empty opens the first source)
void open_settings(const std::string &kind = "", const std::string &name = "");

} // namespace obspet
