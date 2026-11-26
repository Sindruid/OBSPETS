// Updating OBSPETS from inside OBS (see updater.hpp).
#include "updater.hpp"
#include "editor.hpp"
#include "hub.hpp"
#include "net.hpp"
#include "settings.hpp"
#include "util.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/platform.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace obspet {

namespace {

const double FIRST_CHECK_S = 8;           // after OBS starts
const double CHECK_EVERY_S = 6 * 3600.0;
const double STALE_S = 15 * 60;           // a page opening asks again if the last answer is older
const double HANDOFF_MAX_AGE_S = 15 * 60; // an update that took longer than this isn't one any more
const double PAGE_RECENT_S = 180;         // a page that asked this recently counts as open
const double PERMISSION_WAIT_S = 10 * 60; // for the Windows prompt to be answered
const char *HANDOFF_FILE = "update-handoff.json";
const char *MAGIC = "OBSPETS-UPDATE-1";   // the first line of update.txt (tools/installer/obspet.iss reads it)

std::mutex mu;
std::condition_variable cv;
std::thread checker, worker;
std::atomic<bool> stopping{false}, started{false};
bool want_check = false;

// what sindruid.com said last
std::string latest, want_sha, page_url = "https://sindruid.com/streamtools/obspets";
int64_t want_bytes = 0;
double checked_at = -1e9;
bool checked = false;   // it answered at least once

// the update in progress: idle, downloading, verifying, permission, closing, error
std::string phase = "idle", error, hint;
double progress = 0;
std::map<std::string, double> seen;   // page ("settings"; "editor": the old editor page) -> when it last asked

// after the restart an update made
struct Handoff {
	bool have = false;
	int port = 0;
	std::string key, from, to;
	std::vector<std::string> pages;
};
Handoff handoff;
std::string updated_from, failed_to;
bool reopen = false;

bool manual = false;   // not the installed copy in %ProgramData%: no one-click update
std::atomic<bool> ready{false};
std::atomic<bool> streaming{false}, recording{false}, replaying{false}, vcam{false}, starting{false};
std::atomic<void *> main_window{nullptr};

std::wstring widen(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1)
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
	return w;
}

std::string narrow(const std::wstring &w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string s(n > 0 ? n - 1 : 0, '\0');
	if (n > 1)
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
	return s;
}

// "0.5.10" > "0.5.9"; anything after a "-" or "+" is ignored
int vcmp(const std::string &a, const std::string &b)
{
	auto parts = [](const std::string &v) {
		std::vector<long> out;
		std::string core = v.substr(0, v.find_first_of("-+"));
		for (auto &p : split(core, '.'))
			out.push_back(std::strtol(p.c_str(), nullptr, 10));
		return out;
	};
	std::vector<long> x = parts(a), y = parts(b);
	for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
		long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
		if (p != q)
			return p < q ? -1 : 1;
	}
	return 0;
}

bool good_version(const std::string &v)
{
	return !v.empty() && v.size() < 24 && std::all_of(v.begin(), v.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
}

bool sleep_unless_stopping(int ms)
{
	std::unique_lock<std::mutex> lk(mu);
	return !cv.wait_for(lk, std::chrono::milliseconds(ms), [] { return stopping.load(); });
}

// ------------------------------------------------------------------ where things are

std::wstring module_dll()
{
	HMODULE h = nullptr;
	wchar_t buf[MAX_PATH * 2];
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)&module_dll, &h))
		return L"";
	DWORD n = GetModuleFileNameW(h, buf, (DWORD)(sizeof buf / sizeof buf[0]));
	return n ? std::wstring(buf, n) : L"";
}

std::wstring full_lower(const std::wstring &p)
{
	wchar_t buf[MAX_PATH * 2];
	DWORD n = GetFullPathNameW(p.c_str(), (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
	std::wstring s = n ? std::wstring(buf, n) : p;
	std::replace(s.begin(), s.end(), L'/', L'\\');
	CharLowerBuffW(&s[0], (DWORD)s.size());
	return s;
}

// The installer puts the plugin in %ProgramData%\obs-studio\plugins\obspet. A copy anywhere else
// (unzipped by hand, a portable OBS) would end up loaded twice, so that one gets a link instead.
bool installed_by_setup()
{
	PWSTR pd = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd)))
		return false;
	std::wstring want = full_lower(std::wstring(pd) + L"\\obs-studio\\plugins\\obspet\\obspet.dll");
	CoTaskMemFree(pd);
	std::wstring have = module_dll();
	return !have.empty() && full_lower(have) == want;
}

std::wstring obs_exe()
{
	wchar_t buf[MAX_PATH * 2];
	DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
	return n ? std::wstring(buf, n) : L"";
}

// Every other process running the same program as this OBS
bool another_obs_open()
{
	std::wstring me = obs_exe();
	size_t sl = me.find_last_of(L"\\/");
	std::wstring name = sl == std::wstring::npos ? me : me.substr(sl + 1);
	if (name.empty())
		return false;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return false;
	PROCESSENTRY32W pe{};
	pe.dwSize = sizeof pe;
	bool found = false;
	DWORD self = GetCurrentProcessId();
	for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
		found = pe.th32ProcessID != self && _wcsicmp(pe.szExeFile, name.c_str()) == 0;
	CloseHandle(snap);
	return found;
}

// OBS's own command line, to open it again the same way (portable mode, profile, scene collection,
// ...), without the switches that would start a stream or a recording by themselves.
std::wstring relaunch_args()
{
	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv)
		return L"";
	static const wchar_t *DROP[] = {L"--startstreaming", L"--startrecording", L"--startreplaybuffer", L"--startvirtualcam"};
	std::wstring out;
	for (int i = 1; i < argc; i++) {
		std::wstring a = argv[i];
		bool drop = false;
		for (auto *d : DROP)
			drop = drop || _wcsicmp(a.c_str(), d) == 0;
		if (drop)
			continue;
		if (!out.empty())
			out += L' ';
		if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) {
			out += a;
			continue;
		}
		// quoted the way CommandLineToArgvW reads it back
		out += L'"';
		size_t slashes = 0;
		for (wchar_t c : a) {
			if (c == L'\\') {
				slashes++;
				continue;
			}
			if (c == L'"')
				out.append(slashes * 2 + 1, L'\\');
			else
				out.append(slashes, L'\\');
			slashes = 0;
			out += c;
		}
		out.append(slashes * 2, L'\\');
		out += L'"';
	}
	LocalFree(argv);
	return out;
}

std::wstring work_dir()
{
	wchar_t buf[MAX_PATH + 2];
	DWORD n = GetTempPathW((DWORD)(sizeof buf / sizeof buf[0]), buf);
	std::wstring d = (n ? std::wstring(buf, n) : L"C:\\Windows\\Temp\\") + L"obspet-update\\";
	CreateDirectoryW(d.c_str(), nullptr);
	return d;
}

void clear_dir(const std::wstring &dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE f = FindFirstFileW((dir + L"*").c_str(), &fd);
	if (f == INVALID_HANDLE_VALUE)
		return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			DeleteFileW((dir + fd.cFileName).c_str());
	} while (FindNextFileW(f, &fd));
	FindClose(f);
}

std::string sha256_file(const std::wstring &path, int64_t &size)
{
	size = 0;
	HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return "";
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE h = nullptr;
	std::string out;
	if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
	    BCRYPT_SUCCESS(BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0))) {
		std::vector<unsigned char> buf(1 << 16);
		DWORD n = 0;
		bool ok = true;
		while (ok && ReadFile(f, buf.data(), (DWORD)buf.size(), &n, nullptr) && n > 0) {
			ok = BCRYPT_SUCCESS(BCryptHashData(h, buf.data(), n, 0));
			size += n;
		}
		unsigned char dig[32];
		if (ok && BCRYPT_SUCCESS(BCryptFinishHash(h, dig, sizeof dig, 0))) {
			char hex[65];
			for (int i = 0; i < 32; i++)
				snprintf(hex + i * 2, 3, "%02x", dig[i]);
			out = hex;
		}
	}
	if (h)
		BCryptDestroyHash(h);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	CloseHandle(f);
	return out;
}

// ------------------------------------------------------------------ asking sindruid.com

void check_now()
{
	HttpResult r = http("GET", Hub::backend() + "/streamtools/api/pet/update", {}, "", 15);
	Data d = data_json(r.body);
	std::lock_guard<std::mutex> g(mu);
	checked_at = now_s();
	if (r.ok() && d) {
		std::string v = str(d.get(), "version"), s = lower(str(d.get(), "sha256")), pg = str(d.get(), "page");
		latest = good_version(v) ? v : "";
		want_sha = s.size() == 64 && s.find_first_not_of("0123456789abcdef") == std::string::npos ? s : "";
		want_bytes = obs_data_get_int(d.get(), "bytes");
		if (pg.rfind("https://", 0) == 0)
			page_url = pg;
		checked = true;
		if (!latest.empty() && vcmp(latest, PLUGIN_VERSION) > 0)
			info("update: %s is out (this is %s)", latest.c_str(), PLUGIN_VERSION);
	} else if (r.status == 404) {
		latest.clear();   // (none published)
		checked = true;
	}
}

void checker_loop()
{
	double next = now_s() + FIRST_CHECK_S;
	while (!stopping) {
		{
			std::unique_lock<std::mutex> lk(mu);
			cv.wait_for(lk, std::chrono::milliseconds(std::max<long long>(0, (long long)((next - now_s()) * 1000))),
				    [&] { return stopping.load() || want_check || now_s() >= next; });
			if (stopping)
				return;
			want_check = false;
		}
		check_now();
		next = now_s() + CHECK_EVERY_S;
	}
}

// ------------------------------------------------------------------ the card's state

std::string blocked_reason()
{
	if (streaming || starting)
		return "You're live. Stop the stream first: updating closes OBS for a moment.";
	if (recording)
		return "OBS is recording. Stop the recording first: updating closes OBS for a moment.";
	if (replaying)
		return "The replay buffer is running. Stop it first: updating closes OBS for a moment.";
	if (vcam)
		return "The virtual camera is on. Stop it first: updating closes OBS for a moment.";
	if (another_obs_open())
		return "Another OBS is open. Close it first: OBSPETS can only update while OBS is closed.";
	return "";
}

bool busy_phase(const std::string &p)
{
	return p == "downloading" || p == "verifying" || p == "permission" || p == "closing";
}

std::string status_json()
{
	std::string blocked = blocked_reason();
	bool signed_in = !Hub::get().api_token().empty();
	Data d = data_new();
	std::lock_guard<std::mutex> g(mu);
	obs_data_set_string(d.get(), "current", PLUGIN_VERSION);
	obs_data_set_string(d.get(), "latest", latest.c_str());
	obs_data_set_bool(d.get(), "available", !latest.empty() && vcmp(latest, PLUGIN_VERSION) > 0);
	obs_data_set_bool(d.get(), "checked", checked);
	obs_data_set_int(d.get(), "bytes", want_bytes);
	obs_data_set_string(d.get(), "phase", phase.c_str());
	obs_data_set_double(d.get(), "progress", progress);
	obs_data_set_string(d.get(), "error", error.c_str());
	obs_data_set_string(d.get(), "hint", hint.c_str());
	obs_data_set_string(d.get(), "blocked", blocked.c_str());
	obs_data_set_bool(d.get(), "manual", manual);
	obs_data_set_bool(d.get(), "signed_in", signed_in);
	obs_data_set_string(d.get(), "page_url", page_url.c_str());
	obs_data_set_string(d.get(), "updated_from", updated_from.c_str());
	obs_data_set_string(d.get(), "failed_to", failed_to.c_str());
	obs_data_set_bool(d.get(), "ready", ready.load());
	return obs_data_get_json(d.get());
}

HttpResp json(const std::string &body, int status = 200)
{
	HttpResp r;
	r.status = status;
	r.body = body;
	return r;
}

// ------------------------------------------------------------------ updating

void set_phase(const std::string &p, double prog = 0, const std::string &h = "")
{
	std::lock_guard<std::mutex> g(mu);
	phase = p;
	progress = prog;
	hint = h;
}

void fail(const std::string &msg)
{
	warn("update: %s", msg.c_str());
	std::lock_guard<std::mutex> g(mu);
	phase = "error";
	error = msg;
	hint.clear();
	progress = 0;
}

void write_handoff(const std::string &to)
{
	Data d = data_new();
	obs_data_set_int(d.get(), "port", chat_server::port());
	obs_data_set_string(d.get(), "key", protect(chat_server::key()).c_str());
	obs_data_set_string(d.get(), "from", PLUGIN_VERSION);
	obs_data_set_string(d.get(), "to", to.c_str());
	obs_data_set_int(d.get(), "at", (long long)time(nullptr));
	obs_data_array_t *pages = obs_data_array_create();
	{
		std::lock_guard<std::mutex> g(mu);
		for (auto &kv : seen)
			if (now_s() - kv.second < PAGE_RECENT_S) {
				Data it = data_new();
				obs_data_set_string(it.get(), "page", kv.first.c_str());
				obs_data_array_push_back(pages, it.get());
			}
	}
	obs_data_set_array(d.get(), "pages", pages);
	obs_data_array_release(pages);
	std::string path = config_path(HANDOFF_FILE);
	std::string dir = path.substr(0, path.find_last_of("/\\"));
	os_mkdirs(dir.c_str());
	obs_data_save_json_safe(d.get(), path.c_str(), "tmp", "bak");
}

void drop_handoff()
{
	std::string path = config_path(HANDOFF_FILE);
	os_unlink(path.c_str());
	os_unlink((path + ".bak").c_str());
}

// Brings OBS to the front, so the Windows permission prompt the installer brings up isn't left
// blinking in the taskbar behind the browser (UI thread).
void bring_forward(void *)
{
	HWND w = (HWND)main_window.load();
	if (!w)
		return;
	if (IsIconic(w))
		ShowWindow(w, SW_RESTORE);
	HWND fg = GetForegroundWindow();
	DWORD me = GetCurrentThreadId(), them = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
	bool attached = them && them != me && AttachThreadInput(me, them, TRUE);
	BringWindowToTop(w);
	SetForegroundWindow(w);
	if (attached)
		AttachThreadInput(me, them, FALSE);
}

void install()
{
	check_now();   // what's newest right now (and its SHA-256)
	std::string ver, sha;
	int64_t bytes;
	{
		std::lock_guard<std::mutex> g(mu);
		ver = latest;
		sha = want_sha;
		bytes = want_bytes;
	}
	if (ver.empty() || vcmp(ver, PLUGIN_VERSION) <= 0)
		return fail("There's no newer version of OBSPETS after all.");
	if (sha.empty())
		return fail("sindruid.com didn't say how to check the download, so nothing was installed. Try again later.");
	std::string token = Hub::get().api_token();
	if (token.empty())
		return fail("Sign in first (Account), then update.");

	std::wstring dir = work_dir();
	clear_dir(dir);
	std::wstring exe = dir + L"OBSPETS-" + widen(ver) + L"-setup.exe";
	info("update: downloading %s", ver.c_str());
	HttpResult r = http_download(
		Hub::backend() + "/streamtools/api/pet/update/file", {"Authorization: Bearer " + token}, narrow(exe),
		[bytes](int64_t got, int64_t total) {
			if (stopping)
				return false;
			int64_t all = total > 0 ? total : bytes;
			std::lock_guard<std::mutex> g(mu);
			progress = all > 0 ? std::min(1.0, (double)got / (double)all) : 0;
			return true;
		},
		600);
	if (stopping)
		return;
	if (!r.ok()) {
		if (r.status == 401)
			return fail("This OBS isn't signed in any more. Sign in again (Account), then update.");
		if (r.status == 403)
			return fail("This account can't download OBSPETS. Get it on sindruid.com/streamtools/obspets.");
		return fail("Couldn't download the update (" + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error) +
			    "). Check the internet connection and try again.");
	}

	set_phase("verifying", 1);
	int64_t size = 0;
	std::string got = sha256_file(exe, size);
	if (got != sha || (bytes > 0 && size != bytes)) {
		DeleteFileW(exe.c_str());
		return fail("The download didn't match what sindruid.com published, so it wasn't installed. Try again.");
	}
	std::string why = blocked_reason();
	if (!why.empty())
		return fail(why);

	// What the installer needs to know (tools/installer/obspet.iss, UpdateMode): which OBS to wait
	// for, how to open it again, and the file that says it got permission.
	std::wstring ready_file = dir + L"ready", cancel_file = dir + L"cancel", task = dir + L"update.txt";
	std::wstring obs = obs_exe();
	std::wstring obs_dir = obs.substr(0, obs.find_last_of(L"\\/"));
	std::string text = std::string(MAGIC) + "\r\n" + std::to_string(GetCurrentProcessId()) + "\r\n" + narrow(obs) + "\r\n" +
			   narrow(obs_dir) + "\r\n" + narrow(relaunch_args()) + "\r\n" + narrow(ready_file) + "\r\n" +
			   narrow(cancel_file) + "\r\n" + ver + "\r\n";
	if (!os_quick_write_utf8_file(narrow(task).c_str(), text.c_str(), text.size(), true))
		return fail("Couldn't prepare the update (no room in the temp folder?).");

	set_phase("permission");
	obs_queue_task(OBS_TASK_UI, bring_forward, nullptr, false);
	sleep_unless_stopping(250);
	std::wstring params = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- \"/LOG=" + dir + L"setup.log\" \"/OBSPETUPDATE=" + task + L"\"";
	SHELLEXECUTEINFOW sei{};
	sei.cbSize = sizeof sei;
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	sei.hwnd = (HWND)main_window.load();
	sei.lpVerb = L"open";
	sei.lpFile = exe.c_str();
	sei.lpParameters = params.c_str();
	sei.lpDirectory = dir.c_str();
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei) || !sei.hProcess) {
		DWORD e = GetLastError();
		if (e == ERROR_CANCELLED)
			return fail("Windows didn't get permission, so nothing changed.");
		return fail("Couldn't start the installer (Windows error " + std::to_string(e) + "). Nothing changed.");
	}
	AllowSetForegroundWindow(GetProcessId(sei.hProcess));
	info("update: installer started, waiting for permission");

	// The installer, elevated, writes `ready` once Windows said yes. Until then nothing has changed.
	bool have_permission = false;
	for (double t0 = now_s(); !stopping && now_s() - t0 < PERMISSION_WAIT_S;) {
		if (GetFileAttributesW(ready_file.c_str()) != INVALID_FILE_ATTRIBUTES) {
			have_permission = true;
			break;
		}
		if (WaitForSingleObject(sei.hProcess, 150) == WAIT_OBJECT_0) {
			DWORD code = 0;
			GetExitCodeProcess(sei.hProcess, &code);
			CloseHandle(sei.hProcess);
			info("update: the installer ended before it had permission (exit code %lu)", code);
			return fail("Windows didn't get permission, so nothing changed. Click Update to try again.");
		}
	}
	if (stopping) {   // OBS is closing anyway: the installer carries on once it is gone
		CloseHandle(sei.hProcess);
		return;
	}
	if (!have_permission) {
		os_quick_write_utf8_file(narrow(cancel_file).c_str(), "x", 1, false);
		CloseHandle(sei.hProcess);
		return fail("Nobody answered the Windows prompt, so the update was called off. Nothing changed.");
	}
	why = blocked_reason();   // (a stream may have started meanwhile)
	if (!why.empty()) {
		os_quick_write_utf8_file(narrow(cancel_file).c_str(), "x", 1, false);
		CloseHandle(sei.hProcess);
		return fail(why + " The update was called off; nothing changed.");
	}

	// Close OBS the normal way (it saves the scenes), with the next OBS told where the pages are.
	write_handoff(ver);
	set_phase("closing");
	info("update: closing OBS for the installer");
	sleep_unless_stopping(1500);   // (the pages see "closing" first)
	double t0 = now_s(), posted = -1e9;
	while (!stopping) {
		HWND w = (HWND)main_window.load();
		// again every few seconds: OBS ignores it while one of its own dialogs (Settings) is open
		if (now_s() - posted > 4) {
			if (w)
				PostMessageW(w, WM_CLOSE, 0, 0);
			posted = now_s();
		}
		if (now_s() - t0 > 6) {
			std::lock_guard<std::mutex> g(mu);
			hint = w ? "If OBS is showing a window (like Settings), close it so OBS can close."
				 : "Close OBS yourself (File, then Exit): the update installs once it's closed.";
		}
		if (WaitForSingleObject(sei.hProcess, 200) == WAIT_OBJECT_0) {
			// the installer gave up waiting for OBS (2 minutes) and installed nothing
			CloseHandle(sei.hProcess);
			drop_handoff();
			return fail("OBS didn't close, so the update was called off. Nothing changed.");
		}
	}
	CloseHandle(sei.hProcess);
}

void run_install()
{
	HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	install();
	if (SUCCEEDED(co))
		CoUninitialize();
}

} // namespace

// ------------------------------------------------------------------ the plugin's side

void updater_start()
{
	if (started.exchange(true))
		return;
	stopping = false;
	manual = !installed_by_setup();

	// After an update: the port, key and pages of the OBS before, if it closed for one just now
	std::string path = config_path(HANDOFF_FILE);
	if (file_exists(path)) {
		Data d(obs_data_create_from_json_file_safe(path.c_str(), "bak"));
		drop_handoff();
		long long at = d ? obs_data_get_int(d.get(), "at") : 0;
		std::string key;
		if (d && at > 0 && (double)(time(nullptr) - at) < HANDOFF_MAX_AGE_S && unprotect(str(d.get(), "key"), key)) {
			std::lock_guard<std::mutex> g(mu);
			handoff.have = true;
			handoff.port = (int)obs_data_get_int(d.get(), "port");
			handoff.key = key;
			handoff.from = str(d.get(), "from");
			handoff.to = str(d.get(), "to");
			obs_data_array_t *pages = obs_data_get_array(d.get(), "pages");
			for (size_t i = 0; pages && i < obs_data_array_count(pages); i++) {
				Data it(obs_data_array_item(pages, i));
				std::string pg = str(it.get(), "page");
				if (pg == "settings" || pg == "editor")
					handoff.pages.push_back(pg);
			}
			obs_data_array_release(pages);
			if (handoff.to == PLUGIN_VERSION)
				updated_from = handoff.from;
			else
				failed_to = handoff.to;
			info("update: started after updating from %s to %s%s", handoff.from.c_str(), handoff.to.c_str(),
			     handoff.to == PLUGIN_VERSION ? "" : " (it didn't install)");
		}
	}
	bool resume;
	{
		std::lock_guard<std::mutex> g(mu);
		resume = handoff.have;
	}
	if (resume)
		chat_server::start();   // now, on the port the open pages know
	checker = std::thread(checker_loop);
}

void updater_loaded()
{
	main_window = obs_frontend_get_main_window_handle();
	updater_output_changed();
	ready = true;
	std::vector<std::string> pages;
	{
		std::lock_guard<std::mutex> g(mu);
		if (reopen)
			pages = handoff.pages;
		reopen = false;
	}
	// the pages couldn't find this OBS by themselves (the port was taken): open them again
	for (auto &p : pages) {
		if (p == "settings")
			open_settings();
		else if (p == "editor")   // (the old editor page: its Characters view now)
			open_editor("");
	}
}

void updater_output_changed(bool is_starting)
{
	starting = is_starting;   // (a stream starting isn't "active" yet)
	streaming = obs_frontend_streaming_active();
	recording = obs_frontend_recording_active();
	replaying = obs_frontend_replay_buffer_active();
	vcam = obs_frontend_virtualcam_active();
}

void updater_stop()
{
	if (!started)
		return;
	{
		std::lock_guard<std::mutex> g(mu);
		stopping = true;
	}
	cv.notify_all();
	if (checker.joinable())
		checker.join();
	if (worker.joinable())
		worker.join();
	started = false;
}

bool updater_take_handoff(int &port, std::string &key)
{
	std::lock_guard<std::mutex> g(mu);
	if (!handoff.have || handoff.key.empty())
		return false;
	port = handoff.port;
	key = handoff.key;
	handoff.key.clear();   // once
	return true;
}

void updater_resumed(bool same_port)
{
	std::lock_guard<std::mutex> g(mu);
	reopen = !same_port && !handoff.pages.empty();
	info("update: the settings page %s", same_port ? "keep their address" : "open again (the port was taken)");
}

HttpResp updater_api(const HttpReq &r, const char *page)
{
	{
		std::lock_guard<std::mutex> g(mu);
		seen[page] = now_s();
	}
	if (r.method == "GET") {
		if (r.q("check") == "1") {
			std::lock_guard<std::mutex> g(mu);
			if (now_s() - checked_at > STALE_S && !busy_phase(phase)) {
				want_check = true;
				cv.notify_all();
			}
		}
		return json(status_json());
	}
	if (r.method != "POST")
		return json("{\"error\":\"method\"}", 405);
	std::string what = r.q("do");
	if (what == "dismiss") {
		std::lock_guard<std::mutex> g(mu);
		if (phase == "error") {
			phase = "idle";
			error.clear();
		}
		updated_from.clear();
		failed_to.clear();
		return json("{\"ok\":true}");
	}
	if (what != "install")
		return json("{\"error\":\"unknown\"}", 404);

	auto refuse = [](const std::string &msg) {
		Data d = data_new();
		obs_data_set_string(d.get(), "error", msg.c_str());
		return json(obs_data_get_json(d.get()), 409);
	};
	std::string why = blocked_reason();
	if (!why.empty())
		return refuse(why);
	if (Hub::get().api_token().empty())
		return refuse("Sign in first (Account), then update.");
	std::thread old;
	{
		std::lock_guard<std::mutex> g(mu);
		if (stopping)
			return refuse("OBS is closing.");
		if (busy_phase(phase))
			return json("{\"ok\":true}");   // (already updating: clicked twice, or from the other page)
		if (manual)
			return refuse("This copy of OBSPETS wasn't installed with its installer, so it can't update itself. Download the new version instead.");
		if (latest.empty() || vcmp(latest, PLUGIN_VERSION) <= 0)
			return refuse("There's no newer version of OBSPETS.");
		phase = "downloading";
		progress = 0;
		error.clear();
		hint.clear();
		old = std::move(worker);
	}
	if (old.joinable())
		old.join();   // (the last try, finished: it set its phase last)
	std::lock_guard<std::mutex> g(mu);
	if (stopping) {   // (OBS began closing meanwhile, and updater_stop() won't wait for a new thread)
		phase = "idle";
		return refuse("OBS is closing.");
	}
	worker = std::thread(run_install);
	return json("{\"ok\":true}");
}

} // namespace obspet
