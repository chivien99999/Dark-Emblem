// Dark Emblem — Graphics & Upscaler INI Settings Header (lantern-settings.ini)
#ifndef LN_SETTINGS_H
#define LN_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { LN_SET_WINDOWED, LN_SET_BORDERLESS } LnSetWindow;
typedef enum { LN_SET_OUT_NATIVE, LN_SET_OUT_GAME, LN_SET_OUT_SIZE } LnSetOutput;
typedef enum { LN_SET_UP_AUTO, LN_SET_UP_DLSS, LN_SET_UP_FSR3, LN_SET_UP_OFF } LnSetUpscaler;
typedef enum { LN_SET_FG_OFF, LN_SET_FG_DLSS, LN_SET_FG_FSR } LnSetFrameGen;
typedef enum { LN_SET_TEX_AUTO, LN_SET_TEX_FULL, LN_SET_TEX_HIGH, LN_SET_TEX_MEDIUM, LN_SET_TEX_LOW, LN_SET_TEX_COUNT } LnSetTexCompression;
typedef enum { LN_SET_DETAIL_AUTO, LN_SET_DETAIL_HIGH, LN_SET_DETAIL_MEDIUM, LN_SET_DETAIL_LOW, LN_SET_DETAIL_LOWEST, LN_SET_DETAIL_COUNT } LnSetTexDetail;
typedef enum { LN_SET_SHADER_AUTO, LN_SET_SHADER_AT_START, LN_SET_SHADER_WHEN_NEEDED, LN_SET_SHADER_COUNT } LnSetShaderLoad;
#define LN_SHADERS_AUTO LN_SET_SHADER_AUTO
typedef enum { LN_SET_Q_GAME, LN_SET_Q_DLAA, LN_SET_Q_MAXQUALITY, LN_SET_Q_BALANCED, LN_SET_Q_MAXPERF, LN_SET_Q_ULTRAPERF, LN_SET_Q_COUNT } LnSetQuality;

typedef struct {
    LnSetWindow window;
    LnSetOutput output; uint32_t out_w, out_h;

    LnSetUpscaler upscaler;
    LnSetQuality quality;
    uint8_t preset;
    float sharpness;
    uint16_t hdr_peak_nits, hdr_paper_white_nits;
    LnSetFrameGen framegen;
    uint16_t fps_limit;
    bool reflex;
    LnSetTexCompression tex_compression;
    LnSetTexDetail tex_detail;
    LnSetShaderLoad shader_load;
} LnUserSettings;

#define LN_SETTINGS_FILE "lantern-settings.ini"
#define LN_SET_MIN_SIZE 320u
#define LN_SET_MAX_SIZE 16384u
#define LN_SET_MAX_FPS 1000u

void ln_settings_defaults(LnUserSettings *s);

bool ln_settings_parse_hdr_nits(const char *text, uint16_t *out);

uint32_t ln_settings_parse(const char *text, LnUserSettings *s, char *log, size_t log_cap);

size_t ln_settings_write(const LnUserSettings *s, char *out, size_t cap);

const char *ln_settings_window_name(LnSetWindow w);
const char *ln_settings_upscaler_name(LnSetUpscaler u);
const char *ln_settings_quality_name(LnSetQuality q);
const char *ln_settings_framegen_name(LnSetFrameGen f);
const char *ln_settings_tex_compression_name(LnSetTexCompression t);
const char *ln_settings_shader_load_name(LnSetShaderLoad l);

uint32_t ln_settings_tex_wblock_x100(LnSetTexCompression t, uint32_t cache_x100);

LnSetTexCompression ln_settings_resolve_tex_compression(LnSetTexCompression t, uint64_t vram_bytes);

LnSetShaderLoad ln_settings_resolve_shader_load(LnSetShaderLoad want, uint64_t host_ram_mib, uint64_t vram_mib);

bool ln_settings_apply_vram_output_cap(LnUserSettings *s, uint64_t vram_mib);

uint64_t ln_host_ram_mib(void);
uint64_t ln_vram_override_bytes(uint64_t real_vram_bytes);
uint64_t ln_probe_vram_bytes(void);
const char *ln_settings_tex_detail_name(LnSetTexDetail t);

uint32_t ln_settings_tex_detail_drop(LnSetTexDetail t, uint64_t vram_bytes, LnSetTexDetail *resolved);

uint32_t ln_settings_tex_detail_cap(LnSetTexDetail resolved);

uint64_t ln_settings_tex_cache_cap_bytes(LnSetTexDetail resolved);

const char *ln_settings_quality_fsr3(LnSetQuality q);

float ln_settings_fsr3_ratio(LnSetQuality q);

LnSetUpscaler ln_settings_resolve_upscaler(LnSetUpscaler want, bool dlss_can_run, const char **why);

bool ln_settings_fg_fits(LnSetFrameGen fg, LnSetUpscaler resolved);

bool ln_settings_reflex_on(const LnUserSettings *s, LnSetUpscaler resolved);

int ln_settings_display_tier(const LnUserSettings *s, const char **why);

const LnUserSettings *ln_user_settings(void);
#endif
