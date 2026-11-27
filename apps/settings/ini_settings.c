// Dark Emblem — Graphics & Upscaler INI Settings Parser/Writer
#define _CRT_SECURE_NO_WARNINGS
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#include "ini_settings.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static const char *const k_window[] = {"windowed", "borderless"};
static const char *const k_upscaler[] = {"auto", "dlss", "fsr3", "off"};
static const char *const k_framegen[] = {"off", "dlss", "fsr"};
static const char *const k_texcomp[LN_SET_TEX_COUNT] = {"auto", "full", "high", "medium", "low"};
static const char *const k_texdetail[LN_SET_DETAIL_COUNT] = {"auto", "high", "medium", "low", "lowest"};
static const char *const k_shaderload[LN_SET_SHADER_COUNT] = {"auto", "at_start", "when_needed"};
static const char *const k_quality[LN_SET_Q_COUNT] = {"game", "dlaa", "maxquality", "balanced", "maxperf", "ultraperf"};
static const char *const k_fsr3[LN_SET_Q_COUNT] = {NULL, "NativeAA", "Quality", "Balanced", "Performance", "UltraPerformance"};
static const float k_fsr3_ratio[LN_SET_Q_COUNT] = {0.f, 1.0f, 1.5f, 1.7f, 2.0f, 3.0f};

const char *ln_settings_window_name(LnSetWindow w) { return (unsigned)w < 2 ? k_window[w] : "?"; }
const char *ln_settings_upscaler_name(LnSetUpscaler u) { return (unsigned)u < 4 ? k_upscaler[u] : "?"; }
const char *ln_settings_framegen_name(LnSetFrameGen f) { return (unsigned)f < 3 ? k_framegen[f] : "?"; }
const char *ln_settings_tex_compression_name(LnSetTexCompression t) { return (unsigned)t < LN_SET_TEX_COUNT ? k_texcomp[t] : "?"; }
const char *ln_settings_shader_load_name(LnSetShaderLoad l) { return (unsigned)l < LN_SET_SHADER_COUNT ? k_shaderload[l] : "?"; }
const char *ln_settings_tex_detail_name(LnSetTexDetail t) { return (unsigned)t < LN_SET_DETAIL_COUNT ? k_texdetail[t] : "?"; }
uint32_t ln_settings_tex_detail_drop(LnSetTexDetail t, uint64_t vram_bytes, LnSetTexDetail *resolved) {
    if ((unsigned)t >= LN_SET_DETAIL_COUNT) t = LN_SET_DETAIL_AUTO;
    if (t == LN_SET_DETAIL_AUTO) t = vram_bytes >= (20ull << 30) ? LN_SET_DETAIL_HIGH : vram_bytes >= (14ull << 30) ? LN_SET_DETAIL_MEDIUM
                                  : vram_bytes >= (10ull << 30) ? LN_SET_DETAIL_LOW : LN_SET_DETAIL_LOWEST;
    if (resolved) *resolved = t;
    return t == LN_SET_DETAIL_MEDIUM ? 1u : t == LN_SET_DETAIL_LOW ? 2u : 0u;
}
uint32_t ln_settings_tex_detail_cap(LnSetTexDetail resolved) { return resolved == LN_SET_DETAIL_LOWEST ? 512u : 0u; }
uint64_t ln_settings_tex_cache_cap_bytes(LnSetTexDetail resolved) {
    return resolved == LN_SET_DETAIL_MEDIUM ? 3000000000ull : resolved == LN_SET_DETAIL_LOW ? 1500000000ull
         : resolved == LN_SET_DETAIL_LOWEST ? 1000000000ull : 0ull;
}
uint32_t ln_settings_tex_wblock_x100(LnSetTexCompression t, uint32_t cache_x100) {
    switch (t) {
    case LN_SET_TEX_HIGH: return cache_x100;
    case LN_SET_TEX_MEDIUM: return cache_x100 > 1600u ? cache_x100 : 1600u;
    case LN_SET_TEX_LOW: return UINT32_MAX;
    default: return 0;
    }
}
LnSetTexCompression ln_settings_resolve_tex_compression(LnSetTexCompression t, uint64_t vram_bytes) {
    if ((unsigned)t >= LN_SET_TEX_COUNT) t = LN_SET_TEX_AUTO;
    if (t != LN_SET_TEX_AUTO) return t;
    if (vram_bytes >= (16ull << 30)) return LN_SET_TEX_HIGH;
    if (vram_bytes >= (10ull << 30)) return LN_SET_TEX_MEDIUM;
    return LN_SET_TEX_LOW;
}
LnSetShaderLoad ln_settings_resolve_shader_load(LnSetShaderLoad want, uint64_t host_ram_mib, uint64_t vram_mib) {
    if ((unsigned)want >= LN_SET_SHADER_COUNT) want = LN_SET_SHADER_AUTO;
    if (want != LN_SET_SHADER_AUTO) return want;
    if (host_ram_mib <= 20480u || vram_mib <= 14336u) return LN_SET_SHADER_WHEN_NEEDED;
    return LN_SET_SHADER_AT_START;
}
bool ln_settings_apply_vram_output_cap(LnUserSettings *s, uint64_t vram_mib) {
    if (!s || s->output != LN_SET_OUT_NATIVE) return false;
    if (!vram_mib || vram_mib > 8192u) return false;
    if (s->out_w <= 1920u && s->out_h <= 1080u) return false;
    s->out_w = 1920u;
    s->out_h = 1080u;
    return true;
}
static uint64_t ln_sim_gb_env(const char *name, uint64_t *logged_val) {
    const char *e = getenv(name);
    if (!e || !*e) return 0;
    char *end = NULL;
    unsigned long long gb = strtoull(e, &end, 10);
    if (end == e || *end || gb == 0 || gb > 4096ull) return 0;
    if (logged_val && *logged_val != (uint64_t)gb) {
        *logged_val = (uint64_t)gb;
        fprintf(stderr, "lantern: hardware override ACTIVE: %s=%llu GiB (%llu MiB)\n",
                name, gb, gb * 1024ull);
    }
    return (uint64_t)gb;
}
uint64_t ln_host_ram_mib(void) {
    static uint64_t logged_ram_gb = 0;
    uint64_t sim_gb = ln_sim_gb_env("LN_SIMULATE_RAM_GB", &logged_ram_gb);
    if (sim_gb) return sim_gb * 1024ull;
#ifdef _WIN32
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof ms);
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) return (uint64_t)(ms.ullTotalPhys >> 20);
    return 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES), psz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && psz > 0) return ((uint64_t)pages * (uint64_t)psz) >> 20;
    return 0;
#endif
}
uint64_t ln_vram_override_bytes(uint64_t real_vram_bytes) {
    static uint64_t logged_vram_gb = 0;
    uint64_t sim_gb = ln_sim_gb_env("LN_SIMULATE_VRAM_GB", &logged_vram_gb);
    if (sim_gb) return sim_gb << 30;
    return real_vram_bytes;
}
uint64_t ln_probe_vram_bytes(void) {
    uint64_t sim = ln_vram_override_bytes(0);
    if (sim) return sim;
#ifdef _WIN32
    typedef struct LnDxgiAdapter1 LnDxgiAdapter1;
    typedef struct LnDxgiFactory1 LnDxgiFactory1;
    typedef struct {
        WCHAR Description[128];
        UINT VendorId, DeviceId, SubSysId, Revision;
        SIZE_T DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
        LUID AdapterLuid;
        UINT Flags;
    } LnDxgiAdapterDesc1;
    typedef struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(LnDxgiAdapter1 *, const GUID *, void **);
        ULONG   (STDMETHODCALLTYPE *AddRef)(LnDxgiAdapter1 *);
        ULONG   (STDMETHODCALLTYPE *Release)(LnDxgiAdapter1 *);
        void *pad[7];
        HRESULT (STDMETHODCALLTYPE *GetDesc1)(LnDxgiAdapter1 *, LnDxgiAdapterDesc1 *);
    } LnDxgiAdapter1Vtbl;
    struct LnDxgiAdapter1 { const LnDxgiAdapter1Vtbl *lpVtbl; };
    typedef struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(LnDxgiFactory1 *, const GUID *, void **);
        ULONG   (STDMETHODCALLTYPE *AddRef)(LnDxgiFactory1 *);
        ULONG   (STDMETHODCALLTYPE *Release)(LnDxgiFactory1 *);
        void *pad[9];
        HRESULT (STDMETHODCALLTYPE *EnumAdapters1)(LnDxgiFactory1 *, UINT, LnDxgiAdapter1 **);
    } LnDxgiFactory1Vtbl;
    struct LnDxgiFactory1 { const LnDxgiFactory1Vtbl *lpVtbl; };
    typedef HRESULT (WINAPI *PFN_CreateDXGIFactory1)(const GUID *, void **);
    static const GUID k_iid_factory1 = {0x770aae78u, 0xf26fu, 0x4dbau, {0xa8u, 0x29u, 0x25u, 0x3cu, 0x83u, 0xd1u, 0xb3u, 0x87u}};
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) return 0;
    PFN_CreateDXGIFactory1 fn = (PFN_CreateDXGIFactory1)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    uint64_t best = 0;
    LnDxgiFactory1 *fac = NULL;
    if (fn && SUCCEEDED(fn(&k_iid_factory1, (void **)&fac)) && fac) {
        for (UINT i = 0; ; i++) {
            LnDxgiAdapter1 *ad = NULL;
            if (FAILED(fac->lpVtbl->EnumAdapters1(fac, i, &ad)) || !ad) break;
            LnDxgiAdapterDesc1 desc;
            memset(&desc, 0, sizeof desc);
            if (SUCCEEDED(ad->lpVtbl->GetDesc1(ad, &desc)) && !(desc.Flags & 2u)) {
                if ((uint64_t)desc.DedicatedVideoMemory > best) best = (uint64_t)desc.DedicatedVideoMemory;
            }
            ad->lpVtbl->Release(ad);
        }
        fac->lpVtbl->Release(fac);
    }
    FreeLibrary(dxgi);
    return best;
#else
    return 0;
#endif
}
const char *ln_settings_quality_name(LnSetQuality q) { return (unsigned)q < LN_SET_Q_COUNT ? k_quality[q] : "?"; }
const char *ln_settings_quality_fsr3(LnSetQuality q) { return (unsigned)q < LN_SET_Q_COUNT ? k_fsr3[q] : NULL; }
float ln_settings_fsr3_ratio(LnSetQuality q) { return (unsigned)q < LN_SET_Q_COUNT ? k_fsr3_ratio[q] : 0.f; }
int ln_settings_display_tier(const LnUserSettings *s, const char **why) {
    static const uint32_t k_tier[4][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
    const char *w = NULL; int t = -1;
    if (!s || s->output == LN_SET_OUT_GAME) w = "output = game: the game's own display tier (hzrt default 3, 3840x2160)";
    else if (!s->out_w || !s->out_h) w = "output size unknown here: the game's own display tier";
    else {
        for (t = 0; t < 4 && (s->out_w > k_tier[t][0] || s->out_h > k_tier[t][1]); t++) { }
        if (t == 4) { t = 3; w = "larger than the game's largest tier: 3 (3840x2160, measured: boots 13/33); the upscaler output stays the game's"; }
        else w = t == 3 ? "tier 3 (3840x2160, measured: boots 13/33)"
                        : "UNPROVEN: tiers 0..2 (1280x720, 1920x1080, 2560x1440) are hzrt's uncited reading; only tier 3 is measured";
    }
    if (why) *why = w;
    return t;
}

void ln_settings_defaults(LnUserSettings *s) {
    memset(s, 0, sizeof *s);
    s->window = LN_SET_WINDOWED; s->output = LN_SET_OUT_NATIVE; s->upscaler = LN_SET_UP_AUTO; s->quality = LN_SET_Q_GAME;
    s->preset = 0; s->sharpness = 0.0f;
    s->reflex = true;
    s->tex_compression = LN_SET_TEX_AUTO;
    s->tex_detail = LN_SET_DETAIL_AUTO;
    s->shader_load = LN_SET_SHADER_AUTO;
}

bool ln_settings_parse_hdr_nits(const char *text, uint16_t *out) {
    if (!text || !out) return false;
    if (!strcmp(text, "auto")) { *out = 0; return true; }
    if (!*text) return false;
    uint32_t n = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        uint32_t digit = (uint32_t)(*p - '0');
        if (n > (UINT16_MAX - digit) / 10u) return false;
        n = n * 10u + digit;
    }
    if (!n) return false;
    *out = (uint16_t)n; return true;
}

static void logf_(char *log, size_t cap, uint32_t *n, const char *fmt, ...) {
    (*n)++;
    if (!log || !cap) return;
    size_t used = strlen(log); if (used + 1 >= cap) return;
    va_list ap; va_start(ap, fmt); vsnprintf(log + used, cap - used, fmt, ap); va_end(ap);
    used = strlen(log); if (used + 1 < cap) { log[used] = '\n'; log[used + 1] = 0; }
}
static int pick(const char *v, const char *const *names, int n) {
    for (int i = 0; i < n; i++) if (names[i] && !strcmp(v, names[i])) return i;
    return -1;
}

uint32_t ln_settings_parse(const char *text, LnUserSettings *s, char *log, size_t log_cap) {
    uint32_t bad = 0, seen = 0; int line = 0; char sect[16] = "";
    if (log && log_cap) log[0] = 0;
    for (const char *p = text ? text : ""; *p; ) {
        const char *e = strchr(p, '\n'); size_t len = e ? (size_t)(e - p) : strlen(p);
        char b[256]; line++;
        if (len >= sizeof b) { logf_(log, log_cap, &bad, "line %d: longer than %zu characters, ignored", line, sizeof b - 1); goto next; }
        memcpy(b, p, len); b[len] = 0;
        { char *c = strpbrk(b, "#;"); if (c) *c = 0; }
        char *t = b; while (isspace((unsigned char)*t)) t++;
        for (size_t k = strlen(t); k && isspace((unsigned char)t[k - 1]); ) t[--k] = 0;
        for (char *q = t; *q; q++) *q = (char)tolower((unsigned char)*q);
        if (!*t) goto next;
        if (*t == '[') {
            char *r = strchr(t, ']');
            if (!r || r[1]) { logf_(log, log_cap, &bad, "line %d: bad section header", line); sect[0] = 0; goto next; }
            *r = 0;
            if (strcmp(t + 1, "display") && strcmp(t + 1, "upscaler") && strcmp(t + 1, "hdr") && strcmp(t + 1, "framegen") && strcmp(t + 1, "textures") && strcmp(t + 1, "shaders")) { logf_(log, log_cap, &bad, "line %d: unknown section [%s], its keys are ignored", line, t + 1); snprintf(sect, sizeof sect, "?"); }
            else snprintf(sect, sizeof sect, "%s", t + 1);
            goto next;
        }
        {
            char *eq = strchr(t, '=');
            if (!eq) { logf_(log, log_cap, &bad, "line %d: not key = value, ignored", line); goto next; }
            *eq = 0; char *k = t, *v = eq + 1;
            for (size_t n = strlen(k); n && isspace((unsigned char)k[n - 1]); ) k[--n] = 0;
            while (isspace((unsigned char)*v)) v++;
            if (!strcmp(sect, "?")) goto next;
            const bool disp = !strcmp(sect, "display"), up = !strcmp(sect, "upscaler"), hdr = !strcmp(sect, "hdr"), fg = !strcmp(sect, "framegen"), tex = !strcmp(sect, "textures"), shd = !strcmp(sect, "shaders");
            int bit = -1, i;
            if (disp && !strcmp(k, "window")) {
                bit = 0; if ((i = pick(v, k_window, 2)) < 0) goto badval; s->window = (LnSetWindow)i;
            } else if (disp && !strcmp(k, "output")) {
                bit = 1;
                if (!strcmp(v, "native")) s->output = LN_SET_OUT_NATIVE;
                else if (!strcmp(v, "game")) s->output = LN_SET_OUT_GAME;
                else {
                    unsigned w, h; char x;
                    if (sscanf(v, "%u%c%u", &w, &x, &h) != 3 || x != 'x' || w < LN_SET_MIN_SIZE || h < LN_SET_MIN_SIZE || w > LN_SET_MAX_SIZE || h > LN_SET_MAX_SIZE) goto badval;
                    { char chk[32]; snprintf(chk, sizeof chk, "%ux%u", w, h); if (strcmp(chk, v)) goto badval; }
                    s->output = LN_SET_OUT_SIZE; s->out_w = w; s->out_h = h;
                }
            } else if (disp && !strcmp(k, "fps_limit")) {
                bit = 9;
                if (!strcmp(v, "uncapped")) s->fps_limit = 0;
                else {
                    char *end; unsigned long f = strtoul(v, &end, 10);
                    if (end == v || *end || *v < '0' || *v > '9' || f < 1 || f > LN_SET_MAX_FPS) goto badval;
                    s->fps_limit = (uint16_t)f;
                }
            } else if (up && !strcmp(k, "upscaler")) {
                bit = 2; if ((i = pick(v, k_upscaler, 4)) < 0) goto badval; s->upscaler = (LnSetUpscaler)i;
            } else if (up && !strcmp(k, "quality")) {
                bit = 3;
                if ((i = pick(v, k_quality, LN_SET_Q_COUNT)) < 0) {
                    if (!strcmp(v, "quality")) i = LN_SET_Q_MAXQUALITY; else if (!strcmp(v, "performance")) i = LN_SET_Q_MAXPERF; else goto badval;
                }
                s->quality = (LnSetQuality)i;
            } else if (up && !strcmp(k, "preset")) {
                bit = 4;
                if (!strcmp(v, "game")) s->preset = 0;
                else if (strlen(v) == 1 && v[0] >= 'a' && v[0] <= 'o') s->preset = (uint8_t)(v[0] - 'a' + 1);
                else goto badval;
            } else if (up && !strcmp(k, "sharpness")) {
                bit = 5; char *end; double d = strtod(v, &end);
                if (end == v || *end || !(d >= 0.0 && d <= 1.0)) goto badval;
                s->sharpness = (float)d;
            } else if (hdr && !strcmp(k, "peak_nits")) {
                bit = 6; if (!ln_settings_parse_hdr_nits(v, &s->hdr_peak_nits)) goto badval;
            } else if (hdr && !strcmp(k, "paper_white_nits")) {
                bit = 7; if (!ln_settings_parse_hdr_nits(v, &s->hdr_paper_white_nits)) goto badval;
            } else if (fg && !strcmp(k, "frame_generation")) {
                bit = 8; if ((i = pick(v, k_framegen, 3)) < 0) goto badval; s->framegen = (LnSetFrameGen)i;
            } else if (fg && !strcmp(k, "reflex")) {
                bit = 10;
                if (!strcmp(v, "on")) s->reflex = true; else if (!strcmp(v, "off")) s->reflex = false; else goto badval;
            } else if (tex && !strcmp(k, "quality")) {
                bit = 11; if ((i = pick(v, k_texcomp, LN_SET_TEX_COUNT)) < 0) goto badval; s->tex_compression = (LnSetTexCompression)i;
            } else if (tex && !strcmp(k, "detail")) {
                bit = 12; if ((i = pick(v, k_texdetail, LN_SET_DETAIL_COUNT)) < 0) goto badval; s->tex_detail = (LnSetTexDetail)i;
            } else if (shd && !strcmp(k, "loading")) {
                bit = 13; if ((i = pick(v, k_shaderload, LN_SET_SHADER_COUNT)) < 0) goto badval; s->shader_load = (LnSetShaderLoad)i;
            } else {
                logf_(log, log_cap, &bad, "line %d: unknown key '%s'%s%s, ignored", line, k, sect[0] ? " in [" : " outside any section", sect[0] ? sect : "");
                if (sect[0]) { size_t u = log ? strlen(log) : 0; if (log && u && u + 1 < log_cap) { log[u - 1] = ']'; log[u] = '\n'; log[u + 1] = 0; } }
                goto next;
            }
            if (seen >> bit & 1) logf_(log, log_cap, &bad, "line %d: '%s' set again, the later value wins", line, k);
            seen |= 1u << bit;
            goto next;
        badval:
            logf_(log, log_cap, &bad, "line %d: bad value for '%s': '%s', the default is kept", line, k, v);
        }
    next:
        if (!e) break;
        p = e + 1;
    }
    return bad;
}

size_t ln_settings_write(const LnUserSettings *s, char *out, size_t cap) {
    char outv[32], pre[8], peak[8], white[8], fps[12];
    if (s->fps_limit) snprintf(fps, sizeof fps, "%u", s->fps_limit); else strcpy(fps, "uncapped");
    if (s->hdr_peak_nits) snprintf(peak, sizeof peak, "%u", s->hdr_peak_nits); else strcpy(peak, "auto");
    if (s->hdr_paper_white_nits) snprintf(white, sizeof white, "%u", s->hdr_paper_white_nits); else strcpy(white, "auto");
    if (s->output == LN_SET_OUT_SIZE) snprintf(outv, sizeof outv, "%ux%u", s->out_w, s->out_h);
    else snprintf(outv, sizeof outv, "%s", s->output == LN_SET_OUT_GAME ? "game" : "native");
    if (s->preset >= 1 && s->preset <= 15) snprintf(pre, sizeof pre, "%c", 'A' + s->preset - 1); else snprintf(pre, sizeof pre, "game");
    int n = snprintf(out, cap,
        "# lantern settings. Written by the lantern settings app; you may edit it by hand.\n"
        "# lantern reads this file once at start-up. Environment variables (LN_*) override it.\n"
        "[display]\n"
        "# windowed | borderless (a window covering the whole monitor; opt-in, see the note in the app)\n"
        "window = %s\n"
        "# native (the monitor's size) | game (the game's own size) | WxH, e.g. 3840x2160\n"
        "output = %s\n"
        "# uncapped | most frames a second shown, e.g. 30, 60, 120, 144, 165, 240\n"
        "fps_limit = %s\n"
        "\n"
        "[upscaler]\n"
        "# auto (DLSS where it can run, FSR3 otherwise) | dlss (DLSS or nothing) | fsr3 | off\n"
        "upscaler = %s\n"
        "# game | dlaa | maxquality | balanced | maxperf | ultraperf   (FSR3: dlaa = NativeAA)\n"
        "quality = %s\n"
        "# DLSS only: game | A..O\n"
        "preset = %s\n"
        "# FSR3 only: 0.0 .. 1.0\n"
        "sharpness = %.2f\n"
        "\n[hdr]\n"
        "# Windows controls HDR on/off. Independent auto(Windows) or integer1..65535nits.\n"
        "# Changes take effect on the next game start; the existing game tone curve is used.\n"
        "peak_nits = %s\n"
        "paper_white_nits = %s\n"
        "\n[framegen]\n"
        "# off | dlss (DLSS Frame Generation, with the DLSS upscaler) | fsr (FSR Frame Generation, with FSR or DLSS upscaler)\n"
        "# Read once at start-up.\n"
        "frame_generation = %s\n"
        "# on | off: NVIDIA Reflex with DLSS. Always on with DLSS Frame Generation.\n"
        "reflex = %s\n"
        "\n[textures]\n"
        "# Texture quality (BC7 compression, with the texture cache beside the game). Read once at start-up.\n"
        "# auto: from video memory (>=16 GB high, >=10 GB medium, else low; default) | full: full size, most video memory\n"
        "# high: compress only textures that look identical | medium: more textures | low: nearly all, least video memory\n"
        "quality = %s\n"
        "# Texture detail (a PC port's setting for cards with less video memory). Read once at start-up.\n"
        "# auto: from the card's video memory (default) | high: every texture full size\n"
        "# medium: the largest mip level of decoded textures left out (about a quarter of their memory stays)\n"
        "# low: the two largest left out (about a sixteenth stays) | lowest: textures kept at most 512 x 512.\n"
        "# The game's texture cache budget follows it: high 5 GB (the game's own), medium 3, low 1.5, lowest 1.\n"
        "# The game's own data is never changed.\n"
        "detail = %s\n"
        "\n[shaders]\n"
        "# auto: at_start on systems with >20 GB RAM and >14 GB VRAM, when_needed otherwise (default) |\n"
        "# at_start: every game shader made at start-up (most video memory, no first-use cost) |\n"
        "# when_needed: each made from its compiled copy the first time the game uses it (less video memory, a first use\n"
        "# can take a few ms). Nothing is ever compiled while playing. Read once at start-up.\n"
        "loading = %s\n",
        ln_settings_window_name(s->window), outv, fps, ln_settings_upscaler_name(s->upscaler), ln_settings_quality_name(s->quality), pre, (double)s->sharpness, peak, white,
        ln_settings_framegen_name(s->framegen), s->reflex ? "on" : "off", ln_settings_tex_compression_name(s->tex_compression),
        ln_settings_tex_detail_name(s->tex_detail), ln_settings_shader_load_name(s->shader_load));
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

LnSetUpscaler ln_settings_resolve_upscaler(LnSetUpscaler want, bool dlss_can_run, const char **why) {
    if (why) *why = NULL;
    switch (want) {
    case LN_SET_UP_OFF: return LN_SET_UP_OFF;
    case LN_SET_UP_FSR3: return LN_SET_UP_FSR3;
    case LN_SET_UP_DLSS:
        if (dlss_can_run) return LN_SET_UP_DLSS;
        if (why) *why = "DLSS was chosen but cannot run here (needs an NVIDIA GPU and dlss/nvngx_dlss.dll): no upscaler, plain copy";
        return LN_SET_UP_OFF;
    default:
        return dlss_can_run ? LN_SET_UP_DLSS : LN_SET_UP_FSR3;
    }
}

bool ln_settings_fg_fits(LnSetFrameGen fg, LnSetUpscaler resolved) {
    return fg == LN_SET_FG_OFF ||
           (fg == LN_SET_FG_DLSS && resolved == LN_SET_UP_DLSS) ||
           (fg == LN_SET_FG_FSR && (resolved == LN_SET_UP_FSR3 || resolved == LN_SET_UP_DLSS));
}
bool ln_settings_reflex_on(const LnUserSettings *s, LnSetUpscaler resolved) {
    if (resolved != LN_SET_UP_DLSS) return false;
    return s->framegen == LN_SET_FG_DLSS || s->reflex;
}

static bool settings_path(char *out, size_t cap) {
    const char *e = getenv("LN_SETTINGS_FILE");
    if (e && *e) { snprintf(out, cap, "%s", e); return strlen(e) < cap; }
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);
    if (n == 0 || n >= cap) return false;
    char *slash = strrchr(out, '\\'); if (!slash) return false;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return false;
    out[n] = 0;
    char *slash = strrchr(out, '/'); if (!slash) return false;
#endif
    if ((size_t)(slash + 1 - out) + sizeof LN_SETTINGS_FILE > cap) return false;
    strcpy(slash + 1, LN_SETTINGS_FILE);
    return true;
}

static LnUserSettings g_ln_settings; static int g_ln_settings_state;
const LnUserSettings *ln_user_settings(void) {
    if (g_ln_settings_state) return &g_ln_settings;
    ln_settings_defaults(&g_ln_settings);
    char path[1024];
    char log[2048] = ""; uint32_t bad = 0;
    FILE *f = NULL;
    if (!settings_path(path, sizeof path)) { snprintf(path, sizeof path, "(unnamed)"); fprintf(stderr, "lantern: settings: cannot name the settings file, defaults used\n"); }
    else if (!(f = fopen(path, "rb"))) fprintf(stderr, "lantern: settings: no %s, defaults used\n", path);
    if (f) {
        char text[8192]; size_t n = fread(text, 1, sizeof text - 1, f); const bool big = !feof(f) && n == sizeof text - 1; fclose(f); text[n] = 0;
        if (big) fprintf(stderr, "lantern: settings: %s is longer than %zu bytes, the rest is ignored\n", path, sizeof text - 1);
        bad = ln_settings_parse(text, &g_ln_settings, log, sizeof log);
    }
    for (char *p = log, *e; *p; p = e + 1) { e = strchr(p, '\n'); if (!e) break; fprintf(stderr, "lantern: settings: %.*s\n", (int)(e - p), p); }
    LnUserSettings *s = &g_ln_settings;
    if (s->output == LN_SET_OUT_GAME) s->out_w = s->out_h = 0;
    else if (s->output == LN_SET_OUT_NATIVE) {
        s->out_w = s->out_h = 0;
#ifdef _WIN32
        DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth >= LN_SET_MIN_SIZE && dm.dmPelsHeight >= LN_SET_MIN_SIZE) { s->out_w = dm.dmPelsWidth; s->out_h = dm.dmPelsHeight; }
#endif
        if (!s->out_w) fprintf(stderr, "lantern: settings: output native: the monitor size is not known here, the game's own size is used\n");
        else {
            const uint32_t orig_w = s->out_w, orig_h = s->out_h;
            const uint64_t vram_mib = ln_probe_vram_bytes() >> 20;
            if (ln_settings_apply_vram_output_cap(s, vram_mib)) {
                fprintf(stderr, "lantern: settings: low VRAM (%llu MiB <= 8192 MiB): auto output native %ux%u capped to %ux%u (set explicit output = WxH in lantern-settings.ini to override)\n",
                        (unsigned long long)vram_mib, orig_w, orig_h, s->out_w, s->out_h);
            }
        }
    }
    fprintf(stderr, "lantern: settings: %s: window %s, output %s %ux%u (0x0 = the game's size), upscaler %s, quality %s, preset %c, sharpness %.2f, frame generation %s, reflex %s, fps limit %u (0 = uncapped), texture quality %s, texture detail %s, shader loading %s (%u problem%s)\n",
            path, ln_settings_window_name(s->window), s->output == LN_SET_OUT_SIZE ? "size" : s->output == LN_SET_OUT_GAME ? "game" : "native", s->out_w, s->out_h,
            ln_settings_upscaler_name(s->upscaler), ln_settings_quality_name(s->quality),
            s->preset ? 'A' + s->preset - 1 : '-', (double)s->sharpness, ln_settings_framegen_name(s->framegen), s->reflex ? "on" : "off", s->fps_limit, ln_settings_tex_compression_name(s->tex_compression), ln_settings_tex_detail_name(s->tex_detail), ln_settings_shader_load_name(s->shader_load), bad, bad == 1 ? "" : "s");
    g_ln_settings_state = 1;
    return &g_ln_settings;
}
