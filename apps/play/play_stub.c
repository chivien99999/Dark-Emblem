// Play.exe — Minimal Win32 launch stub for Dark Emblem.
// Linked with /HIGHENTROPYVA:NO /SUBSYSTEM:WINDOWS and custom play.ico.
// Loads bin\engine.dll immediately (before any heap growth) and invokes its entry point.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

// Prefer discrete GPU on NVIDIA Optimus / AMD PowerXpress hybrid-graphics laptops.
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

typedef int (*de_engine_run_fn)(const void *reserved, uint32_t flags, const wchar_t *install_root);
#define DE_ENGINE_RUN_SYM "de_70acf276"

static void show_err(const wchar_t *msg) {
    MessageBoxW(NULL, msg, L"Dark Emblem", MB_OK | MB_ICONERROR);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmdLine, int nShowCmd) {
    (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nShowCmd;

    wchar_t root[MAX_PATH * 2];
    DWORD len = GetModuleFileNameW(NULL, root, MAX_PATH * 2);
    if (len == 0 || len >= MAX_PATH * 2) {
        show_err(L"Unable to determine installation directory.");
        return 2;
    }
    while (len > 0 && root[len - 1] != L'\\' && root[len - 1] != L'/') len--;
    if (len > 0) root[len - 1] = L'\0';

    wchar_t bin_dir[MAX_PATH * 2];
    wchar_t engine_path[MAX_PATH * 2];
    wsprintfW(bin_dir, L"%s\\bin", root);
    wsprintfW(engine_path, L"%s\\bin\\engine.dll", root);

    // Ensure bin\ is on the DLL search path (for SDL3.dll)
    SetDllDirectoryW(bin_dir);
    SetCurrentDirectoryW(root);

    HMODULE eng = LoadLibraryW(engine_path);
    if (!eng) {
        show_err(L"Failed to load runtime (bin\\engine.dll).\n\nPlease run Launcher.exe to complete or repair setup.");
        return 2;
    }

    de_engine_run_fn run_fn = (de_engine_run_fn)(void *)GetProcAddress(eng, DE_ENGINE_RUN_SYM);
    if (!run_fn) {
        show_err(L"Runtime binary is corrupted or incompatible.\n\nPlease reinstall from the official release.");
        return 2;
    }

    return run_fn(NULL, 0, root);
}
