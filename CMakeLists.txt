cmake_minimum_required(VERSION 3.20)

project(AetherTrack
    VERSION 0.1.2
    DESCRIPTION "Ultra-lightweight infinite skill-tracking timer"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Toolchain / language
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to a release build when no configuration was supplied.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies (pkg-config)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM4 REQUIRED IMPORTED_TARGET gtkmm-4.0>=4.10)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

# Optional: X11 lets the micro-pill request _NET_WM_STATE_ABOVE. GTK4 has no
# portable always-on-top API, and Wayland compositors deliberately own that
# policy, so builds without X11 simply skip the hint.
pkg_check_modules(X11 IMPORTED_TARGET x11)

# ---------------------------------------------------------------------------
# Asset lookup paths compiled into the binary
#   - source tree path: works for ./build/aethertrack during development
#   - install path:     works for a system/AppImage install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set(AETHER_ASSET_INSTALL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/AetherTrack/assets")

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(aethertrack
    src/main.cpp
    src/core/Database.cpp
    src/core/TimerEngine.cpp
    src/ui/HeatmapWidget.cpp
    src/ui/HoldButton.cpp
    src/ui/MainWindow.cpp)

target_include_directories(aethertrack PRIVATE src)

target_link_libraries(aethertrack PRIVATE
    PkgConfig::GTKMM4
    PkgConfig::SQLITE3)

if(X11_FOUND)
    target_link_libraries(aethertrack PRIVATE PkgConfig::X11)
    target_compile_definitions(aethertrack PRIVATE AETHER_HAVE_X11)
endif()

target_compile_definitions(aethertrack PRIVATE
    AETHER_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
    AETHER_INSTALL_ASSET_DIR="${AETHER_ASSET_INSTALL_DIR}"
    AETHER_SOURCE_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons")

# Release: -O3 + LTO. Deliberately no -march=native so AppImage/Deb builds
# remain portable across x86-64 machines.
target_compile_options(aethertrack PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-DNDEBUG>
    $<$<CONFIG:Release>:-flto>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>)

target_link_options(aethertrack PRIVATE
    $<$<CONFIG:Release>:-flto>)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aethertrack PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS aethertrack
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(DIRECTORY assets/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/AetherTrack/assets")

install(FILES packaging/io.github.aethertrack.AetherTrack.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

install(DIRECTORY packaging/icons/hicolor
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons"
    FILES_MATCHING PATTERN "*.png")

# Debian/Ubuntu autostart lives in /etc, outside the /usr prefix. Keep it
# optional so AppImage builds can stay self-contained.
option(AETHER_INSTALL_AUTOSTART "Install the XDG autostart entry" ON)
set(AETHER_AUTOSTART_DIR "/etc/xdg/autostart"
    CACHE PATH "System-wide XDG autostart directory")

if(AETHER_INSTALL_AUTOSTART)
    install(FILES packaging/io.github.aethertrack.AetherTrack-autostart.desktop
        DESTINATION "${AETHER_AUTOSTART_DIR}"
        RENAME "io.github.aethertrack.AetherTrack.desktop")
endif()
