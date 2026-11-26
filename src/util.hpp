// Small shared helpers: logging, random numbers, JSON through obs_data, paths, opening a browser.
#pragma once

#include <obs-module.h>
#include <plugin-support.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace obspet {

template<typename... A> void info(const char *fmt, A... a) { obs_log(LOG_INFO, fmt, a...); }
template<typename... A> void warn(const char *fmt, A... a) { obs_log(LOG_WARNING, fmt, a...); }

double rnd(double a, double b);
int rnd_int(int a, int b);   // a..b inclusive
inline double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
double now_s();               // a monotonic clock, seconds

// obs_data with automatic release, for JSON in and out
struct DataDel { void operator()(obs_data_t *d) const { obs_data_release(d); } };
using Data = std::unique_ptr<obs_data_t, DataDel>;
inline Data data_new() { return Data(obs_data_create()); }
inline Data data_json(const std::string &s) { return Data(s.empty() ? nullptr : obs_data_create_from_json(s.c_str())); }
std::string str(obs_data_t *d, const char *key);

struct SourceDel { void operator()(obs_source_t *s) const { obs_source_release(s); } };
using SourceRef = std::unique_ptr<obs_source_t, SourceDel>;

// "#rrggbb" -> 0xAABBGGRR (OBS's color order); `fallback` if it isn't one
uint32_t abgr(const std::string &hex, uint32_t fallback = 0xFFDEE4ECu);
// a viewer's chat color, softened so it reads on a dark outline: at most 40 % saturation, at
// least 74 % lightness (like the stream's own chat)
std::string soften(const std::string &hex);

// The chat bots an OBSPETS Chat hides and a Pet ignores, until changed in their settings
inline const char *DEFAULT_BOTS = "nightbot, streamelements, streamlabs, moobot, fossabot, wizebot, sery_bot, "
				  "soundalerts, kofistreambot, botrixoficial";

std::string trim(const std::string &s);
std::vector<std::string> split(const std::string &s, char sep);
std::string lower(std::string s);

// where the plugin keeps its own files: %APPDATA%/obs-studio/plugin_config/obspet/<name>
std::string config_path(const char *name);
std::string module_data_path(const char *name);   // the plugin's bundled data/<name>
bool file_exists(const std::string &path);
void open_url(const std::string &url);             // in the default browser
void open_folder(const std::string &path);
void reveal_file(const std::string &path);          // its folder, with it selected
std::string computer_name();

// Windows' per-user encryption (DPAPI), as base64: only this Windows user on this PC can read it back.
std::string protect(const std::string &plain);
bool unprotect(const std::string &b64, std::string &plain);   // false: not ours (another user or PC), or damaged

} // namespace obspet
