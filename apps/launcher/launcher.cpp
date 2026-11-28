// Launcher.exe — Dark-Mode Native Win32 / Direct2D / DirectWrite Launcher & Setup Wizard
// for Dark Emblem (x86-64 PC Runtime).
// Zero .NET / WebView2 / third-party UI dependencies. Uses Windows CNG (bcrypt.dll) for SHA-256
// verification and delegates backup verification to bin\nup_tool.exe and installation to bin\recompiler.exe.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// ---- Companion Portable LLVM Toolchain URL & SHA-256 ----
#ifndef DE_TOOLCHAIN_URL
#define DE_TOOLCHAIN_URL L"https://github.com/chivien99999/Dark-Emblem/releases/latest/download/toolchain-win64.zip"
#endif
#ifndef DE_TOOLCHAIN_SHA256
#define DE_TOOLCHAIN_SHA256 "f1478538afe675f748f51d2c0301f08b93f9cb481f4817a6ba9d6392fadf603e"
#endif

static constexpr UINT WM_APP_STATE_UPDATE = WM_APP + 1;
static constexpr int kWinW = 860;
static constexpr int kWinH = 600;

enum class UiMode { Setup, Installing, Hub };

struct RectF {
    float x, y, w, h;
    bool hit(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
    D2D1_RECT_F d2d() const { return D2D1::RectF(x, y, x + w, y + h); }
    D2D1_ROUNDED_RECT rr(float r) const { return D2D1::RoundedRect(d2d(), r, r); }
};

struct LauncherApp {
    HWND hwnd = NULL;
    HINSTANCE hinst = NULL;
    ID2D1Factory *d2d_factory = nullptr;
    ID2D1HwndRenderTarget *rt = nullptr;
    IDWriteFactory *dw_factory = nullptr;
    IDWriteTextFormat *tf_title = nullptr;
    IDWriteTextFormat *tf_sub = nullptr;
    IDWriteTextFormat *tf_head = nullptr;
    IDWriteTextFormat *tf_body = nullptr;
    IDWriteTextFormat *tf_small = nullptr;
    IDWriteTextFormat *tf_err = nullptr;
    IDWriteTextFormat *tf_btn = nullptr;
    IDWriteTextFormat *tf_btn_big = nullptr;

    std::wstring install_root;
    std::wstring gpu_name = L"Detecting GPU...";
    std::wstring settings_summary = L"Default (Auto Upscaler / Native)";
    bool has_bc7 = false;
    bool is_installed = false;
    bool force_setup = false;

    UiMode mode = UiMode::Setup;
    std::wstring selected_input;
    bool input_is_nup = false;
    bool input_valid = false;
    std::wstring verify_title;
    std::wstring verify_detail;
    bool opt_bc7 = false;

    // Live installation state (protected by mtx)
    std::mutex mtx;
    int progress_pct = 0;
    std::wstring progress_stage = L"Preparing";
    std::wstring progress_detail = L"Initializing setup...";
    std::wstring last_error;
    std::atomic<bool> worker_running{false};
    std::atomic<bool> cancel_req{false};
    HANDLE child_proc = NULL;
    HANDLE child_job = NULL;
    uint64_t install_start_ms = 0;

    // Hover state
    int hover_id = -1;
    float mouse_x = -1, mouse_y = -1;
    float anim_phase = 0.0f;

    // Interactive layout buttons (recomputed each draw)
    struct Btn { int id; RectF rc; };
    std::vector<Btn> btns;
};

static LauncherApp g_app;

// Button IDs
enum {
    BTN_PICK_NUP = 1,
    BTN_PICK_DIR = 2,
    BTN_CHECK_BC7 = 3,
    BTN_START_INSTALL = 4,
    BTN_CANCEL_INSTALL = 5,
    BTN_BACK_HUB = 6,
    BTN_PLAY = 10,
    BTN_SETTINGS = 11,
    BTN_REBUILD_SHADERS = 12,
    BTN_BUILD_BC7 = 13,
    BTN_OPEN_SAVE = 14,
    BTN_REINSTALL = 15,
    BTN_NOTICES = 16,
};

static std::wstring utf8_to_w(const std::string &s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

static std::string w_to_utf8(const std::wstring &s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, NULL, NULL);
    return out;
}

static bool file_exists(const std::wstring &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dir_exists(const std::wstring &p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::wstring quote_arg(std::wstring s) {
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    std::wstring out = L"\"";
    size_t bs = 0;
    for (wchar_t ch : s) {
        if (ch == L'\\') {
            bs++;
        } else if (ch == L'"') {
            out.append(bs * 2 + 1, L'\\');
            out.push_back(L'"');
            bs = 0;
        } else {
            out.append(bs, L'\\');
            bs = 0;
            out.push_back(ch);
        }
    }
    out.append(bs * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static bool toolchain_valid(const std::wstring &dir) {
    return file_exists(dir + L"\\bin\\clang-cl.exe") &&
           file_exists(dir + L"\\bin\\lld-link.exe") &&
           file_exists(dir + L"\\lib\\clang_rt.builtins-x86_64.lib") &&
           file_exists(dir + L"\\include\\stddef.h");
}

static void remove_dir_recursive(const std::wstring &dir) {
    if (!dir_exists(dir)) return;
    WIN32_FIND_DATAW fd{};
    HANDLE hf = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring child = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                remove_dir_recursive(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(dir.c_str());
}

static HANDLE create_kill_job() {
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (!job) return NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        CloseHandle(job);
        return NULL;
    }
    return job;
}

static void terminate_active_children(LauncherApp &app) {
    std::lock_guard<std::mutex> lk(app.mtx);
    if (app.child_job) {
        TerminateJobObject(app.child_job, 1);
        CloseHandle(app.child_job);
        app.child_job = NULL;
    }
    if (app.child_proc) {
        TerminateProcess(app.child_proc, 1);
    }
}

static bool cpu_supports_x86_64_v3() {
    int r[4] = {};
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuid(r, 1);
    const bool osxsave = (r[2] & (1 << 27)) != 0;
    const bool avx     = (r[2] & (1 << 28)) != 0;
    const bool fma     = (r[2] & (1 << 12)) != 0;
    if (!osxsave || !avx || !fma) return false;
    const unsigned long long xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6ull) != 0x6ull) return false;
    __cpuidex(r, 7, 0);
    const bool avx2 = (r[1] & (1 << 5)) != 0;
    const bool bmi2 = (r[1] & (1 << 8)) != 0;
    return avx2 && bmi2;
}

static bool preflight_check(const LauncherApp &app, std::wstring &out_err) {
    if (!cpu_supports_x86_64_v3()) {
        out_err = L"Unsupported CPU: AVX2, FMA, and BMI2 instructions are required (Intel Haswell 4th-gen / AMD Excavator or newer).";
        return false;
    }
    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) {
        out_err = L"No Vulkan 1.3 graphics driver found (vulkan-1.dll missing). Please install or update your GPU drivers.";
        return false;
    }
    FreeLibrary(vk);

    const uint64_t need_gb = app.input_is_nup ? 55ull : 32ull;
    ULARGE_INTEGER free_avail{};
    if (GetDiskFreeSpaceExW(app.install_root.c_str(), &free_avail, NULL, NULL)) {
        if (free_avail.QuadPart < (need_gb << 30)) {
            wchar_t msg[256];
            double avail_gb = (double)free_avail.QuadPart / (1024.0 * 1024.0 * 1024.0);
            swprintf_s(msg, L"Not enough free disk space: %.1f GB available, at least %llu GB required for %s setup.",
                       avail_gb, (unsigned long long)need_gb, app.input_is_nup ? L".nup" : L"folder");
            out_err = msg;
            return false;
        }
    }
    return true;
}

static void detect_gpu(LauncherApp &app) {
    IDXGIFactory1 *fac = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac)) && fac) {
        IDXGIAdapter1 *best = nullptr;
        SIZE_T best_vram = 0;
        for (UINT i = 0; ; i++) {
            IDXGIAdapter1 *ad = nullptr;
            if (fac->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(ad->GetDesc1(&desc))) {
                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                    ad->Release();
                    continue;
                }
                if (!best || desc.DedicatedVideoMemory > best_vram) {
                    if (best) best->Release();
                    best = ad;
                    best_vram = desc.DedicatedVideoMemory;
                    continue;
                }
            }
            ad->Release();
        }
        if (best) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(best->GetDesc1(&desc))) {
                double gb = (double)desc.DedicatedVideoMemory / (1024.0 * 1024.0 * 1024.0);
                wchar_t buf[256];
                swprintf_s(buf, L"%s (%.1f GB VRAM)", desc.Description, gb);
                app.gpu_name = buf;
                // Recommend BC7 cache automatically on <= 12 GB VRAM hardware GPUs (including low-VRAM / iGPUs)
                app.opt_bc7 = (gb <= 12.5);
            }
            best->Release();
        }
        fac->Release();
    }
}

static void refresh_install_state(LauncherApp &app) {
    const bool installed =
        file_exists(app.install_root + L"\\data\\install.seal") &&
        file_exists(app.install_root + L"\\bin\\game.dll") &&
        file_exists(app.install_root + L"\\data\\game.romfs");
    const bool has_bc7 = file_exists(app.install_root + L"\\bc7cache\\bc7cache.idx");

    // Parse active settings summary from save\lantern-settings.ini (outside app.mtx)
    std::wstring summary = L"Upscaler: Auto (DLSS / FSR)  |  Output: Native Display";
    std::wstring ini = app.install_root + L"\\save\\lantern-settings.ini";
    FILE *f = _wfopen(ini.c_str(), L"rb");
    if (f) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = 0;
        std::string txt(buf);
        auto find_val = [&](const char *key) -> std::string {
            size_t pos = txt.find(key);
            if (pos == std::string::npos) return "";
            size_t eq = txt.find('=', pos);
            if (eq == std::string::npos) return "";
            size_t eol = txt.find_first_of("\r\n", eq + 1);
            std::string v = txt.substr(eq + 1, eol == std::string::npos ? std::string::npos : eol - eq - 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            return v;
        };
        std::string up = find_val("upscaler");
        std::string fg = find_val("frame_generation");
        std::string out = find_val("output");
        std::string s = "Upscaler: " + (up.empty() ? "auto" : up);
        if (!fg.empty() && fg != "off") s += "  |  Frame Gen: " + fg;
        if (!out.empty()) s += "  |  Output: " + out;
        summary = utf8_to_w(s);
    }

    std::lock_guard<std::mutex> lk(app.mtx);
    app.is_installed = installed;
    app.has_bc7 = has_bc7;
    app.settings_summary = std::move(summary);
    if (!app.worker_running.load()) {
        app.mode = (installed && !app.force_setup) ? UiMode::Hub : UiMode::Setup;
    }
}

static bool run_nup_tool(const std::wstring &install_root, const std::wstring &subcmd,
                         const std::wstring &target_path, std::string &out_stdout, std::string &out_stderr,
                         DWORD &out_exit) {
    out_stdout.clear();
    out_stderr.clear();
    out_exit = 1;
    std::wstring exe = install_root + L"\\bin\\nup_tool.exe";
    if (!file_exists(exe)) {
        out_stderr = "Missing bin\\nup_tool.exe. Please re-extract the official release archive.";
        return false;
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE hOutR = NULL, hOutW = NULL, hErrR = NULL, hErrW = NULL;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0) || !CreatePipe(&hErrR, &hErrW, &sa, 0)) {
        if (hOutR) CloseHandle(hOutR); if (hOutW) CloseHandle(hOutW);
        if (hErrR) CloseHandle(hErrR); if (hErrW) CloseHandle(hErrW);
        out_stderr = "Failed to create process pipes.";
        return false;
    }
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = quote_arg(exe) + L" " + subcmd + L" " + quote_arg(target_path);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hOutW;
    si.hStdError = hErrW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                        install_root.c_str(), &si, &pi)) {
        CloseHandle(hOutR); CloseHandle(hOutW);
        CloseHandle(hErrR); CloseHandle(hErrW);
        out_stderr = "Failed to launch bin\\nup_tool.exe.";
        return false;
    }
    CloseHandle(hOutW);
    CloseHandle(hErrW);

    auto drain = [](HANDLE h, std::string &dst) {
        char buf[1024];
        DWORD got = 0;
        while (ReadFile(h, buf, sizeof(buf), &got, NULL) && got > 0) {
            if (dst.size() < 64 * 1024) dst.append(buf, got);
        }
    };
    std::thread t_err(drain, hErrR, std::ref(out_stderr));
    drain(hOutR, out_stdout);
    t_err.join();
    CloseHandle(hOutR);
    CloseHandle(hErrR);

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &out_exit);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static std::string parse_kv_token(const std::string &line, const char *key) {
    std::string needle = std::string(key) + "=";
    size_t pos = line.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = line.find_first_of(" \r\n", pos);
    return line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

static std::string parse_kv_rest(const std::string &line, const char *key) {
    std::string needle = std::string(key) + "=";
    size_t pos = line.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = line.find_first_of("\r\n", pos);
    return line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

static std::string trim_err_line(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    if (s.rfind("ERR: ", 0) == 0) s.erase(0, 5);
    return s;
}

static void inspect_selected_backup(LauncherApp &app, const std::wstring &path) {
    app.selected_input = path;
    app.input_valid = false;
    app.verify_title.clear();
    app.verify_detail.clear();
    if (path.empty()) return;

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        app.verify_title = L"Path not found";
        app.verify_detail = L"The selected file or folder does not exist.";
        return;
    }

    std::string out, err;
    DWORD exit_code = 1;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        app.input_is_nup = true;
        if (run_nup_tool(app.install_root, L"inspect-nup", path, out, err, exit_code) &&
            exit_code == 0 && out.rfind("OK ", 0) == 0) {
            unsigned pkg_cnt = (unsigned)strtoul(parse_kv_token(out, "packages").c_str(), nullptr, 10);
            unsigned long long romfs_bytes = strtoull(parse_kv_token(out, "romfs_bytes").c_str(), nullptr, 10);
            app.input_valid = true;
            app.verify_title = L"Verified: Dark Emblem Target Backup (v1.2.0 / 131072)";
            wchar_t det[256];
            double gb = (double)romfs_bytes / (1024.0 * 1024.0 * 1024.0);
            swprintf_s(det, L"Switch 2 .nup container  •  %u package(s)  •  %.1f GB RomFS", pkg_cnt, gb);
            app.verify_detail = det;
        } else {
            app.verify_title = L"Unsupported Backup";
            std::string msg = trim_err_line(err);
            app.verify_detail = utf8_to_w(msg.empty() ? "Backup verification failed." : msg);
        }
    } else {
        app.input_is_nup = false;
        if (run_nup_tool(app.install_root, L"inspect-dir", path, out, err, exit_code) &&
            exit_code == 0 && out.rfind("OK ", 0) == 0) {
            std::string resolved_u8 = parse_kv_rest(out, "resolved");
            app.selected_input = resolved_u8.empty() ? path : utf8_to_w(resolved_u8);
            app.input_valid = true;
            app.verify_title = L"Verified: Dark Emblem Target Backup (v1.2.0 / 131072)";
            app.verify_detail = L"Unpacked Switch 2 exefs/ + romfs/ folder ready for x86-64 recompilation.";
        } else {
            app.verify_title = L"Unsupported Folder";
            std::string msg = trim_err_line(err);
            app.verify_detail = utf8_to_w(msg.empty() ? "Folder verification failed." : msg);
        }
    }
}

// ---- Windows CNG (bcrypt.dll) SHA-256 helper ----
struct WinSha256 {
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    bool init() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return false;
        if (BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0) != 0) {
            BCryptCloseAlgorithmProvider(alg, 0); alg = NULL; return false;
        }
        return true;
    }
    bool update(const uint8_t *data, size_t len) {
        return hash && BCryptHashData(hash, (PUCHAR)data, (ULONG)len, 0) == 0;
    }
    bool final_hex(char out_hex[65]) {
        uint8_t digest[32] = {};
        if (!hash || BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) return false;
        static const char kHex[] = "0123456789abcdef";
        for (int i = 0; i < 32; i++) {
            out_hex[i * 2] = kHex[digest[i] >> 4];
            out_hex[i * 2 + 1] = kHex[digest[i] & 0xF];
        }
        out_hex[64] = '\0';
        return true;
    }
    ~WinSha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
};

// ---- Find or Auto-Download Slim Portable LLVM Toolchain (~62 MB, SHA-256 verified) ----
static bool find_or_download_toolchain(LauncherApp &app, std::wstring &out_tc_dir, std::wstring &out_err) {
    // 1. Check local portable toolchain beside Launcher.exe first (instant offline path)
    std::wstring local_tc = app.install_root + L"\\toolchain";
    if (toolchain_valid(local_tc)) {
        out_tc_dir = local_tc;
        return true;
    }

    // 2. Check cached user toolchain in %LOCALAPPDATA%\DarkEmblem\toolchain
    wchar_t local_app[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH) == 0) {
        out_err = L"Unable to locate LOCALAPPDATA.";
        return false;
    }
    std::wstring cache_root = std::wstring(local_app) + L"\\DarkEmblem";
    std::wstring cache_tc = cache_root + L"\\toolchain";
    if (toolchain_valid(cache_tc)) {
        out_tc_dir = cache_tc;
        return true;
    }

    // 3. Auto-download slim portable LLVM toolchain archive via WinHTTP
    CreateDirectoryW(cache_root.c_str(), NULL);
    std::wstring zip_path = cache_root + L"\\toolchain-win64.zip";

    {
        std::lock_guard<std::mutex> lk(app.mtx);
        app.progress_pct = 1;
        app.progress_stage = L"Downloading Compiler";
        app.progress_detail = L"Fetching portable LLVM x86-64 toolchain (~62 MB)...";
    }
    PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);

    std::wstring url = DE_TOOLCHAIN_URL;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[1024] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;  uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        out_err = L"Invalid portable toolchain download URL.";
        return false;
    }

    HINTERNET hSess = WinHttpOpen(L"DarkEmblem-Launcher/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSess) { out_err = L"WinHTTP initialization failed."; return false; }
    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    HINTERNET hReq = hConn ? WinHttpOpenRequest(hConn, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0) : NULL;
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    if (hReq) WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    bool dl_ok = false;
    if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, NULL)) {
        DWORD status = 0, slen = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &slen, NULL);
        DWORD total_len = 0, tlen = sizeof(total_len);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, NULL, &total_len, &tlen, NULL);
        if (status == 200) {
            HANDLE hf = CreateFileW(zip_path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                WinSha256 sc;
                bool sha_ok = sc.init();
                std::vector<uint8_t> buf(256 * 1024);
                DWORD got = 0;
                uint64_t done = 0;
                dl_ok = sha_ok;
                while (dl_ok && !app.cancel_req.load()) {
                    if (!WinHttpReadData(hReq, buf.data(), (DWORD)buf.size(), &got)) { dl_ok = false; break; }
                    if (got == 0) break;
                    DWORD wr = 0;
                    if (!WriteFile(hf, buf.data(), got, &wr, NULL) || wr != got) { dl_ok = false; break; }
                    if (!sc.update(buf.data(), got)) { dl_ok = false; break; }
                    done += got;
                    wchar_t det[128];
                    if (total_len > 0) {
                        swprintf_s(det, L"Downloading portable LLVM: %.1f / %.1f MB",
                                   done / (1024.0 * 1024.0), total_len / (1024.0 * 1024.0));
                    } else {
                        swprintf_s(det, L"Downloading portable LLVM: %.1f MB", done / (1024.0 * 1024.0));
                    }
                    {
                        std::lock_guard<std::mutex> lk(app.mtx);
                        app.progress_detail = det;
                    }
                    PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);
                }
                CloseHandle(hf);
                if (dl_ok && !app.cancel_req.load()) {
                    const char *exp_sha = DE_TOOLCHAIN_SHA256;
                    if (exp_sha && strlen(exp_sha) == 64) {
                        char hex[65];
                        if (!sc.final_hex(hex) || _stricmp(hex, exp_sha) != 0) {
                            dl_ok = false;
                            out_err = L"Downloaded toolchain SHA-256 verification failed.";
                        }
                    }
                }
            }
        }
    }
    if (hReq) WinHttpCloseHandle(hReq);
    if (hConn) WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);

    if (app.cancel_req.load()) { DeleteFileW(zip_path.c_str()); out_err = L"Cancelled by user."; return false; }
    if (!dl_ok) {
        DeleteFileW(zip_path.c_str());
        if (out_err.empty())
            out_err = L"Could not download portable LLVM toolchain. Place toolchain-win64.zip extracted under .\\toolchain\\ or check your internet connection.";
        return false;
    }

    // Extract zip via Windows System32\tar.exe (never bare tar.exe which can hit Git Bash GNU tar)
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        app.progress_stage = L"Extracting Compiler";
        app.progress_detail = L"Unpacking portable LLVM toolchain...";
    }
    PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);

    wchar_t sysdir[MAX_PATH * 2] = {};
    UINT sdlen = GetSystemDirectoryW(sysdir, (UINT)(sizeof(sysdir) / sizeof(sysdir[0])));
    if (sdlen == 0 || sdlen >= (sizeof(sysdir) / sizeof(sysdir[0]))) {
        DeleteFileW(zip_path.c_str());
        out_err = L"Unable to locate Windows System32 directory.";
        return false;
    }
    std::wstring tar_exe = std::wstring(sysdir) + L"\\tar.exe";
    if (!file_exists(tar_exe)) {
        DeleteFileW(zip_path.c_str());
        out_err = L"Windows System32\\tar.exe not found (requires Windows 10 1803+ or Windows 11).";
        return false;
    }

    std::wstring tmp_tc = cache_tc + L".tmp";
    remove_dir_recursive(tmp_tc);
    if (!CreateDirectoryW(tmp_tc.c_str(), NULL)) {
        DeleteFileW(zip_path.c_str());
        out_err = L"Failed to create temporary toolchain extraction folder.";
        return false;
    }

    std::wstring cmd = quote_arg(tar_exe) + L" -xf " + quote_arg(zip_path) + L" -C " + quote_arg(tmp_tc);
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(tar_exe.c_str(), cmd.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        NULL, NULL, &si, &pi)) {
        remove_dir_recursive(tmp_tc);
        DeleteFileW(zip_path.c_str());
        out_err = L"Failed to launch System32\\tar.exe to unpack portable LLVM archive.";
        return false;
    }
    HANDLE job = create_kill_job();
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
        CloseHandle(job);
        job = NULL;
    }
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        app.child_proc = pi.hProcess;
        app.child_job = job;
    }
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        app.child_proc = NULL;
        if (app.child_job && app.child_job == job) {
            CloseHandle(app.child_job);
            app.child_job = NULL;
        }
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    DeleteFileW(zip_path.c_str());

    if (app.cancel_req.load()) {
        remove_dir_recursive(tmp_tc);
        out_err = L"Cancelled by user.";
        return false;
    }
    if (rc != 0 || !toolchain_valid(tmp_tc)) {
        remove_dir_recursive(tmp_tc);
        out_err = L"Portable LLVM extraction failed or archive is incomplete.";
        return false;
    }

    // Atomically move validated temp directory into final cache_tc
    remove_dir_recursive(cache_tc);
    if (!MoveFileW(tmp_tc.c_str(), cache_tc.c_str()) || !toolchain_valid(cache_tc)) {
        remove_dir_recursive(tmp_tc);
        out_err = L"Failed to finalize portable LLVM toolchain directory.";
        return false;
    }
    out_tc_dir = cache_tc;
    return true;
}

// ---- Background Recompiler Runner (pipes PROGRESS lines into Direct2D UI) ----
static void run_recompiler_async(LauncherApp &app, const std::wstring &mode_arg, bool need_toolchain) {
    if (app.worker_running.exchange(true)) return;
    app.cancel_req.store(false);
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        app.mode = UiMode::Installing;
        app.progress_pct = 0;
        app.progress_stage = L"Starting";
        app.progress_detail = L"Preparing workspace...";
        app.last_error.clear();
        app.install_start_ms = GetTickCount64();
    }
    SetTimer(app.hwnd, 1, 500, NULL);
    PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);

    std::wstring input_copy = app.selected_input;
    bool bc7_copy = app.opt_bc7;

    std::thread([&app, mode_arg, need_toolchain, input_copy, bc7_copy]() {
        std::wstring tc_dir, err;
        if (need_toolchain && !find_or_download_toolchain(app, tc_dir, err)) {
            {
                std::lock_guard<std::mutex> lk(app.mtx);
                app.last_error = err;
                if (app.cancel_req.load()) app.force_setup = false;
            }
            app.worker_running.store(false);
            refresh_install_state(app);
            PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);
            return;
        }

        std::wstring recomp = app.install_root + L"\\bin\\recompiler.exe";
        if (!file_exists(recomp)) {
            {
                std::lock_guard<std::mutex> lk(app.mtx);
                app.last_error = L"Missing bin\\recompiler.exe. Please re-extract the official release archive.";
            }
            app.worker_running.store(false);
            refresh_install_state(app);
            PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);
            return;
        }

        std::wstring cmd = quote_arg(recomp) + L" --mode " + mode_arg +
                           L" --install-root " + quote_arg(app.install_root);
        if (mode_arg == L"install") {
            cmd += L" --input " + quote_arg(input_copy) + L" --toolchain " + quote_arg(tc_dir);
            if (bc7_copy) cmd += L" --bc7";
        }

        SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
        HANDLE hRead = NULL, hWrite = NULL;
        CreatePipe(&hRead, &hWrite, &sa, 0);
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.hStdOutput = hWrite;
        si.hStdError = hWrite;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};

        if (!CreateProcessW(NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL,
                            app.install_root.c_str(), &si, &pi)) {
            CloseHandle(hRead); CloseHandle(hWrite);
            {
                std::lock_guard<std::mutex> lk(app.mtx);
                app.last_error = L"Failed to start bin\\recompiler.exe.";
            }
            app.worker_running.store(false);
            refresh_install_state(app);
            PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);
            return;
        }
        CloseHandle(hWrite);
        HANDLE job = create_kill_job();
        if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
            CloseHandle(job);
            job = NULL;
        }
        {
            std::lock_guard<std::mutex> lk(app.mtx);
            app.child_proc = pi.hProcess;
            app.child_job = job;
        }
        ResumeThread(pi.hThread);

        static constexpr size_t kLogHeadMax = 32 * 1024;
        static constexpr size_t kLogTailMax = 256 * 1024;
        std::string line_buf, err_msg, last_line, log_head, log_tail;
        bool log_truncated = false;
        auto process_line = [&](std::string line) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (line.rfind("PROGRESS ", 0) == 0) {
                const char *p = line.c_str() + 9;
                int pct = atoi(p);
                const char *sp = strchr(p, ' ');
                std::string rest = sp ? (sp + 1) : "";
                size_t bar = rest.find(" | ");
                std::string st = (bar != std::string::npos) ? rest.substr(0, bar) : rest;
                std::string det = (bar != std::string::npos) ? rest.substr(bar + 3) : "";
                {
                    std::lock_guard<std::mutex> lk(app.mtx);
                    app.progress_pct = std::max(0, std::min(100, pct));
                    app.progress_stage = utf8_to_w(st);
                    app.progress_detail = utf8_to_w(det);
                }
                PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);
            } else if (line.rfind("ERROR ", 0) == 0) {
                err_msg = line.substr(6);
            } else if (!line.empty()) {
                last_line = line;
            }
        };

        char chunk[2048];
        DWORD got = 0;
        while (ReadFile(hRead, chunk, sizeof(chunk) - 1, &got, NULL) && got > 0) {
            chunk[got] = 0;
            size_t off = 0;
            if (log_head.size() < kLogHeadMax) {
                size_t take = std::min<size_t>(got, kLogHeadMax - log_head.size());
                log_head.append(chunk, take);
                off = take;
            }
            if (off < (size_t)got) {
                log_tail.append(chunk + off, (size_t)got - off);
                if (log_tail.size() > kLogTailMax) {
                    log_tail.erase(0, log_tail.size() - kLogTailMax);
                    log_truncated = true;
                }
            }
            line_buf.append(chunk, got);
            size_t nl;
            while ((nl = line_buf.find('\n')) != std::string::npos) {
                std::string line = line_buf.substr(0, nl);
                line_buf.erase(0, nl + 1);
                process_line(std::move(line));
            }
        }
        if (!line_buf.empty()) {
            process_line(std::move(line_buf));
            line_buf.clear();
        }
        CloseHandle(hRead);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD rc = 1;
        GetExitCodeProcess(pi.hProcess, &rc);
        if (rc != 0 && !app.cancel_req.load()) {
            std::wstring log_path = app.install_root + L"\\setup.log";
            FILE *lf = _wfopen(log_path.c_str(), L"wb");
            if (lf) {
                char hdr[256];
                snprintf(hdr, sizeof(hdr), "exit_code=%lu (0x%08lX)\r\n", (unsigned long)rc, (unsigned long)rc);
                fwrite(hdr, 1, strlen(hdr), lf);
                if (!log_head.empty()) fwrite(log_head.data(), 1, log_head.size(), lf);
                if (log_truncated) {
                    static const char kMid[] = "\r\n... [middle output truncated to preserve error tail] ...\r\n";
                    fwrite(kMid, 1, sizeof(kMid) - 1, lf);
                }
                if (!log_tail.empty()) fwrite(log_tail.data(), 1, log_tail.size(), lf);
                fclose(lf);
            }
        }
        {
            std::lock_guard<std::mutex> lk(app.mtx);
            app.child_proc = NULL;
            if (app.child_job && app.child_job == job) {
                CloseHandle(app.child_job);
                app.child_job = NULL;
            }
            if (app.cancel_req.load()) {
                app.last_error = L"Operation cancelled.";
                app.force_setup = false;
            } else if (rc != 0) {
                char fallback[160];
                snprintf(fallback, sizeof(fallback), "Setup failed (exit 0x%08lX). Check setup.log and antivirus/free space.", (unsigned long)rc);
                std::string shown = !err_msg.empty() ? err_msg : (!last_line.empty() ? last_line : fallback);
                app.last_error = utf8_to_w(shown);
            } else if (mode_arg == L"install") {
                app.force_setup = false;
            }
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        app.worker_running.store(false);
        refresh_install_state(app);
        PostMessageW(app.hwnd, WM_APP_STATE_UPDATE, 0, 0);
    }).detach();
}

// ---- Native File/Folder Pickers ----
static std::wstring pick_nup_file(HWND owner) {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Switch 2 NUP Backup (*.nup)\0*.nup\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Select Switch 2 .nup Backup";
    if (GetOpenFileNameW(&ofn)) return file;
    return L"";
}

static std::wstring pick_folder(HWND owner) {
    std::wstring result;
    IFileOpenDialog *dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&dlg))) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(L"Select exefs/ + romfs/ Backup Folder");
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    return result;
}

// ---- Direct2D Rendering ----
static D2D1_COLOR_F hex_col(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(((rgb >> 16) & 0xff) / 255.0f,
                        ((rgb >> 8) & 0xff) / 255.0f,
                        (rgb & 0xff) / 255.0f, a);
}

static void draw_button(LauncherApp &app, ID2D1SolidColorBrush *br, int id, RectF rc,
                        const wchar_t *label, bool primary, bool enabled, bool big = false) {
    if (enabled) app.btns.push_back({ id, rc });
    bool hov = enabled && (app.hover_id == id);

    if (primary) {
        br->SetColor(enabled ? (hov ? hex_col(0xE6C36E) : hex_col(0xCFA74E)) : hex_col(0x3B3830));
        app.rt->FillRoundedRectangle(rc.rr(8.0f), br);
        br->SetColor(enabled ? hex_col(0x111318) : hex_col(0x7A7E8A));
    } else {
        br->SetColor(hov ? hex_col(0x232A38) : hex_col(0x181D27));
        app.rt->FillRoundedRectangle(rc.rr(8.0f), br);
        br->SetColor(hov ? hex_col(0xD4AF5A, 0.7f) : hex_col(0x2E3546));
        app.rt->DrawRoundedRectangle(rc.rr(8.0f), br, 1.2f);
        br->SetColor(enabled ? hex_col(0xECEFF4) : hex_col(0x6D7482));
    }
    IDWriteTextFormat *tf = big ? app.tf_btn_big : app.tf_btn;
    app.rt->DrawText(label, (UINT32)wcslen(label), tf, rc.d2d(), br);
}

static void draw_error_banner(LauncherApp &app, ID2D1SolidColorBrush *br, RectF box, const std::wstring &err) {
    if (err.empty()) return;
    br->SetColor(hex_col(0x2A1419));
    app.rt->FillRoundedRectangle(box.rr(6.0f), br);
    br->SetColor(hex_col(0xC93B4B, 0.65f));
    app.rt->DrawRoundedRectangle(box.rr(6.0f), br, 1.0f);

    RectF tr{ box.x + 10.0f, box.y + 5.0f, std::max(10.0f, box.w - 20.0f), std::max(10.0f, box.h - 10.0f) };
    IDWriteTextFormat *fmt = app.tf_err ? app.tf_err : app.tf_small;
    std::wstring shown = err;

    auto fits = [&](const std::wstring &s) -> bool {
        if (!app.dw_factory || !fmt) return true;
        IDWriteTextLayout *tl = nullptr;
        if (FAILED(app.dw_factory->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, tr.w, tr.h, &tl)) || !tl)
            return true;
        DWRITE_TEXT_METRICS tm{};
        bool ok = SUCCEEDED(tl->GetMetrics(&tm)) ? (tm.height <= tr.h + 0.5f) : true;
        tl->Release();
        return ok;
    };

    if (!fits(shown)) {
        const std::wstring suffix = (shown.find(L"setup.log") == std::wstring::npos) ? L"… (see setup.log)" : L"…";
        size_t lo = 0, hi = shown.size();
        while (lo < hi) {
            size_t mid = lo + (hi - lo + 1) / 2;
            if (fits(shown.substr(0, mid) + suffix)) lo = mid;
            else hi = mid - 1;
        }
        while (lo > 0 && (shown[lo - 1] == L' ' || shown[lo - 1] == L'\t')) lo--;
        shown = shown.substr(0, lo) + suffix;
    }

    br->SetColor(hex_col(0xF07B86));
    app.rt->DrawText(shown.c_str(), (UINT32)shown.size(), fmt, tr.d2d(), br, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

static void render_app(LauncherApp &app) {
    if (!app.rt) {
        RECT cr{}; GetClientRect(app.hwnd, &cr);
        D2D1_SIZE_U sz = D2D1::SizeU(std::max<LONG>(1, cr.right - cr.left), std::max<LONG>(1, cr.bottom - cr.top));
        app.d2d_factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(app.hwnd, sz),
            &app.rt);
        if (!app.rt) return;
    }

    app.btns.clear();
    D2D1_SIZE_F sz = app.rt->GetSize();
    float W = sz.width, H = sz.height;

    app.rt->BeginDraw();
    app.rt->Clear(hex_col(0x0C0E13));

    ID2D1SolidColorBrush *br = nullptr;
    app.rt->CreateSolidColorBrush(hex_col(0xFFFFFF), &br);

    // Subtle crimson/gold top ambient header bar
    br->SetColor(hex_col(0x131720));
    app.rt->FillRectangle(D2D1::RectF(0, 0, W, 118), br);
    br->SetColor(hex_col(0xC93B4B, 0.35f));
    app.rt->FillRectangle(D2D1::RectF(0, 116, W, 118), br);
    br->SetColor(hex_col(0xD4AF5A, 0.85f));
    app.rt->FillRectangle(D2D1::RectF(36, 116, 260, 118), br);

    // Crest emblem badge (stylized diamond-weave vector in header)
    {
        float cx = 62, cy = 58;
        br->SetColor(hex_col(0x1D222E));
        app.rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 28, 28), br);
        br->SetColor(hex_col(0xD4AF5A));
        app.rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 28, 28), br, 2.0f);
        br->SetColor(hex_col(0xC93B4B));
        D2D1_RECT_F dm = D2D1::RectF(cx - 10, cy - 10, cx + 10, cy + 10);
        app.rt->SetTransform(D2D1::Matrix3x2F::Rotation(45.0f, D2D1::Point2F(cx, cy)));
        app.rt->FillRectangle(dm, br);
        br->SetColor(hex_col(0xF0D084));
        app.rt->DrawRectangle(dm, br, 1.5f);
        app.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    // Title + GPU pill
    br->SetColor(hex_col(0xF5F6FA));
    app.rt->DrawText(L"DARK EMBLEM", 11, app.tf_title,
                     D2D1::RectF(106, 28, W - 36, 62), br);
    br->SetColor(hex_col(0x9BA3B5));
    std::wstring sub = L"Native x86-64 PC Runtime  •  " + app.gpu_name;
    app.rt->DrawText(sub.c_str(), (UINT32)sub.size(), app.tf_sub,
                     D2D1::RectF(106, 66, W - 36, 94), br);

    // Lock snapshot of state (avoid data races with background worker thread)
    UiMode cur_mode;
    int pct;
    std::wstring stage, detail, err, settings_summary;
    bool has_bc7 = false, force_setup = false, is_installed = false;
    uint64_t install_start_ms = 0;
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        cur_mode = app.mode;
        pct = app.progress_pct;
        stage = app.progress_stage;
        detail = app.progress_detail;
        err = app.last_error;
        settings_summary = app.settings_summary;
        has_bc7 = app.has_bc7;
        force_setup = app.force_setup;
        is_installed = app.is_installed;
        install_start_ms = app.install_start_ms;
    }

    if (cur_mode == UiMode::Setup) {
        // ---- SETUP WIZARD VIEW ----
        RectF drop{ 36, 144, W - 72, 168 };
        br->SetColor(hex_col(0x131720));
        app.rt->FillRoundedRectangle(drop.rr(12), br);
        br->SetColor(app.input_valid ? hex_col(0x3DB87A, 0.8f) : hex_col(0xD4AF5A, 0.45f));
        app.rt->DrawRoundedRectangle(drop.rr(12), br, 1.6f);

        br->SetColor(hex_col(0xF0F2F7));
        const wchar_t *h1 = L"Supply Your Switch 2 Game Backup";
        app.rt->DrawText(h1, (UINT32)wcslen(h1), app.tf_head, D2D1::RectF(60, 162, W - 60, 190), br);
        br->SetColor(hex_col(0x9BA3B5));
        const wchar_t *h2 = L"Drag & drop a Switch 2 .nup package or an unpacked exefs/ + romfs/ folder here.\n"
                            L"Requires your own legally obtained target backup (v1.2.0 / 131072).";
        app.rt->DrawText(h2, (UINT32)wcslen(h2), app.tf_body, D2D1::RectF(60, 194, W - 60, 238), br);

        draw_button(app, br, BTN_PICK_NUP, { 60, 250, 210, 40 }, L"Select .nup File...", false, true);
        draw_button(app, br, BTN_PICK_DIR, { 286, 250, 230, 40 }, L"Select exefs+romfs Folder...", false, true);

        // Verification status card
        RectF vcard{ 36, 330, W - 72, 82 };
        br->SetColor(hex_col(0x141822));
        app.rt->FillRoundedRectangle(vcard.rr(10), br);
        if (!app.selected_input.empty()) {
            br->SetColor(app.input_valid ? hex_col(0x3DB87A) : hex_col(0xD94B5A));
            app.rt->DrawRoundedRectangle(vcard.rr(10), br, 1.4f);
            app.rt->DrawText(app.verify_title.c_str(), (UINT32)app.verify_title.size(), app.tf_head,
                             D2D1::RectF(56, 344, W - 56, 370), br);
            br->SetColor(hex_col(0xB8C0D0));
            app.rt->DrawText(app.verify_detail.c_str(), (UINT32)app.verify_detail.size(), app.tf_small,
                             D2D1::RectF(56, 374, W - 56, 402), br);
        } else {
            br->SetColor(hex_col(0x262D3D));
            app.rt->DrawRoundedRectangle(vcard.rr(10), br, 1.0f);
            br->SetColor(hex_col(0x7B8396));
            const wchar_t *wait_txt = L"No backup selected yet. Portable LLVM (~62 MB) downloads automatically on first setup — no Visual Studio needed.";
            app.rt->DrawText(wait_txt, (UINT32)wcslen(wait_txt), app.tf_small,
                             D2D1::RectF(56, 358, W - 56, 396), br);
        }

        // BC7 Optional Checkbox
        RectF chk{ 36, 430, 480, 30 };
        app.btns.push_back({ BTN_CHECK_BC7, chk });
        RectF box{ 36, 434, 20, 20 };
        br->SetColor(app.opt_bc7 ? hex_col(0xD4AF5A) : hex_col(0x181D27));
        app.rt->FillRoundedRectangle(box.rr(4), br);
        br->SetColor(hex_col(0xD4AF5A));
        app.rt->DrawRoundedRectangle(box.rr(4), br, 1.4f);
        if (app.opt_bc7) {
            br->SetColor(hex_col(0x0C0E13));
            app.rt->DrawText(L"✓", 1, app.tf_btn, box.d2d(), br);
        }
        br->SetColor(hex_col(0xD2D8E4));
        const wchar_t *bc7_lbl = L"Build BC7 Texture Cache (saves 2–4 GB VRAM on 6–12 GB GPUs; +3 min)";
        app.rt->DrawText(bc7_lbl, (UINT32)wcslen(bc7_lbl), app.tf_body, D2D1::RectF(66, 434, W - 36, 460), br);

        if (!err.empty()) {
            draw_error_banner(app, br, { 36, 466, W - 72, 48 }, err);
        }

        draw_button(app, br, BTN_START_INSTALL, { W - 296, H - 76, 260, 48 },
                    L"INSTALL & RECOMPILE", true, app.input_valid, true);
        draw_button(app, br, BTN_NOTICES, { 36, H - 68, 170, 36 }, L"Third-Party Notices", false, true);
        if (force_setup && is_installed) {
            draw_button(app, br, BTN_BACK_HUB, { 218, H - 68, 128, 36 }, L"Back to Hub", false, true);
        }

    } else if (cur_mode == UiMode::Installing) {
        // ---- INSTALLING / RECOMPILING VIEW ----
        RectF card{ 36, 160, W - 72, 260 };
        br->SetColor(hex_col(0x131720));
        app.rt->FillRoundedRectangle(card.rr(12), br);
        br->SetColor(hex_col(0x283042));
        app.rt->DrawRoundedRectangle(card.rr(12), br, 1.2f);

        br->SetColor(hex_col(0xD4AF5A));
        std::wstring hdr = stage + L"  (" + std::to_wstring(pct) + L"%)";
        app.rt->DrawText(hdr.c_str(), (UINT32)hdr.size(), app.tf_title, D2D1::RectF(64, 192, W - 210, 228), br);

        // Live elapsed timer (updates every 500ms so user always sees active progress)
        if (install_start_ms > 0) {
            uint64_t sec = (GetTickCount64() - install_start_ms) / 1000ull;
            wchar_t tbuf[64];
            swprintf_s(tbuf, L"Elapsed %02llu:%02llu", (unsigned long long)(sec / 60ull), (unsigned long long)(sec % 60ull));
            br->SetColor(hex_col(0x8D96AA));
            app.rt->DrawText(tbuf, (UINT32)wcslen(tbuf), app.tf_sub, D2D1::RectF(W - 200, 196, W - 64, 226), br);
        }

        br->SetColor(hex_col(0xC2C9D6));
        app.rt->DrawText(detail.c_str(), (UINT32)detail.size(), app.tf_body, D2D1::RectF(64, 236, W - 64, 274), br);

        // Progress bar track + golden fill + subtle animated activity sheen
        RectF track{ 64, 296, W - 128, 18 };
        br->SetColor(hex_col(0x1E2432));
        app.rt->FillRoundedRectangle(track.rr(9), br);
        float fill_w = std::max(18.0f, track.w * (pct / 100.0f));
        RectF fill{ track.x, track.y, fill_w, track.h };
        br->SetColor(hex_col(0xD4AF5A));
        app.rt->FillRoundedRectangle(fill.rr(9), br);
        if (fill_w > 40.0f) {
            float sheen_w = std::min(72.0f, fill_w * 0.35f);
            float t_pos = fmodf(app.anim_phase, 1.0f);
            float sx = fill.x + t_pos * (fill_w - sheen_w);
            RectF sheen{ sx, fill.y + 2.0f, sheen_w, fill.h - 4.0f };
            br->SetColor(hex_col(0xFFF0BE, 0.38f));
            app.rt->FillRoundedRectangle(sheen.rr(7), br);
        }

        br->SetColor(hex_col(0x7F879B));
        const wchar_t *tip = L"Translating Switch 2 binaries to native x86-64 and precompiling shaders for your GPU.\n"
                             L"All temporary source and object files are automatically deleted upon completion.";
        app.rt->DrawText(tip, (UINT32)wcslen(tip), app.tf_small, D2D1::RectF(64, 340, W - 64, 392), br);

        draw_button(app, br, BTN_CANCEL_INSTALL, { W - 176, H - 72, 140, 40 }, L"Cancel", false, true);

    } else {
        // ---- MAIN HUB VIEW ----
        RectF hero{ 36, 144, W - 72, 148 };
        br->SetColor(hex_col(0x141822));
        app.rt->FillRoundedRectangle(hero.rr(12), br);
        br->SetColor(hex_col(0xD4AF5A, 0.5f));
        app.rt->DrawRoundedRectangle(hero.rr(12), br, 1.3f);

        br->SetColor(hex_col(0x3DB87A));
        const wchar_t *ready = L"● READY TO PLAY  •  VERIFIED x86-64 INSTALL";
        app.rt->DrawText(ready, (UINT32)wcslen(ready), app.tf_small, D2D1::RectF(60, 166, 520, 188), br);

        br->SetColor(hex_col(0xF5F6FA));
        const wchar_t *ver = L"Dark Emblem  •  Native x86-64 (v1.2.0)";
        app.rt->DrawText(ver, (UINT32)wcslen(ver), app.tf_head, D2D1::RectF(60, 194, 520, 224), br);

        br->SetColor(hex_col(0x9EA7BA));
        app.rt->DrawText(settings_summary.c_str(), (UINT32)settings_summary.size(), app.tf_body,
                         D2D1::RectF(60, 232, 540, 266), br);

        draw_button(app, br, BTN_PLAY, { W - 266, 186, 206, 62 }, L"▶  PLAY", true, true, true);

        // 2x2 Utility Grid
        float gx = 36, gy = 316, gw = (W - 72 - 18) * 0.5f, gh = 68;
        draw_button(app, br, BTN_SETTINGS,        { gx,           gy,      gw, gh }, L"Graphics & Upscaler Settings", false, true);
        draw_button(app, br, BTN_REBUILD_SHADERS, { gx + gw + 18, gy,      gw, gh }, L"Rebuild GPU Shader Cache",     false, true);
        draw_button(app, br, BTN_BUILD_BC7,       { gx,           gy + 84, gw, gh },
                    has_bc7 ? L"Rebuild BC7 Texture Cache (Installed)" : L"Build BC7 Texture Cache (Save VRAM)", false, true);
        draw_button(app, br, BTN_OPEN_SAVE,       { gx + gw + 18, gy + 84, gw, gh }, L"Open Save Data Folder",        false, true);

        if (!err.empty()) {
            draw_error_banner(app, br, { 36, 486, W - 72, 36 }, err);
        }

        draw_button(app, br, BTN_REINSTALL, { 36, H - 64, 195, 36 }, L"Reinstall / Change Backup", false, true);
        draw_button(app, br, BTN_NOTICES,   { W - 206, H - 64, 170, 36 }, L"Third-Party Notices", false, true);
    }

    br->Release();
    if (app.rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        app.rt->Release();
        app.rt = nullptr;
    }
}

static void handle_click(LauncherApp &app, int id) {
    switch (id) {
        case BTN_PICK_NUP: {
            auto p = pick_nup_file(app.hwnd);
            if (!p.empty()) { inspect_selected_backup(app, p); InvalidateRect(app.hwnd, NULL, FALSE); }
            break;
        }
        case BTN_PICK_DIR: {
            auto p = pick_folder(app.hwnd);
            if (!p.empty()) { inspect_selected_backup(app, p); InvalidateRect(app.hwnd, NULL, FALSE); }
            break;
        }
        case BTN_CHECK_BC7:
            app.opt_bc7 = !app.opt_bc7;
            InvalidateRect(app.hwnd, NULL, FALSE);
            break;
        case BTN_START_INSTALL:
            if (app.input_valid) {
                std::wstring pf_err;
                if (!preflight_check(app, pf_err)) {
                    {
                        std::lock_guard<std::mutex> lk(app.mtx);
                        app.last_error = pf_err;
                    }
                    InvalidateRect(app.hwnd, NULL, FALSE);
                } else {
                    run_recompiler_async(app, L"install", true);
                }
            }
            break;
        case BTN_CANCEL_INSTALL:
            app.cancel_req.store(true);
            terminate_active_children(app);
            break;
        case BTN_BACK_HUB:
            {
                std::lock_guard<std::mutex> lk(app.mtx);
                app.force_setup = false;
                app.last_error.clear();
            }
            refresh_install_state(app);
            InvalidateRect(app.hwnd, NULL, FALSE);
            break;
        case BTN_PLAY: {
            std::wstring exe = app.install_root + L"\\Play.exe";
            ShellExecuteW(app.hwnd, L"open", exe.c_str(), NULL, app.install_root.c_str(), SW_SHOWNORMAL);
            break;
        }
        case BTN_SETTINGS: {
            std::wstring exe = app.install_root + L"\\Settings.exe";
            ShellExecuteW(app.hwnd, L"open", exe.c_str(), NULL, app.install_root.c_str(), SW_SHOWNORMAL);
            break;
        }
        case BTN_REBUILD_SHADERS:
            run_recompiler_async(app, L"rebuild-shaders", false);
            break;
        case BTN_BUILD_BC7:
            run_recompiler_async(app, L"build-bc7", false);
            break;
        case BTN_OPEN_SAVE: {
            std::wstring sv = app.install_root + L"\\save";
            CreateDirectoryW(sv.c_str(), NULL);
            ShellExecuteW(app.hwnd, L"explore", sv.c_str(), NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        case BTN_REINSTALL:
            {
                std::lock_guard<std::mutex> lk(app.mtx);
                app.force_setup = true;
                app.mode = UiMode::Setup;
                app.last_error.clear();
            }
            InvalidateRect(app.hwnd, NULL, FALSE);
            break;
        case BTN_NOTICES: {
            std::wstring p = app.install_root + L"\\THIRD_PARTY_NOTICES.txt";
            ShellExecuteW(app.hwnd, L"open", p.c_str(), NULL, app.install_root.c_str(), SW_SHOWNORMAL);
            break;
        }
    }
}

static void client_px_to_dip(const LauncherApp &app, LPARAM lp, float &x, float &y) {
    x = (float)GET_X_LPARAM(lp);
    y = (float)GET_Y_LPARAM(lp);
    if (app.rt) {
        FLOAT dpiX = 96.0f, dpiY = 96.0f;
        app.rt->GetDpi(&dpiX, &dpiY);
        if (dpiX > 0.0f && dpiY > 0.0f) {
            x *= 96.0f / dpiX;
            y *= 96.0f / dpiY;
        }
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            render_app(g_app);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            if (g_app.rt) {
                g_app.rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE: {
            float x = 0.0f, y = 0.0f;
            client_px_to_dip(g_app, lp, x, y);
            int hit = -1;
            for (const auto &b : g_app.btns) if (b.rc.hit(x, y)) { hit = b.id; break; }
            if (hit != g_app.hover_id) {
                g_app.hover_id = hit;
                SetCursor(LoadCursor(NULL, hit >= 0 ? IDC_HAND : IDC_ARROW));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            float x = 0.0f, y = 0.0f;
            client_px_to_dip(g_app, lp, x, y);
            for (const auto &b : g_app.btns) {
                if (b.rc.hit(x, y)) { handle_click(g_app, b.id); break; }
            }
            return 0;
        }
        case WM_DROPFILES: {
            HDROP hd = (HDROP)wp;
            wchar_t path[MAX_PATH * 2];
            UiMode cur_mode;
            {
                std::lock_guard<std::mutex> lk(g_app.mtx);
                cur_mode = g_app.mode;
            }
            if (DragQueryFileW(hd, 0, path, MAX_PATH * 2) > 0 && cur_mode == UiMode::Setup) {
                inspect_selected_backup(g_app, path);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            DragFinish(hd);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE && !g_app.worker_running.load()) {
                refresh_install_state(g_app);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                if (g_app.worker_running.load()) {
                    g_app.anim_phase += 0.08f;
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    KillTimer(hwnd, 1);
                }
            }
            return 0;
        case WM_APP_STATE_UPDATE:
            if (!g_app.worker_running.load()) KillTimer(hwnd, 1);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_CLOSE:
            g_app.cancel_req.store(true);
            terminate_active_children(g_app);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            g_app.cancel_req.store(true);
            terminate_active_children(g_app);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmd, int show) {
    (void)prev; (void)cmd;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    g_app.hinst = hi;

    // Resolve install root (folder containing Launcher.exe)
    wchar_t exe[MAX_PATH * 2];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
    while (len > 0 && exe[len - 1] != L'\\' && exe[len - 1] != L'/') len--;
    if (len > 0) exe[len - 1] = L'\0';
    g_app.install_root = exe;

    detect_gpu(g_app);
    refresh_install_state(g_app);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_app.d2d_factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&g_app.dw_factory);

    auto mk_fmt = [&](float pt, DWRITE_FONT_WEIGHT w, DWRITE_TEXT_ALIGNMENT align, IDWriteTextFormat **out) {
        g_app.dw_factory->CreateTextFormat(L"Segoe UI", NULL, w, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, pt, L"en-us", out);
        if (*out) {
            (*out)->SetTextAlignment(align);
            (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    };
    mk_fmt(22.0f, DWRITE_FONT_WEIGHT_BOLD,     DWRITE_TEXT_ALIGNMENT_LEADING, &g_app.tf_title);
    mk_fmt(13.0f, DWRITE_FONT_WEIGHT_NORMAL,   DWRITE_TEXT_ALIGNMENT_LEADING, &g_app.tf_sub);
    mk_fmt(16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, &g_app.tf_head);
    mk_fmt(13.5f, DWRITE_FONT_WEIGHT_NORMAL,    DWRITE_TEXT_ALIGNMENT_LEADING, &g_app.tf_body);
    mk_fmt(12.0f, DWRITE_FONT_WEIGHT_NORMAL,    DWRITE_TEXT_ALIGNMENT_LEADING, &g_app.tf_small);
    mk_fmt(13.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER,  &g_app.tf_btn);
    mk_fmt(16.0f, DWRITE_FONT_WEIGHT_BOLD,     DWRITE_TEXT_ALIGNMENT_CENTER,  &g_app.tf_btn_big);

    // Dedicated error text format: top-aligned, wrapped, with character-granularity ellipsis trimming
    g_app.dw_factory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &g_app.tf_err);
    if (g_app.tf_err) {
        g_app.tf_err->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_app.tf_err->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        g_app.tf_err->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TRIMMING trim{ DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        IDWriteInlineObject *ellipsis = nullptr;
        g_app.dw_factory->CreateEllipsisTrimmingSign(g_app.tf_err, &ellipsis);
        g_app.tf_err->SetTrimming(&trim, ellipsis);
        if (ellipsis) ellipsis->Release();
    }

    HICON hIco = LoadIconW(hi, MAKEINTRESOURCEW(1));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"DarkEmblem_Launcher_D2D";
    wc.hIcon = hIco ? hIco : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    RECT rc{ 0, 0, kWinW, kWinH };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

    g_app.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName,
                                 L"Dark Emblem — Launcher",
                                 style, sx, sy, ww, wh, NULL, NULL, hi, NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_app.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    ShowWindow(g_app.hwnd, show);
    UpdateWindow(g_app.hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    auto rel_tf = [](IDWriteTextFormat *&p) { if (p) { p->Release(); p = nullptr; } };
    rel_tf(g_app.tf_title); rel_tf(g_app.tf_sub); rel_tf(g_app.tf_head);
    rel_tf(g_app.tf_body); rel_tf(g_app.tf_small); rel_tf(g_app.tf_err);
    rel_tf(g_app.tf_btn); rel_tf(g_app.tf_btn_big);
    if (g_app.rt) g_app.rt->Release();
    if (g_app.d2d_factory) g_app.d2d_factory->Release();
    if (g_app.dw_factory) g_app.dw_factory->Release();
    CoUninitialize();
    return 0;
}
