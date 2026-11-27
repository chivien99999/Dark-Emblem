cmake_minimum_required(VERSION 3.20)
project(DarkEmblemFrontend C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(NOT WIN32)
    message(FATAL_ERROR "Dark Emblem desktop front-end apps target Windows x64.")
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---- Launcher.exe (Direct2D / DirectWrite / WinHTTP / Windows CNG bcrypt) ----
add_executable(Launcher WIN32
    launcher/launcher.cpp
    assets/launcher.rc
)
target_compile_definitions(Launcher PRIVATE
    "DE_TOOLCHAIN_SHA256=\"f1478538afe675f748f51d2c0301f08b93f9cb481f4817a6ba9d6392fadf603e\""
)
target_link_libraries(Launcher PRIVATE
    d2d1 dwrite dwmapi dxgi winhttp bcrypt ole32 shell32 comdlg32 user32 gdi32
)

# ---- Play.exe (Minimal Win32 launch stub for bin\engine.dll) ----
add_executable(Play WIN32
    play/play_stub.c
    assets/play.rc
)
target_link_libraries(Play PRIVATE user32)
if(MSVC)
    target_link_options(Play PRIVATE /HIGHENTROPYVA:NO)
endif()

# ---- Settings.exe (Standalone Win32 graphics & upscaler configurator) ----
add_executable(Settings WIN32
    settings/settings_app.c
    settings/ini_settings.c
    assets/settings.rc
)
target_include_directories(Settings PRIVATE settings)
target_link_libraries(Settings PRIVATE user32 gdi32 comctl32 shell32)
