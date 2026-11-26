// The character editor: a page (data/editor/) on the loopback server (chat-server.cpp) where
// streamers make their own characters one animation and one sound at a time. The page does the
// image and audio work itself (slicing, the sprite sheet, trimming, loudness) and writes the
// finished files through editor_api(), only into the user's own packs folder.
#pragma once

#include "chat-server.hpp"

#include <string>

namespace obspet {

// every /editor/api/... request with the right key (any thread)
HttpResp editor_api(const HttpReq &r);

// opens the editor in the default browser, on character `pack` (or the list, if empty)
void open_editor(const std::string &pack);

} // namespace obspet
