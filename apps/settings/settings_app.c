// Settings.exe — Standalone Win32 Graphics & Upscaler Configurator for Dark Emblem
// Edits <InstallDir>\save\lantern-settings.ini and probes local GPU/upscaler capabilities.
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "vk_probe_defs.h"
#include "ini_settings.h"


typedef struct {
    bool have_vk; char gpu[256]; uint32_t vendor;
    bool vk13, shader_object, eds3, vertex_input;
    bool dlss_files; bool dlss_pin_ok; bool dlss_can_run; char dlss_why[200];
    bool fsr_files;
    bool dlss_g_files, ffx_fg_files;
    uint32_t mon_w, mon_h;
    uint64_t vram;
} Caps;
static Caps g_caps;
static char g_dir[MAX_PATH];
static char g_dlss_dir[MAX_PATH * 2];

static bool path_is_file(const char *p) {
    DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool path_is_dir(const char *p) {
    DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
static bool file_exists(const char *name) {
    char p[MAX_PATH * 2]; snprintf(p, sizeof p, "%s%s", g_dir, name);
    return path_is_file(p);
}
static bool dlss_file_exists(const char *name) {
    char p[MAX_PATH * 2]; snprintf(p, sizeof p, "%s%s", g_dlss_dir, name);
    return path_is_file(p);
}
static void resolve_dlss_dir(void) {
    static const char *const k_rel[] = {"dlss\\", "..\\out\\dlss\\", "out\\dlss\\"};
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < sizeof k_rel / sizeof k_rel[0]; i++) {
            char cand[MAX_PATH * 2]; snprintf(cand, sizeof cand, "%s%s", g_dir, k_rel[i]);
            if (pass == 0) {
                char probe1[MAX_PATH * 2], probe2[MAX_PATH * 2];
                snprintf(probe1, sizeof probe1, "%snvngx_dlss.dll", cand);
                snprintf(probe2, sizeof probe2, "%samd_fidelityfx_loader_vk.dll", cand);
                if (path_is_file(probe1) || path_is_file(probe2)) { snprintf(g_dlss_dir, sizeof g_dlss_dir, "%s", cand); return; }
            } else if (path_is_dir(cand)) {
                snprintf(g_dlss_dir, sizeof g_dlss_dir, "%s", cand); return;
            }
        }
    }
    snprintf(g_dlss_dir, sizeof g_dlss_dir, "%sdlss\\", g_dir);
}
static void probe_gpu(Caps *c) {
    HMODULE vk = LoadLibraryA("vulkan-1.dll");
    if (!vk) { snprintf(c->gpu, sizeof c->gpu, "no Vulkan driver found (vulkan-1.dll)"); return; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(vk, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance create = gipa ? (PFN_vkCreateInstance)(void *)gipa(NULL, "vkCreateInstance") : NULL;
    if (!create) { snprintf(c->gpu, sizeof c->gpu, "the Vulkan loader is broken"); return; }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "DarkEmblemSettings", .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst;
    if (create(&ici, NULL, &inst) != VK_SUCCESS) { snprintf(c->gpu, sizeof c->gpu, "Vulkan would not start"); return; }
#define IP(t, n) t n = (t)(void *)gipa(inst, #n)
    IP(PFN_vkEnumeratePhysicalDevices, vkEnumeratePhysicalDevices); IP(PFN_vkGetPhysicalDeviceProperties, vkGetPhysicalDeviceProperties);
    IP(PFN_vkEnumerateDeviceExtensionProperties, vkEnumerateDeviceExtensionProperties); IP(PFN_vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceFeatures2);
    IP(PFN_vkDestroyInstance, vkDestroyInstance); IP(PFN_vkGetPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceMemoryProperties);
#undef IP
    VkPhysicalDevice pd[16]; uint32_t n = 16;
    if (!vkEnumeratePhysicalDevices || vkEnumeratePhysicalDevices(inst, &n, pd) < 0 || !n) { snprintf(c->gpu, sizeof c->gpu, "no Vulkan GPU"); if (vkDestroyInstance) vkDestroyInstance(inst, NULL); return; }

    VkPhysicalDevice p = pd[0]; VkPhysicalDeviceProperties pp;
    for (uint32_t i = 0; i < n; i++) { vkGetPhysicalDeviceProperties(pd[i], &pp); if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { p = pd[i]; break; } }
    vkGetPhysicalDeviceProperties(p, &pp);
    c->have_vk = true; snprintf(c->gpu, sizeof c->gpu, "%s", pp.deviceName); c->vendor = pp.vendorID; c->vk13 = pp.apiVersion >= VK_API_VERSION_1_3;
    if (vkGetPhysicalDeviceMemoryProperties) {
        VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(p, &mp);
        for (uint32_t i = 0; i < mp.memoryHeapCount; i++)
            if ((mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && mp.memoryHeaps[i].size > c->vram) c->vram = mp.memoryHeaps[i].size;
    }
    uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(p, NULL, &ne, NULL);
    VkExtensionProperties *ep = calloc(ne ? ne : 1, sizeof *ep);
    if (ep) {
        vkEnumerateDeviceExtensionProperties(p, NULL, &ne, ep);
        bool so = false, e3 = false, vi = false;
        for (uint32_t i = 0; i < ne; i++) {
            so |= !strcmp(ep[i].extensionName, VK_EXT_SHADER_OBJECT_EXTENSION_NAME);
            e3 |= !strcmp(ep[i].extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
            vi |= !strcmp(ep[i].extensionName, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
        }
        free(ep);
        VkPhysicalDeviceShaderObjectFeaturesEXT fso = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
        VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT fvi = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, .pNext = so ? &fso : NULL};
        VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = vi ? (void *)&fvi : so ? (void *)&fso : NULL};
        if (vkGetPhysicalDeviceFeatures2 && c->vk13) vkGetPhysicalDeviceFeatures2(p, &f2);

        const char *il = getenv("VK_INSTANCE_LAYERS"), *al = getenv("VK_LOADER_LAYERS_ENABLE");
        const bool layer = (il && strstr(il, "VK_LAYER_KHRONOS_shader_object")) || (al && strstr(al, "shader_object"));
        c->shader_object = so && fso.shaderObject && !layer; c->eds3 = e3; c->vertex_input = vi && fvi.vertexInputDynamicState;
    }
    vkDestroyInstance(inst, NULL);
}
static void probe(Caps *c) {
    memset(c, 0, sizeof *c);
    resolve_dlss_dir();
    probe_gpu(c);
    c->dlss_files = dlss_file_exists("nvngx_dlss.dll") && dlss_file_exists("dlss.ini");

    if (c->dlss_files) c->dlss_pin_ok = true;
    if (c->vendor != 0x10de) { char pin[200]; snprintf(pin, sizeof pin, "%s", c->dlss_why);
        snprintf(c->dlss_why, sizeof c->dlss_why, "DLSS needs an NVIDIA GPU%s%s", pin[0] ? "; also " : "", pin); }
    else if (!c->dlss_files) snprintf(c->dlss_why, sizeof c->dlss_why, "DLSS files missing: dlss\\nvngx_dlss.dll and dlss\\dlss.ini");
    if (c->vendor == 0x10de && c->dlss_files) c->dlss_why[0] = 0;
    c->dlss_can_run = c->vendor == 0x10de && c->dlss_files;
    const bool ffx_loader = dlss_file_exists("amd_fidelityfx_loader_vk.dll");
    c->fsr_files = ffx_loader && dlss_file_exists("amd_fidelityfx_upscaler_vk.dll");
    c->dlss_g_files = dlss_file_exists("sl.dlss_g.dll");
    c->ffx_fg_files = ffx_loader && dlss_file_exists("amd_fidelityfx_framegeneration_vk.dll");
    DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) { c->mon_w = dm.dmPelsWidth; c->mon_h = dm.dmPelsHeight; }
}

enum { ID_UP_AUTO = 100, ID_UP_DLSS, ID_UP_FSR3, ID_UP_OFF, ID_QUALITY = 110, ID_OUTPUT, ID_OUT_W, ID_OUT_H, ID_WIN_WINDOWED = 120, ID_WIN_BORDERLESS,
       ID_SHARP = 130, ID_SHARP_VAL, ID_PRESET, ID_SAVE = 140, ID_PLAY, ID_CLOSE, ID_STATUS = 150, ID_RESOLVED, ID_HDR_WHITE_AUTO = 160, ID_HDR_WHITE, ID_HDR_PEAK_AUTO, ID_HDR_PEAK, ID_FRAMEGEN = 170, ID_FPS, ID_REFLEX,
       ID_TEXCOMP = 180, ID_TEXDESC, ID_TEXDETAIL, ID_DETDESC, ID_SHADERLOAD = 190, ID_SHDDESC, ID_DETWARN = 195 };
static HWND g_wnd; static HFONT g_font; static LnUserSettings g_set; static char g_path[MAX_PATH * 2];
static char g_requested_path[MAX_PATH * 2]; 
static const struct { const char *label; uint32_t w, h; int kind; } k_out[] = {
    {"Native (monitor)", 0, 0, LN_SET_OUT_NATIVE}, {"Game's own size", 0, 0, LN_SET_OUT_GAME}, {"1280 x 720", 1280, 720, LN_SET_OUT_SIZE},
    {"1920 x 1080", 1920, 1080, LN_SET_OUT_SIZE}, {"2560 x 1440", 2560, 1440, LN_SET_OUT_SIZE}, {"3840 x 2160 (4K)", 3840, 2160, LN_SET_OUT_SIZE},
    {"Custom", 0, 0, LN_SET_OUT_SIZE}};
#define N_OUT (int)(sizeof k_out / sizeof k_out[0])

static const uint16_t k_fps[] = {0, 30, 60, 120, 144, 165, 240};
#define N_FPS (int)(sizeof k_fps / sizeof k_fps[0])
static uint16_t g_fps_extra;
static int g_fg_sel;
static bool g_reflex_user;

static const char *const k_tex_label[LN_SET_TEX_COUNT] = {"Auto (recommended)", "Full size", "High", "Medium", "Low"};
static const char *const k_tex_desc[LN_SET_TEX_COUNT] = {
    "Picks from your graphics card's video memory: High with 16 GB or more, Medium with 10 GB or more, Low below that.",
    "Textures stay at full size, exactly as the game draws them. Uses the most video memory.",
    "Compresses only the textures that look identical to full size. About a third less texture memory, with no visible difference.",
    "Compresses more textures. Nearly two thirds less texture memory. A few tiny spots, on small text or sharp edges, may look "
    "slightly off up close.",
    "Compresses nearly every texture, for graphics cards with little video memory. About 70% less texture memory. Small blotches "
    "can show on some textures."};

static const char *const k_det_label[LN_SET_DETAIL_COUNT] = {"Auto (recommended)", "High", "Medium", "Low", "Lowest"};
static const char *const k_det_desc[LN_SET_DETAIL_COUNT] = {
    "Picks from your graphics card's video memory: High with 20 GB or more, Medium with 14 GB or more, Low with 10 GB or more, "
    "Lowest below that.",
    "Every texture at full detail. Needs the most video memory (about 21 GB at the game's busiest).",
    "Leaves out the sharpest level of large textures (a quarter of their memory stays, a little softer up close); texture "
    "cache 3 GB. For cards with 14 to 20 GB.",
    "Leaves out the two sharpest levels (a sixteenth stays, visibly softer up close); texture cache 1.5 GB. For cards with 10 "
    "to 14 GB.",
    "Keeps large textures at most 512 x 512 (clearly softer up close); texture cache 1 GB. For cards with less than 10 GB."};

static const char *const k_shd_label[LN_SET_SHADER_COUNT] = {"Auto (recommended)", "All at start", "When first needed"};
static const char *const k_shd_desc[LN_SET_SHADER_COUNT] = {
    "Picks from your PC's RAM and video memory: All at start when RAM > 20 GB and VRAM > 14 GB, otherwise When first needed.",
    "Every game shader is ready before play: no first-use cost, about 3.4 GB of video memory.",
    "Each shader is made from its compiled copy the first time the game uses it (nothing is compiled while playing): "
    "less video memory, but a first use can take a few milliseconds."};
static const char *const k_qlabel[LN_SET_Q_COUNT] = {"Game's choice", "DLAA / Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"};

static HWND mk(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
}
static HWND item(int id) { return GetDlgItem(g_wnd, id); }
static LnSetUpscaler ui_upscaler(void) {
    for (int i = 0; i < 4; i++) if (SendMessageA(item(ID_UP_AUTO + i), BM_GETCHECK, 0, 0) == BST_CHECKED) return (LnSetUpscaler)i;
    return LN_SET_UP_AUTO;
}
static LnSetUpscaler ui_resolved(void) { const char *why = NULL; return ln_settings_resolve_upscaler(ui_upscaler(), g_caps.dlss_can_run, &why); }

static bool fg_item_ok(int i, LnSetUpscaler r) {
    if (i < 0 || i > LN_SET_FG_FSR || !ln_settings_fg_fits((LnSetFrameGen)i, r)) return false;
    return i == LN_SET_FG_OFF || (i == LN_SET_FG_DLSS ? g_caps.dlss_g_files : g_caps.ffx_fg_files);
}
static void refresh(void) {
    EnableWindow(item(ID_HDR_WHITE), SendMessageA(item(ID_HDR_WHITE_AUTO), BM_GETCHECK, 0, 0) != BST_CHECKED);
    EnableWindow(item(ID_HDR_PEAK), SendMessageA(item(ID_HDR_PEAK_AUTO), BM_GETCHECK, 0, 0) != BST_CHECKED);
    const char *why = NULL; const LnSetUpscaler u = ui_upscaler(), r = ln_settings_resolve_upscaler(u, g_caps.dlss_can_run, &why);
    EnableWindow(item(ID_PRESET), r == LN_SET_UP_DLSS);
    EnableWindow(item(ID_SHARP), r == LN_SET_UP_FSR3); EnableWindow(item(ID_SHARP_VAL), r == LN_SET_UP_FSR3);
    EnableWindow(item(ID_QUALITY), r != LN_SET_UP_OFF);
    if (!fg_item_ok(g_fg_sel, r)) { g_fg_sel = LN_SET_FG_OFF; SendMessageA(item(ID_FRAMEGEN), CB_SETCURSEL, (WPARAM)g_fg_sel, 0); }
    InvalidateRect(item(ID_FRAMEGEN), NULL, TRUE);
    const bool fg_dlss = g_fg_sel == LN_SET_FG_DLSS;
    EnableWindow(item(ID_REFLEX), r == LN_SET_UP_DLSS && !fg_dlss);
    SendMessageA(item(ID_REFLEX), BM_SETCHECK, r == LN_SET_UP_DLSS && (fg_dlss || g_reflex_user) ? BST_CHECKED : BST_UNCHECKED, 0);
    const int o = (int)SendMessageA(item(ID_OUTPUT), CB_GETCURSEL, 0, 0);
    EnableWindow(item(ID_OUT_W), o == N_OUT - 1); EnableWindow(item(ID_OUT_H), o == N_OUT - 1);
    char t[256];
    snprintf(t, sizeof t, "In game: %s%s%s%s%s", r == LN_SET_UP_DLSS ? "DLSS" : r == LN_SET_UP_FSR3 ? "AMD FSR (3.1 / 4)" : "no upscaler (plain copy)",
             g_fg_sel == LN_SET_FG_DLSS ? " with DLSS Frame Generation (Reflex on)" : g_fg_sel == LN_SET_FG_FSR ? " with FSR Frame Generation" : "",
             r == LN_SET_UP_DLSS && g_fg_sel != LN_SET_FG_DLSS ? (g_reflex_user ? ", Reflex on" : ", Reflex off") : "",
             why ? ". " : "", why ? why : "");
    if (SendMessageA(item(ID_WIN_BORDERLESS), BM_GETCHECK, 0, 0) == BST_CHECKED)
        strncat(t, ". Borderless covers the whole monitor; a 4K borderless window once lost the display, so windowed is the safe choice.", sizeof t - strlen(t) - 1);
    SetWindowTextA(item(ID_RESOLVED), t);
    {   const LRESULT tc = SendMessageA(item(ID_TEXCOMP), CB_GETCURSEL, 0, 0);
        char yours[96] = "";
        if (tc == LN_SET_TEX_AUTO && g_caps.vram) {
            const LnSetTexCompression tres = ln_settings_resolve_tex_compression(LN_SET_TEX_AUTO, g_caps.vram);
            snprintf(yours, sizeof yours, " This card: %.1f GB, so %s.", (double)g_caps.vram / 1073741824.0, k_tex_label[tres]);
        }
        char td[512];
        snprintf(td, sizeof td, "%s%s Applies the next time the game starts.", tc >= 0 && tc < LN_SET_TEX_COUNT ? k_tex_desc[tc] : "", yours);
        SetWindowTextA(item(ID_TEXDESC), td); }
    {   const LRESULT dc = SendMessageA(item(ID_TEXDETAIL), CB_GETCURSEL, 0, 0);
        LnSetTexDetail res = LN_SET_DETAIL_HIGH;
        (void)ln_settings_tex_detail_drop(dc >= 0 && dc < LN_SET_DETAIL_COUNT ? (LnSetTexDetail)dc : LN_SET_DETAIL_AUTO, g_caps.vram, &res);
        char yours[96] = "";
        if (dc == LN_SET_DETAIL_AUTO && g_caps.vram)
            snprintf(yours, sizeof yours, " This card: %.1f GB, so %s.", (double)g_caps.vram / 1073741824.0, k_det_label[res]);
        char td[512];
        snprintf(td, sizeof td, "%s%s Applies the next time the game starts.", dc >= 0 && dc < LN_SET_DETAIL_COUNT ? k_det_desc[dc] : "", yours);
        SetWindowTextA(item(ID_DETDESC), td);
        LnSetTexDetail rec = LN_SET_DETAIL_HIGH;
        if (g_caps.vram) (void)ln_settings_tex_detail_drop(LN_SET_DETAIL_AUTO, g_caps.vram, &rec);
        const uint64_t ram_mib = ln_host_ram_mib();
        if (ram_mib > 0 && ram_mib <= 16896u && rec < LN_SET_DETAIL_LOW) rec = LN_SET_DETAIL_LOW;
        if (dc >= LN_SET_DETAIL_HIGH && dc < LN_SET_DETAIL_COUNT && (int)dc < (int)rec) {
            char warn[256];
            snprintf(warn, sizeof warn,
                     "Warning: Exceeds recommended Auto tier (%s) for your GPU/RAM - texture cache will be clamped for safety; use Auto if you see hitching.",
                     k_det_label[rec]);
            SetWindowTextA(item(ID_DETWARN), warn);
        } else {
            SetWindowTextA(item(ID_DETWARN), "");
        } }
    {   const LRESULT sc = SendMessageA(item(ID_SHADERLOAD), CB_GETCURSEL, 0, 0);
        char yours[128] = "";
        if (sc == LN_SET_SHADER_AUTO && g_caps.vram) {
            const uint64_t ram_mib = ln_host_ram_mib();
            const LnSetShaderLoad sres = ln_settings_resolve_shader_load(LN_SET_SHADER_AUTO, ram_mib, g_caps.vram >> 20);
            snprintf(yours, sizeof yours, " This PC: %.0f GB RAM / %.1f GB VRAM, so %s.",
                     (double)ram_mib / 1024.0, (double)g_caps.vram / 1073741824.0, k_shd_label[sres]);
        }
        char sd[512];
        snprintf(sd, sizeof sd, "%s%s Applies the next time the game starts.", sc >= 0 && sc < LN_SET_SHADER_COUNT ? k_shd_desc[sc] : "", yours);
        SetWindowTextA(item(ID_SHDDESC), sd); }
    snprintf(t, sizeof t, "%.2f", SendMessageA(item(ID_SHARP), TBM_GETPOS, 0, 0) / 100.0);
    SetWindowTextA(item(ID_SHARP_VAL), t);
}
static void to_ui(const LnUserSettings *s) {
    for (int i = 0; i < 4; i++) SendMessageA(item(ID_UP_AUTO + i), BM_SETCHECK, (int)s->upscaler == i ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(item(ID_QUALITY), CB_SETCURSEL, (WPARAM)s->quality, 0);
    int o = s->output == LN_SET_OUT_NATIVE ? 0 : s->output == LN_SET_OUT_GAME ? 1 : N_OUT - 1;
    for (int i = 2; i < N_OUT - 1; i++) if (s->output == LN_SET_OUT_SIZE && k_out[i].w == s->out_w && k_out[i].h == s->out_h) o = i;
    SendMessageA(item(ID_OUTPUT), CB_SETCURSEL, (WPARAM)o, 0);
    char b[16];
    snprintf(b, sizeof b, "%u", s->output == LN_SET_OUT_SIZE ? s->out_w : (g_caps.mon_w ? g_caps.mon_w : 1920)); SetWindowTextA(item(ID_OUT_W), b);
    snprintf(b, sizeof b, "%u", s->output == LN_SET_OUT_SIZE ? s->out_h : (g_caps.mon_h ? g_caps.mon_h : 1080)); SetWindowTextA(item(ID_OUT_H), b);
    SendMessageA(item(ID_WIN_WINDOWED), BM_SETCHECK, s->window == LN_SET_WINDOWED ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(item(ID_WIN_BORDERLESS), BM_SETCHECK, s->window == LN_SET_BORDERLESS ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(item(ID_SHARP), TBM_SETPOS, TRUE, (LPARAM)(s->sharpness * 100.0f + 0.5f));
    SendMessageA(item(ID_PRESET), CB_SETCURSEL, (WPARAM)s->preset, 0);
    g_fg_sel = (int)s->framegen; SendMessageA(item(ID_FRAMEGEN), CB_SETCURSEL, (WPARAM)g_fg_sel, 0);
    g_reflex_user = s->reflex;
    SendMessageA(item(ID_TEXCOMP), CB_SETCURSEL, (WPARAM)((unsigned)s->tex_compression < LN_SET_TEX_COUNT ? s->tex_compression : LN_SET_TEX_AUTO), 0);
    SendMessageA(item(ID_TEXDETAIL), CB_SETCURSEL, (WPARAM)((unsigned)s->tex_detail < LN_SET_DETAIL_COUNT ? s->tex_detail : LN_SET_DETAIL_AUTO), 0);
    SendMessageA(item(ID_SHADERLOAD), CB_SETCURSEL, (WPARAM)((unsigned)s->shader_load < LN_SET_SHADER_COUNT ? s->shader_load : LN_SET_SHADER_AUTO), 0);
    {
        int fi = -1;
        for (int i = 0; i < N_FPS; i++) if (k_fps[i] == s->fps_limit) fi = i;
        if (fi < 0) {
            char e[32]; snprintf(e, sizeof e, "%u fps (from the settings file)", s->fps_limit);
            if (g_fps_extra) SendMessageA(item(ID_FPS), CB_DELETESTRING, (WPARAM)N_FPS, 0);
            SendMessageA(item(ID_FPS), CB_ADDSTRING, 0, (LPARAM)e); g_fps_extra = s->fps_limit; fi = N_FPS;
        }
        SendMessageA(item(ID_FPS), CB_SETCURSEL, (WPARAM)fi, 0);
    }
    SendMessageA(item(ID_HDR_WHITE_AUTO), BM_SETCHECK, s->hdr_paper_white_nits ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageA(item(ID_HDR_PEAK_AUTO), BM_SETCHECK, s->hdr_peak_nits ? BST_UNCHECKED : BST_CHECKED, 0);
    if (s->hdr_paper_white_nits) snprintf(b, sizeof b, "%u", s->hdr_paper_white_nits); else b[0] = 0;
    SetWindowTextA(item(ID_HDR_WHITE), b);
    if (s->hdr_peak_nits) snprintf(b, sizeof b, "%u", s->hdr_peak_nits); else b[0] = 0;
    SetWindowTextA(item(ID_HDR_PEAK), b);
    refresh();
}
static bool from_ui(LnUserSettings *s, char *err, size_t cap) {
    ln_settings_defaults(s);
    s->upscaler = ui_upscaler();
    s->quality = (LnSetQuality)SendMessageA(item(ID_QUALITY), CB_GETCURSEL, 0, 0);
    const int o = (int)SendMessageA(item(ID_OUTPUT), CB_GETCURSEL, 0, 0);
    s->output = (LnSetOutput)k_out[o < 0 ? 0 : o].kind; s->out_w = k_out[o < 0 ? 0 : o].w; s->out_h = k_out[o < 0 ? 0 : o].h;
    if (o == N_OUT - 1) {
        char b[16]; GetWindowTextA(item(ID_OUT_W), b, sizeof b); s->out_w = (uint32_t)strtoul(b, NULL, 10);
        GetWindowTextA(item(ID_OUT_H), b, sizeof b); s->out_h = (uint32_t)strtoul(b, NULL, 10);
        if (s->out_w < LN_SET_MIN_SIZE || s->out_h < LN_SET_MIN_SIZE || s->out_w > LN_SET_MAX_SIZE || s->out_h > LN_SET_MAX_SIZE) {
            snprintf(err, cap, "The custom size must be between %u and %u pixels on each side.", LN_SET_MIN_SIZE, LN_SET_MAX_SIZE); return false;
        }
    }
    s->window = SendMessageA(item(ID_WIN_BORDERLESS), BM_GETCHECK, 0, 0) == BST_CHECKED ? LN_SET_BORDERLESS : LN_SET_WINDOWED;
    s->sharpness = (float)SendMessageA(item(ID_SHARP), TBM_GETPOS, 0, 0) / 100.0f;
    const LRESULT pr = SendMessageA(item(ID_PRESET), CB_GETCURSEL, 0, 0); s->preset = (uint8_t)(pr > 0 && pr <= 15 ? pr : 0);
    s->framegen = (LnSetFrameGen)g_fg_sel;
    s->reflex = g_reflex_user;
    const LRESULT tc = SendMessageA(item(ID_TEXCOMP), CB_GETCURSEL, 0, 0);
    s->tex_compression = tc >= 0 && tc < LN_SET_TEX_COUNT ? (LnSetTexCompression)tc : LN_SET_TEX_AUTO;
    const LRESULT dc = SendMessageA(item(ID_TEXDETAIL), CB_GETCURSEL, 0, 0);
    s->tex_detail = dc >= 0 && dc < LN_SET_DETAIL_COUNT ? (LnSetTexDetail)dc : LN_SET_DETAIL_AUTO;
    const LRESULT shc = SendMessageA(item(ID_SHADERLOAD), CB_GETCURSEL, 0, 0);
    s->shader_load = shc >= 0 && shc < LN_SET_SHADER_COUNT ? (LnSetShaderLoad)shc : LN_SET_SHADER_AUTO;
    const LRESULT fp = SendMessageA(item(ID_FPS), CB_GETCURSEL, 0, 0);
    s->fps_limit = fp >= 0 && fp < N_FPS ? k_fps[fp] : fp == N_FPS ? g_fps_extra : 0;
    char nits[16];
    if (SendMessageA(item(ID_HDR_WHITE_AUTO), BM_GETCHECK, 0, 0) != BST_CHECKED) {
        GetWindowTextA(item(ID_HDR_WHITE), nits, sizeof nits);
        if (!ln_settings_parse_hdr_nits(nits, &s->hdr_paper_white_nits) || !s->hdr_paper_white_nits) {
            snprintf(err, cap, "Paper white must be Auto or a whole number from 1 to 65535 nits."); return false;
        }
    }
    if (SendMessageA(item(ID_HDR_PEAK_AUTO), BM_GETCHECK, 0, 0) != BST_CHECKED) {
        GetWindowTextA(item(ID_HDR_PEAK), nits, sizeof nits);
        if (!ln_settings_parse_hdr_nits(nits, &s->hdr_peak_nits) || !s->hdr_peak_nits) {
            snprintf(err, cap, "Peak brightness must be Auto or a whole number from 1 to 65535 nits."); return false;
        }
    }
    return true;
}

static bool write_settings(const LnUserSettings *s, char *err, size_t cap) {
    char text[4096]; const size_t n = ln_settings_write(s, text, sizeof text);
    char tmp[sizeof g_path + 8]; snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && n && fwrite(text, 1, n, f) == n; if (f) ok = fclose(f) == 0 && ok;
    ok = ok && MoveFileExA(tmp, g_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) { DeleteFileA(tmp); snprintf(err, cap, "Could not write %s (error %lu).", g_path, GetLastError()); }
    return ok;
}
static bool save(void) {
    char err[256]; LnUserSettings s;
    if (!from_ui(&s, err, sizeof err)) { MessageBoxA(g_wnd, err, "Dark Emblem — Graphics Settings", MB_OK | MB_ICONWARNING); return false; }
    if (!write_settings(&s, err, sizeof err)) { MessageBoxA(g_wnd, err, "Dark Emblem — Graphics Settings", MB_OK | MB_ICONERROR); return false; }
    g_set = s; SetWindowTextA(item(ID_STATUS), "Saved. lantern reads these settings the next time the game starts.");
    return true;
}
static void play(void) {
    const char *what = file_exists("Play.exe") ? "Play.exe" : NULL;
    if (!what) { MessageBoxA(g_wnd, "Saved. No Play.exe found in this folder.", "Dark Emblem — Graphics Settings", MB_OK | MB_ICONINFORMATION); return; }
    if ((INT_PTR)ShellExecuteA(g_wnd, "open", what, NULL, g_dir, SW_SHOWNORMAL) <= 32) { MessageBoxA(g_wnd, "Saved, but the game would not start.", "Dark Emblem — Graphics Settings", MB_OK | MB_ICONERROR); return; }
    DestroyWindow(g_wnd);
}

static void build(void) {
    int y = 12; char t[512];
    if (!g_caps.have_vk) snprintf(t, sizeof t, "GPU: %s. lantern cannot run on this PC until a Vulkan 1.3 driver is installed.", g_caps.gpu);
    else if (!g_caps.vk13 || !g_caps.shader_object || !g_caps.eds3 || !g_caps.vertex_input)
        snprintf(t, sizeof t, "GPU: %s. lantern cannot run on this graphics driver; missing:%s%s%s%s. Install a newer driver.", g_caps.gpu,
                 g_caps.vk13 ? "" : " Vulkan 1.3", g_caps.shader_object ? "" : " VK_EXT_shader_object (native)",
                 g_caps.eds3 ? "" : " VK_EXT_extended_dynamic_state3", g_caps.vertex_input ? "" : " VK_EXT_vertex_input_dynamic_state");
    else snprintf(t, sizeof t, "GPU: %s. DLSS: %s. AMD FSR (3.1 / 4): %s.", g_caps.gpu,
                  g_caps.dlss_can_run ? "available" : g_caps.dlss_why,
                  g_caps.fsr_files ? "available" : "files missing (dlss\\amd_fidelityfx_loader_vk.dll + upscaler)");
    mk("STATIC", t, SS_LEFT, 12, y, 456, 46, 0); y += 52;

    mk("BUTTON", "Upscaler", BS_GROUPBOX, 12, y, 456, 118, 0);
    mk("BUTTON", "Auto (DLSS on NVIDIA, AMD FSR elsewhere)", BS_AUTORADIOBUTTON | WS_GROUP, 24, y + 20, 300, 20, ID_UP_AUTO);
    snprintf(t, sizeof t, "DLSS%s", g_caps.dlss_can_run ? "" : "  (not on this PC)");
    mk("BUTTON", t, BS_AUTORADIOBUTTON, 24, y + 42, 240, 20, ID_UP_DLSS);
    snprintf(t, sizeof t, "AMD FSR (3.1 / 4)%s", g_caps.fsr_files ? "" : "  (DLLs missing)");
    mk("BUTTON", t, BS_AUTORADIOBUTTON, 24, y + 64, 280, 20, ID_UP_FSR3);
    mk("BUTTON", "Off (plain copy)", BS_AUTORADIOBUTTON, 24, y + 86, 200, 20, ID_UP_OFF);
    EnableWindow(item(ID_UP_DLSS), g_caps.dlss_can_run);
    y += 126;
    mk("STATIC", "Quality:", SS_LEFT, 12, y + 4, 110, 20, 0);
    HWND q = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 130, y, 200, 200, ID_QUALITY);
    for (int i = 0; i < LN_SET_Q_COUNT; i++) SendMessageA(q, CB_ADDSTRING, 0, (LPARAM)k_qlabel[i]);
    y += 32;
    mk("STATIC", "Output resolution:", SS_LEFT, 12, y + 4, 115, 20, 0);
    HWND o = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 130, y, 200, 240, ID_OUTPUT);
    for (int i = 0; i < N_OUT; i++) {
        if (i == 0 && g_caps.mon_w) { snprintf(t, sizeof t, "Native (%u x %u)", g_caps.mon_w, g_caps.mon_h); SendMessageA(o, CB_ADDSTRING, 0, (LPARAM)t); }
        else SendMessageA(o, CB_ADDSTRING, 0, (LPARAM)k_out[i].label);
    }
    mk("EDIT", "", WS_BORDER | ES_NUMBER | WS_TABSTOP, 340, y, 56, 22, ID_OUT_W);
    mk("STATIC", "x", SS_CENTER, 396, y + 4, 14, 20, 0);
    mk("EDIT", "", WS_BORDER | ES_NUMBER | WS_TABSTOP, 410, y, 56, 22, ID_OUT_H);
    y += 32;
    mk("STATIC", "Window:", SS_LEFT, 12, y + 2, 110, 20, 0);
    mk("BUTTON", "Windowed", BS_AUTORADIOBUTTON | WS_GROUP, 130, y, 100, 20, ID_WIN_WINDOWED);
    mk("BUTTON", "Borderless (whole monitor)", BS_AUTORADIOBUTTON, 240, y, 200, 20, ID_WIN_BORDERLESS);
    y += 30;
    mk("BUTTON", "HDR brightness", BS_GROUPBOX, 12, y, 456, 124, 0);
    mk("STATIC", "Paper white (nits):", SS_LEFT, 24, y + 25, 170, 20, 0);
    HWND hw = mk("EDIT", "", WS_BORDER | ES_NUMBER | WS_TABSTOP, 200, y + 21, 80, 24, ID_HDR_WHITE);
    SendMessageA(hw, EM_SETLIMITTEXT, 5, 0);
    mk("BUTTON", "Auto (Windows)", BS_AUTOCHECKBOX | WS_TABSTOP, 298, y + 23, 150, 22, ID_HDR_WHITE_AUTO);
    mk("STATIC", "Peak brightness (nits):", SS_LEFT, 24, y + 55, 170, 20, 0);
    HWND hp = mk("EDIT", "", WS_BORDER | ES_NUMBER | WS_TABSTOP, 200, y + 51, 80, 24, ID_HDR_PEAK);
    SendMessageA(hp, EM_SETLIMITTEXT, 5, 0);
    mk("BUTTON", "Auto (Windows)", BS_AUTOCHECKBOX | WS_TABSTOP, 298, y + 53, 150, 22, ID_HDR_PEAK_AUTO);
    mk("STATIC", "Windows controls HDR on/off. Auto uses Windows values.\nSaved changes apply when the game restarts.", SS_LEFT, 24, y + 83, 432, 34, 0);
    y += 134;
    mk("STATIC", "FSR sharpness:", SS_LEFT, 12, y + 4, 115, 20, 0);
    HWND sh = mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 124, y, 220, 28, ID_SHARP);
    SendMessageA(sh, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    mk("STATIC", "0.00", SS_LEFT, 350, y + 4, 50, 20, ID_SHARP_VAL);
    y += 34;
    mk("STATIC", "DLSS preset (advanced):", SS_LEFT, 12, y + 4, 150, 20, 0);
    HWND pr = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 170, y, 160, 300, ID_PRESET);
    SendMessageA(pr, CB_ADDSTRING, 0, (LPARAM)"Game's choice");
    for (int i = 0; i < 15; i++) { snprintf(t, sizeof t, "Preset %c", 'A' + i); SendMessageA(pr, CB_ADDSTRING, 0, (LPARAM)t); }
    y += 36;
    mk("STATIC", "Frame generation:", SS_LEFT, 12, y + 4, 150, 20, 0);

    HWND fgc = mk("COMBOBOX", "", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, 170, y, 290, 120, ID_FRAMEGEN);
    SendMessageA(fgc, CB_ADDSTRING, 0, (LPARAM)"Off");
    SendMessageA(fgc, CB_ADDSTRING, 0, (LPARAM)(g_caps.dlss_g_files ? "DLSS Frame Generation (with DLSS)" : "DLSS Frame Generation (no dlss\\sl.dlss_g.dll)"));
    SendMessageA(fgc, CB_ADDSTRING, 0, (LPARAM)(g_caps.ffx_fg_files ? "FSR Frame Generation (with FSR or DLSS)" : "FSR Frame Generation (no FSR frame generation DLL)"));
    y += 32;
    mk("BUTTON", "NVIDIA Reflex low latency (with DLSS; always on with DLSS Frame Generation)", BS_AUTOCHECKBOX | WS_TABSTOP, 12, y, 456, 22, ID_REFLEX);
    y += 32;
    mk("STATIC", "FPS cap:", SS_LEFT, 12, y + 4, 150, 20, 0);
    HWND fpc = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 170, y, 240, 240, ID_FPS);
    for (int i = 0; i < N_FPS; i++) {
        if (!k_fps[i]) SendMessageA(fpc, CB_ADDSTRING, 0, (LPARAM)"Uncapped");
        else { snprintf(t, sizeof t, "%u fps", k_fps[i]); SendMessageA(fpc, CB_ADDSTRING, 0, (LPARAM)t); }
    }
    y += 36;
    mk("STATIC", "Texture quality:", SS_LEFT, 12, y + 4, 150, 20, 0);
    HWND tcb = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 170, y, 240, 200, ID_TEXCOMP);
    for (int i = 0; i < LN_SET_TEX_COUNT; i++) SendMessageA(tcb, CB_ADDSTRING, 0, (LPARAM)k_tex_label[i]);
    y += 30;
    mk("STATIC", "", SS_LEFT, 12, y, 456, 50, ID_TEXDESC); y += 56;
    mk("STATIC", "Texture detail:", SS_LEFT, 12, y + 4, 150, 20, 0);
    HWND dcb = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 170, y, 240, 200, ID_TEXDETAIL);
    for (int i = 0; i < LN_SET_DETAIL_COUNT; i++) SendMessageA(dcb, CB_ADDSTRING, 0, (LPARAM)k_det_label[i]);
    y += 30;
    mk("STATIC", "", SS_LEFT, 12, y, 456, 50, ID_DETDESC); y += 52;
    mk("STATIC", "", SS_LEFT, 12, y, 456, 34, ID_DETWARN); y += 40;
    mk("STATIC", "Shader loading:", SS_LEFT, 12, y + 4, 150, 20, 0);
    HWND scb = mk("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 170, y, 240, 200, ID_SHADERLOAD);
    for (int i = 0; i < LN_SET_SHADER_COUNT; i++) SendMessageA(scb, CB_ADDSTRING, 0, (LPARAM)k_shd_label[i]);
    y += 30;
    mk("STATIC", "", SS_LEFT, 12, y, 456, 50, ID_SHDDESC); y += 56;
    mk("STATIC", "", SS_LEFT, 12, y, 456, 36, ID_RESOLVED); y += 38;
    mk("STATIC", "", SS_LEFT, 12, y, 456, 20, ID_STATUS); y += 28;
    mk("BUTTON", "Save", BS_DEFPUSHBUTTON | WS_TABSTOP, 172, y, 90, 28, ID_SAVE);
    mk("BUTTON", "Save and play", BS_PUSHBUTTON | WS_TABSTOP, 270, y, 110, 28, ID_PLAY);
    mk("BUTTON", "Close", BS_PUSHBUTTON | WS_TABSTOP, 388, y, 80, 28, ID_CLOSE);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        const int id = LOWORD(w), code = HIWORD(w);
        if (id == ID_SAVE) save();
        else if (id == ID_PLAY) { if (save()) play(); }
        else if (id == ID_CLOSE) DestroyWindow(h);
        else if ((id >= ID_UP_AUTO && id <= ID_UP_OFF) || (id == ID_OUTPUT && code == CBN_SELCHANGE) || (id == ID_TEXCOMP && code == CBN_SELCHANGE) ||
                 (id == ID_TEXDETAIL && code == CBN_SELCHANGE) || (id == ID_SHADERLOAD && code == CBN_SELCHANGE)) { refresh(); SetWindowTextA(item(ID_STATUS), ""); }
        else if (id == ID_FRAMEGEN && code == CBN_SELCHANGE) {
            const int sel = (int)SendMessageA(item(ID_FRAMEGEN), CB_GETCURSEL, 0, 0);
            if (fg_item_ok(sel, ui_resolved())) g_fg_sel = sel;
            else SendMessageA(item(ID_FRAMEGEN), CB_SETCURSEL, (WPARAM)g_fg_sel, 0);
            refresh(); SetWindowTextA(item(ID_STATUS), ""); }
        else if (id == ID_REFLEX && code == BN_CLICKED) {
            g_reflex_user = SendMessageA(item(ID_REFLEX), BM_GETCHECK, 0, 0) == BST_CHECKED; refresh(); SetWindowTextA(item(ID_STATUS), ""); }
        else if (code == CBN_SELCHANGE || code == BN_CLICKED || code == EN_CHANGE) SetWindowTextA(item(ID_STATUS), "");
        if (id == ID_WIN_WINDOWED || id == ID_WIN_BORDERLESS || id == ID_HDR_WHITE_AUTO || id == ID_HDR_PEAK_AUTO) refresh();
        return 0; }
    case WM_HSCROLL: refresh(); SetWindowTextA(item(ID_STATUS), ""); return 0;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l; if (mi->CtlID != ID_FRAMEGEN) break;
        HDC dc = GetDC(h); HGDIOBJ old = SelectObject(dc, g_font); TEXTMETRICA tm; GetTextMetricsA(dc, &tm); SelectObject(dc, old); ReleaseDC(h, dc);
        mi->itemHeight = (UINT)tm.tmHeight + 4; return TRUE; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)l; if (di->CtlID != ID_FRAMEGEN) break;
        if (di->itemID == (UINT)-1) return TRUE;
        const bool ok = fg_item_ok((int)di->itemID, ui_resolved()) && !(di->itemState & ODS_DISABLED);
        const bool hi = ok && (di->itemState & ODS_SELECTED) && !(di->itemState & ODS_COMBOBOXEDIT && GetFocus() != di->hwndItem);
        FillRect(di->hDC, &di->rcItem, GetSysColorBrush(hi ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        char text[128] = ""; SendMessageA(di->hwndItem, CB_GETLBTEXT, di->itemID, (LPARAM)text);
        SetBkMode(di->hDC, TRANSPARENT); SetTextColor(di->hDC, GetSysColor(!ok ? COLOR_GRAYTEXT : hi ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        HGDIOBJ old = SelectObject(di->hDC, g_font); RECT rc = di->rcItem; rc.left += 4;
        DrawTextA(di->hDC, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX); SelectObject(di->hDC, old);
        if (di->itemState & ODS_FOCUS) DrawFocusRect(di->hDC, &di->rcItem);
        return TRUE; }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        SetBkMode(dc, TRANSPARENT);
        if ((HWND)l == item(ID_DETWARN)) SetTextColor(dc, RGB(180, 55, 0));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static bool settings_ui_path(char *out, size_t cap, char *err, size_t errcap) {
    if (!out || !cap || cap > MAXDWORD) { snprintf(err, errcap, "Settings path buffer is invalid."); return false; }
    SetLastError(ERROR_SUCCESS);
    DWORD n = GetEnvironmentVariableA("LN_SETTINGS_FILE", out, (DWORD)cap);
    if (n) {
        if (n >= cap) { snprintf(err, errcap, "LN_SETTINGS_FILE is too long; no settings file was opened."); return false; }
        if (out[n - 1] == '\\' || out[n - 1] == '/') { snprintf(err, errcap, "LN_SETTINGS_FILE must name a file, not a folder."); return false; }
        return true;
    }
    DWORD error = GetLastError();
    if (error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND) { snprintf(err, errcap, "Cannot read LN_SETTINGS_FILE (error %lu).", error); return false; }
    int written = snprintf(out, cap, "%s%s", g_dir, LN_SETTINGS_FILE);
    if (written < 0 || (size_t)written >= cap) { snprintf(err, errcap, "Default settings path is too long."); return false; }
    return true;
}


static void resolve_release_dlss_dir(void) {
    static const char *const k_rel[] = { "dlss\\", "bin\\dlss\\" };
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < sizeof k_rel / sizeof k_rel[0]; i++) {
            char cand[MAX_PATH * 2];
            snprintf(cand, sizeof cand, "%s%s", g_dir, k_rel[i]);
            if (pass == 0) {
                char p1[MAX_PATH * 2], p2[MAX_PATH * 2];
                snprintf(p1, sizeof p1, "%snvngx_dlss.dll", cand);
                snprintf(p2, sizeof p2, "%samd_fidelityfx_loader_vk.dll", cand);
                if (path_is_file(p1) || path_is_file(p2)) {
                    snprintf(g_dlss_dir, sizeof g_dlss_dir, "%s", cand);
                    return;
                }
            } else if (path_is_dir(cand)) {
                snprintf(g_dlss_dir, sizeof g_dlss_dir, "%s", cand);
                return;
            }
        }
    }
    snprintf(g_dlss_dir, sizeof g_dlss_dir, "%sdlss\\", g_dir);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    DWORD n = GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    char *slash = (n && n < sizeof g_dir) ? strrchr(g_dir, '\\') : NULL;
    if (!slash) {
        MessageBoxA(NULL, "Cannot find installation folder.", "Dark Emblem — Graphics Settings", MB_OK | MB_ICONERROR);
        return 1;
    }
    slash[1] = 0;

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
        MessageBoxA(NULL, path_error, "Dark Emblem — Graphics Settings", MB_OK | MB_ICONERROR);
        return 1;
    }
    snprintf(g_requested_path, sizeof g_requested_path, "%s", g_path);

    resolve_release_dlss_dir();
    probe(&g_caps);
    if (!g_caps.dlss_files && !g_caps.fsr_files) {
        resolve_release_dlss_dir();
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
        .lpfnWndProc = wndproc,
        .hInstance = hi,
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = GetSysColorBrush(COLOR_BTNFACE),
        .lpszClassName = "dark_emblem_settings_wnd",
        .hIcon = hIco ? hIco : LoadIcon(NULL, IDI_APPLICATION),
        .hIconSm = hIco ? hIco : LoadIcon(NULL, IDI_APPLICATION)
    };
    RegisterClassExA(&wc);
    RECT rc = { 0, 0, 480, 480 + 38 + 134 + 36 + 36 + 28 + 86 + 86 + 86 + 36 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_wnd = CreateWindowExA(0, wc.lpszClassName, "Dark Emblem — Graphics Settings",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                            NULL, NULL, hi, NULL);
    if (!g_wnd) return 1;
    build();
    to_ui(&g_set);
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
