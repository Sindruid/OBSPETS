#include "util.hpp"

#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <random>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <dpapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlguid.h>
#include <thread>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace obspet {

static std::mt19937 &rng()
{
	static thread_local std::mt19937 r{std::random_device{}()};
	return r;
}

double rnd(double a, double b)
{
	return a + std::uniform_real_distribution<double>(0.0, 1.0)(rng()) * (b - a);
}

int rnd_int(int a, int b)
{
	return std::uniform_int_distribution<int>(a, b)(rng());
}

double now_s()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string str(obs_data_t *d, const char *key)
{
	const char *s = d ? obs_data_get_string(d, key) : nullptr;
	return s ? s : "";
}

uint32_t abgr(const std::string &hex, uint32_t fallback)
{
	unsigned r, g, b;
	if (hex.size() == 7 && hex[0] == '#' && sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
		return 0xFF000000u | (b << 16) | (g << 8) | r;
	return fallback;
}

std::string soften(const std::string &hex)
{
	unsigned ri, gi, bi;
	if (hex.size() != 7 || hex[0] != '#' || sscanf(hex.c_str() + 1, "%02x%02x%02x", &ri, &gi, &bi) != 3)
		return "";
	double r = ri / 255.0, g = gi / 255.0, b = bi / 255.0;
	double mx = std::max({r, g, b}), mn = std::min({r, g, b});
	double h = 0, s = 0, l = (mx + mn) / 2;
	if (mx != mn) {
		double d = mx - mn;
		s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
		if (mx == r)
			h = (g - b) / d + (g < b ? 6 : 0);
		else if (mx == g)
			h = (b - r) / d + 2;
		else
			h = (r - g) / d + 4;
		h /= 6;
	}
	s = std::min(s, 0.40);
	l = std::max(l, 0.74);
	auto hue = [](double p, double q, double t) {
		if (t < 0)
			t += 1;
		if (t > 1)
			t -= 1;
		if (t < 1.0 / 6)
			return p + (q - p) * 6 * t;
		if (t < 0.5)
			return q;
		if (t < 2.0 / 3)
			return p + (q - p) * (2.0 / 3 - t) * 6;
		return p;
	};
	double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
	char out[8];
	snprintf(out, sizeof out, "#%02x%02x%02x", (int)std::lround(hue(p, q, h + 1.0 / 3) * 255),
		 (int)std::lround(hue(p, q, h) * 255), (int)std::lround(hue(p, q, h - 1.0 / 3) * 255));
	return out;
}

std::string trim(const std::string &s)
{
	size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string &s, char sep)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i <= s.size()) {
		size_t j = s.find(sep, i);
		if (j == std::string::npos)
			j = s.size();
		std::string t = trim(s.substr(i, j - i));
		if (!t.empty())
			out.push_back(t);
		i = j + 1;
	}
	return out;
}

std::string lower(std::string s)
{
	for (auto &c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

std::string config_path(const char *name)
{
	char *p = obs_module_config_path(name);
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

std::string module_data_path(const char *name)
{
	char *p = obs_module_file(name);
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

bool file_exists(const std::string &path)
{
	return !path.empty() && os_file_exists(path.c_str());
}

static std::wstring widen(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1)
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
	return w;
}

// The folder an Explorer window shows, or "" (Raymond Chen's walk: shell window -> its browser ->
// the view -> the folder's pidl).
static std::wstring explorer_folder(IDispatch *d)
{
	std::wstring out;
	IServiceProvider *sp = nullptr;
	if (FAILED(d->QueryInterface(IID_PPV_ARGS(&sp))))
		return out;
	IShellBrowser *sb = nullptr;
	if (SUCCEEDED(sp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&sb)))) {
		IShellView *sv = nullptr;
		if (SUCCEEDED(sb->QueryActiveShellView(&sv))) {
			IFolderView *fv = nullptr;
			if (SUCCEEDED(sv->QueryInterface(IID_PPV_ARGS(&fv)))) {
				IPersistFolder2 *pf = nullptr;
				if (SUCCEEDED(fv->GetFolder(IID_PPV_ARGS(&pf)))) {
					PIDLIST_ABSOLUTE pidl = nullptr;
					if (SUCCEEDED(pf->GetCurFolder(&pidl))) {
						wchar_t buf[MAX_PATH * 4];
						if (SHGetPathFromIDListEx(pidl, buf, (DWORD)std::size(buf), GPFIDL_DEFAULT))
							out = buf;
						CoTaskMemFree(pidl);
					}
					pf->Release();
				}
				fv->Release();
			}
			sv->Release();
		}
		sb->Release();
	}
	sp->Release();
	return out;
}

// Windows won't let OBS (not the app in front: the browser is) put a window it opens in front, so
// Explorer opens behind the editor. Wait for the Explorer window showing `folder`, then bring it up.
static void bring_explorer_forward(const std::string &folder)
{
	std::wstring want = widen(folder);
	std::replace(want.begin(), want.end(), L'/', L'\\');
	while (want.size() > 3 && want.back() == L'\\')
		want.pop_back();
	std::thread([want] {
		if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
			return;
		HWND found = nullptr;
		for (int tries = 0; tries < 50 && !found; tries++) {   // up to 5 s: a new Explorer window takes a moment
			Sleep(100);
			IShellWindows *sw = nullptr;
			if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&sw))))
				continue;
			long n = 0;
			sw->get_Count(&n);
			for (long i = 0; i < n && !found; i++) {
				VARIANT v;
				VariantInit(&v);
				v.vt = VT_I4;
				v.lVal = i;
				IDispatch *d = nullptr;
				if (sw->Item(v, &d) != S_OK || !d)
					continue;
				std::wstring f = explorer_folder(d);
				if (!f.empty() && CompareStringOrdinal(f.c_str(), -1, want.c_str(), -1, TRUE) == CSTR_EQUAL) {
					IWebBrowserApp *wb = nullptr;
					if (SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&wb)))) {
						SHANDLE_PTR h = 0;
						if (SUCCEEDED(wb->get_HWND(&h)))
							found = (HWND)h;
						wb->Release();
					}
				}
				d->Release();
			}
			sw->Release();
		}
		CoUninitialize();
		if (!found)
			return;
		// borrow the input of the window in front, which may hand the foreground on
		HWND fg = GetForegroundWindow();
		DWORD me = GetCurrentThreadId(), them = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
		bool attached = them && them != me && AttachThreadInput(me, them, TRUE);
		if (IsIconic(found))
			ShowWindow(found, SW_RESTORE);
		// above everything for a moment, so it shows even if Windows keeps the focus where it was
		SetWindowPos(found, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetWindowPos(found, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		BringWindowToTop(found);
		SetForegroundWindow(found);
		if (attached)
			AttachThreadInput(me, them, FALSE);
		if (GetForegroundWindow() != found)
			FlashWindow(found, TRUE);   // (still not let in front: at least its taskbar button blinks)
	}).detach();
}

void open_url(const std::string &url)
{
	if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0)
		return;
	for (unsigned char c : url)
		if (c < 0x20 || c == 0x7f)
			return;   // (no address has these)
	ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_folder(const std::string &path)
{
	os_mkdirs(path.c_str());
	ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	bring_explorer_forward(path);
}

void reveal_file(const std::string &path)
{
	std::string p = path;
	std::replace(p.begin(), p.end(), '/', '\\');
	ShellExecuteW(nullptr, L"open", L"explorer.exe", widen("/select,\"" + p + "\"").c_str(), nullptr, SW_SHOWNORMAL);
	size_t i = p.find_last_of('\\');
	if (i != std::string::npos)
		bring_explorer_forward(p.substr(0, i));
}

std::string computer_name()
{
	wchar_t buf[256];
	DWORD n = 256;
	if (GetComputerNameW(buf, &n)) {
		int m = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
		std::string s(m, '\0');
		WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, s.data(), m, nullptr, nullptr);
		return s;
	}
	return "this PC";
}

// ------------------------------------------------------------------ DPAPI

static const char ENTROPY[] = "OBSPETS account";

std::string protect(const std::string &plain)
{
	DATA_BLOB in{(DWORD)plain.size(), (BYTE *)plain.data()}, ent{sizeof(ENTROPY) - 1, (BYTE *)ENTROPY}, out{};
	if (!CryptProtectData(&in, L"OBSPETS", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return "";
	std::string b64;
	DWORD n = 0;
	const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
	if (CryptBinaryToStringA(out.pbData, out.cbData, flags, nullptr, &n) && n) {
		b64.resize(n);
		if (CryptBinaryToStringA(out.pbData, out.cbData, flags, b64.data(), &n))
			b64.resize(n);
		else
			b64.clear();
	}
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return b64;
}

bool unprotect(const std::string &b64, std::string &plain)
{
	DWORD n = 0;
	if (b64.empty() || !CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr))
		return false;
	std::string raw(n, '\0');
	if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, (BYTE *)raw.data(), &n, nullptr, nullptr))
		return false;
	DATA_BLOB in{n, (BYTE *)raw.data()}, ent{sizeof(ENTROPY) - 1, (BYTE *)ENTROPY}, out{};
	if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return false;
	plain.assign((const char *)out.pbData, out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return true;
}

} // namespace obspet
