// Updating OBSPETS from inside OBS: the Update card on the settings page.
//
// It asks sindruid.com for the newest version (soon after OBS starts, then every few hours). When
// the streamer clicks Update, it downloads the installer (with this OBS's sign-in, like the Pet
// page's download), checks its SHA-256, and starts it in update mode. That asks Windows for
// permission. Once the installer has it, OBS closes the normal way (scenes saved). The installer waits for
// OBS to be gone, installs, and opens OBS again, unelevated, with the same command line.
//
// An open settings page carries on by itself. Before closing, the plugin leaves a
// handoff (update-handoff.json, the key encrypted for this Windows user) so the next OBS listens on
// the same port with the same key. The pages wait for it, then reload with the new files. If the
// port is taken, the next OBS opens those pages again instead.
//
// It never updates while OBS is streaming, recording or running the replay buffer or virtual camera, or while another OBS is
// open. It only updates the installed copy in %ProgramData%\obs-studio\plugins\obspet: a copy
// installed anywhere else by hand gets a download link instead.
#pragma once

#include "chat-server.hpp"

#include <string>

namespace obspet {

void updater_start();    // module load (UI thread): takes up a handoff, starts checking
void updater_loaded();   // OBS finished loading (UI thread)
// a stream, recording, replay buffer or virtual camera is starting (`starting`), started or stopped (UI thread)
void updater_output_changed(bool starting = false);
void updater_stop();     // OBS is closing, or the module unloads

// GET  /settings/api/self-update[?check=1]   the card's state (or /editor/api/...: an old editor
//                                            page, from when the editor had its own, open while it updates)
// POST /settings/api/self-update?do=install  starts updating; ?do=dismiss forgets an error
HttpResp updater_api(const HttpReq &r, const char *page);

// chat_server::start(): the port and key the OBS before an update listened with, once
bool updater_take_handoff(int &port, std::string &key);
void updater_resumed(bool same_port);

} // namespace obspet
