// Public release Settings.exe wrapper:
// - Embeds custom settings.ico (resource ID 1) for both Explorer and window/taskbar icons
// - Resolves bin\dlss\ first (clean x86-64 folder layout) and stores settings in save\lantern-settings.ini
// - Strips internal --check test mode and updates window title
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void rel_resolve_dlss_dir(void);
#define resolve_dlss_dir orig_resolve_dlss_dir
#define WinMain lantern_settings_inner_WinMain
#include "../../src/lantern/tools/settings_app/lantern_settings.c"
#undef WinMain
#undef resolve_dlss_dir

static void rel_resolve_dlss_dir(void) {
    static const char *const k_rel[] = { "dlss\\", "bin\\dlss\\", "..\\out\\dlss\\", "out\\dlss\\" };
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < sizeof k_rel / sizeof k_rel[0]; i++) {
            char cand[MAX_PATH * 2];
            snprintf(cand, sizeof cand, "%s%s", g_dir, k_rel[i]);
            if (pass == 0) {
                char p1[MAX_PATH * 2], p2[MAX_PATH * 2];
                snprintf(p1, sizeof p1, "%snvngx_dlss.dll", cand);
                snprintf(p2, sizeof p2, "%samd_fidelityfx_loader_vk.dll", cand);
                if (path_is_file(p1) || path_is_file(p2)) { snprintf(g_dlss_dir, sizeof g_dlss_dir, "%s", cand); return; }
            } else if (path_is_dir(cand)) {
                snprintf(g_dlss_dir, sizeof g_dlss_dir, "%s", cand); return;
            }
        }
    }
    snprintf(g_dlss_dir, sizeof g_dlss_dir, "%sbin\\dlss\\", g_dir);
}

#define ID_DETWARN 195
static int g_rel_content_h = 0;
static int g_rel_scroll_y = 0;

static LnSetTexDetail rel_detail_recommended(void) {
    LnSetTexDetail rec = LN_SET_DETAIL_HIGH;
    if (g_caps.vram) {
        (void)ln_settings_tex_detail_drop(LN_SET_DETAIL_AUTO, g_caps.vram, &rec);
    }
    const uint64_t ram_mib = ln_host_ram_mib();
    if (ram_mib > 0 && ram_mib <= 16896u && rec < LN_SET_DETAIL_LOW)
        rec = LN_SET_DETAIL_LOW;
    return rec;
}

static void rel_update_detail_warning(void) {
    HWND wnd = item(ID_DETWARN);
    if (!wnd) return;
    const LRESULT dc = SendMessageA(item(ID_TEXDETAIL), CB_GETCURSEL, 0, 0);
    const LnSetTexDetail rec = rel_detail_recommended();
    const bool exceeds = (dc >= LN_SET_DETAIL_HIGH && dc < LN_SET_DETAIL_COUNT && (int)dc < (int)rec);
    const bool low_6g = caps_is_low_vram_6g(&g_caps);
    char warn[384] = "";
    if (exceeds && low_6g) {
        snprintf(warn, sizeof warn,
                 "Warning: Exceeds recommended Auto (%s) for %.1f GB VRAM. Recommend 1080p, Detail = Lowest, Quality = Low, BC7 cache, and Frame Generation Off above 1080p.",
                 k_det_label[rec], (double)g_caps.vram / 1073741824.0);
    } else if (exceeds) {
        snprintf(warn, sizeof warn,
                 "Warning: Exceeds recommended Auto tier (%s) for your GPU/RAM - texture cache will be clamped for safety; use Auto if you see hitching.",
                 k_det_label[rec]);
    } else if (low_6g) {
        snprintf(warn, sizeof warn,
                 "Low-VRAM GPU (%.1f GB): Recommend 1080p output, Texture Detail = Lowest (or Auto), Texture Quality = Low, BC7 cache, and Frame Generation Off above 1080p.",
                 (double)g_caps.vram / 1073741824.0);
    }
    SetWindowTextA(wnd, warn);
}

static void rel_insert_detail_warning_ctrl(void) {
    HWND hDesc = item(ID_DETDESC);
    if (!hDesc) return;
    RECT dr;
    GetWindowRect(hDesc, &dr);
    MapWindowPoints(NULL, g_wnd, (POINT *)&dr, 2);
    const int shift = 52;
    for (HWND ch = GetWindow(g_wnd, GW_CHILD); ch; ch = GetWindow(ch, GW_HWNDNEXT)) {
        if (ch == hDesc) continue;
        RECT cr;
        GetWindowRect(ch, &cr);
        MapWindowPoints(NULL, g_wnd, (POINT *)&cr, 2);
        if (cr.top >= dr.bottom) {
            SetWindowPos(ch, NULL, cr.left, cr.top + shift, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    mk("STATIC", "", SS_LEFT, 12, dr.bottom + 2, 456, 48, ID_DETWARN);
}

static void rel_scroll_to(HWND h, int pos) {
    RECT cr;
    GetClientRect(h, &cr);
    const int page = cr.bottom - cr.top;
    int max_pos = g_rel_content_h - page;
    if (max_pos < 0) max_pos = 0;
    if (pos < 0) pos = 0;
    if (pos > max_pos) pos = max_pos;
    if (pos == g_rel_scroll_y) return;
    const int dy = g_rel_scroll_y - pos;
    g_rel_scroll_y = pos;
    SetScrollPos(h, SB_VERT, pos, TRUE);
    ScrollWindowEx(h, 0, dy, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    UpdateWindow(h);
}

static void rel_fit_window_to_workarea(HWND h) {
    int max_bottom = 0;
    for (HWND ch = GetWindow(h, GW_CHILD); ch; ch = GetWindow(ch, GW_HWNDNEXT)) {
        RECT cr;
        GetWindowRect(ch, &cr);
        MapWindowPoints(NULL, h, (POINT *)&cr, 2);
        if (cr.bottom > max_bottom) max_bottom = cr.bottom;
    }
    g_rel_content_h = max_bottom + 14;
    g_rel_scroll_y = 0;

    RECT wa = { 0, 0, 1920, 1040 };
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    const int wa_w = wa.right - wa.left;
    const int wa_h = wa.bottom - wa.top;
    const int max_win_h = wa_h > 260 ? (wa_h - 16) : wa_h;

    DWORD style = (DWORD)GetWindowLongA(h, GWL_STYLE);
    RECT rc = { 0, 0, 480, g_rel_content_h };
    AdjustWindowRect(&rc, style & ~(DWORD)WS_VSCROLL, FALSE);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;
    bool need_vscroll = (win_h > max_win_h);
    if (need_vscroll) {
        style |= WS_VSCROLL;
        SetWindowLongA(h, GWL_STYLE, (LONG)style);
        RECT rc2 = { 0, 0, 480 + GetSystemMetrics(SM_CXVSCROLL), g_rel_content_h };
        AdjustWindowRect(&rc2, style, FALSE);
        win_w = rc2.right - rc2.left;
        win_h = max_win_h;
    }
    int wx = wa.left + (wa_w - win_w) / 2;
    int wy = wa.top + (wa_h - win_h) / 2;
    if (wx < wa.left) wx = wa.left;
    if (wy < wa.top) wy = wa.top;
    SetWindowPos(h, NULL, wx, wy, win_w, win_h, SWP_NOZORDER | SWP_FRAMECHANGED);

    if (need_vscroll) {
        RECT cr;
        GetClientRect(h, &cr);
        SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, g_rel_content_h - 1, (UINT)(cr.bottom - cr.top), 0, 0 };
        SetScrollInfo(h, SB_VERT, &si, TRUE);
    }
}

static LRESULT CALLBACK rel_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CTLCOLORSTATIC && (HWND)l == item(ID_DETWARN)) {
        HDC dc = (HDC)w;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(180, 55, 0));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    if (m == WM_VSCROLL) {
        SCROLLINFO si = { sizeof si, SIF_ALL, 0, 0, 0, 0, 0 };
        GetScrollInfo(h, SB_VERT, &si);
        int pos = g_rel_scroll_y;
        switch (LOWORD(w)) {
        case SB_LINEUP:        pos -= 24; break;
        case SB_LINEDOWN:      pos += 24; break;
        case SB_PAGEUP:        pos -= (int)si.nPage; break;
        case SB_PAGEDOWN:      pos += (int)si.nPage; break;
        case SB_TOP:           pos = si.nMin; break;
        case SB_BOTTOM:        pos = si.nMax; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        default: break;
        }
        rel_scroll_to(h, pos);
        return 0;
    }
    if (m == WM_MOUSEWHEEL) {
        const short delta = (short)HIWORD(w);
        if (delta != 0) rel_scroll_to(h, g_rel_scroll_y - (delta / WHEEL_DELTA) * 48);
        return 0;
    }
    LRESULT r = wndproc(h, m, w, l);
    if (m == WM_COMMAND && HIWORD(w) == CBN_SELCHANGE &&
        (LOWORD(w) == ID_TEXDETAIL || LOWORD(w) == ID_OUTPUT || LOWORD(w) == ID_FRAMEGEN))
        rel_update_detail_warning();
    return r;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd; // Strip --check and dev flags
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    DWORD n = GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    char *slash = (n && n < sizeof g_dir) ? strrchr(g_dir, '\\') : NULL;
    if (!slash) {
        MessageBoxA(NULL, "Cannot find installation folder.", "Graphics Settings", MB_OK | MB_ICONERROR);
        return 1;
    }
    slash[1] = 0;

    // Default to <root>\save\lantern-settings.ini so Settings.exe and Play.exe stay synced
    char save_dir[MAX_PATH * 2], ini_path[MAX_PATH * 2];
    snprintf(save_dir, sizeof save_dir, "%ssave", g_dir);
    CreateDirectoryA(save_dir, NULL);
    snprintf(ini_path, sizeof ini_path, "%ssave\\lantern-settings.ini", g_dir);
    SetEnvironmentVariableA("LN_SETTINGS_FILE", ini_path);
    _putenv_s("LN_SETTINGS_FILE", ini_path);

    // Auto-migrate legacy bin\dlss\ -> dlss\ beside Play.exe / Settings.exe
    {
        char dlss_new[MAX_PATH * 2], dlss_ini[MAX_PATH * 2], dlss_old[MAX_PATH * 2], old_ini[MAX_PATH * 2];
        snprintf(dlss_new, sizeof dlss_new, "%sdlss", g_dir);
        snprintf(dlss_ini, sizeof dlss_ini, "%sdlss\\dlss.ini", g_dir);
        snprintf(dlss_old, sizeof dlss_old, "%sbin\\dlss", g_dir);
        snprintf(old_ini, sizeof old_ini, "%sbin\\dlss\\dlss.ini", g_dir);
        if (!path_is_file(dlss_ini) && path_is_file(old_ini)) MoveFileA(dlss_old, dlss_new);
    }

    char path_error[160];
    if (!settings_ui_path(g_path, sizeof g_path, path_error, sizeof path_error)) {
        MessageBoxA(NULL, path_error, "Graphics Settings", MB_OK | MB_ICONERROR);
        return 1;
    }
    snprintf(g_requested_path, sizeof g_requested_path, "%s", g_path);

    // Probe GPU + bin\dlss\ files
    rel_resolve_dlss_dir();
    probe(&g_caps);
    // Re-check bin\dlss\ in case probe() overwrote g_dlss_dir via orig_resolve_dlss_dir
    if (!g_caps.dlss_files && !g_caps.fsr_files) {
        char p1[MAX_PATH * 2], p2[MAX_PATH * 2];
        snprintf(p1, sizeof p1, "%sbin\\dlss\\nvngx_dlss.dll", g_dir);
        snprintf(p2, sizeof p2, "%sbin\\dlss\\amd_fidelityfx_loader_vk.dll", g_dir);
        if (path_is_file(p1) || path_is_file(p2)) {
            rel_resolve_dlss_dir();
            g_caps.dlss_files = dlss_file_exists("nvngx_dlss.dll") && dlss_file_exists("dlss.ini");
            g_caps.fsr_files = dlss_file_exists("amd_fidelityfx_loader_vk.dll") && dlss_file_exists("amd_fidelityfx_upscaler_vk.dll");
            g_caps.dlss_g_files = dlss_file_exists("sl.dlss_g.dll") && dlss_file_exists("nvngx_dlssg.dll");
            g_caps.ffx_fg_files = dlss_file_exists("amd_fidelityfx_framegeneration_vk.dll");
            if (g_caps.dlss_files && g_caps.vendor == 0x10de) {
                g_caps.dlss_pin_ok = true;
                g_caps.dlss_can_run = true;
                g_caps.dlss_why[0] = 0;
            }
        }
    }

    ln_settings_defaults(&g_set);
    char note[160] = "";
    FILE *f = fopen(g_path, "rb");
    if (f) {
        char text[8192];
        size_t r = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[r] = 0;
        const uint32_t bad = ln_settings_parse(text, &g_set, NULL, 0);
        if (bad) snprintf(note, sizeof note, "%u settings line(s) reset to default.", bad);
    }

    NONCLIENTMETRICSA ncm = { .cbSize = sizeof ncm };
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_font = CreateFontIndirectA(&ncm.lfMessageFont);

    HICON hIco = LoadIconA(hi, MAKEINTRESOURCEA(1));
    WNDCLASSEXA wc = {
        .cbSize = sizeof wc,
        .lpfnWndProc = rel_wndproc,
        .hInstance = hi,
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = GetSysColorBrush(COLOR_BTNFACE),
        .lpszClassName = "dark_emblem_settings_wnd",
        .hIcon = hIco ? hIco : LoadIcon(NULL, IDI_APPLICATION),
        .hIconSm = hIco ? hIco : LoadIcon(NULL, IDI_APPLICATION)
    };
    RegisterClassExA(&wc);
    RECT rc = { 0, 0, 480, 480 + 38 + 134 + 36 + 36 + 28 + 86 + 86 + 86 + 38 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_wnd = CreateWindowExA(0, wc.lpszClassName, "Dark Emblem — Graphics Settings",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                            NULL, NULL, hi, NULL);
    if (!g_wnd) return 1;
    build();
    rel_insert_detail_warning_ctrl();
    rel_fit_window_to_workarea(g_wnd);
    to_ui(&g_set);
    rel_update_detail_warning();
    if (note[0]) SetWindowTextA(item(ID_STATUS), note);
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(g_wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return 0;
}
