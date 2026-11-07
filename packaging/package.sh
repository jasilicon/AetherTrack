#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-package}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/.tools}"
VERSION="${VERSION:-$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9A-Za-z.+-]*\).*/\1/p' "$ROOT/CMakeLists.txt" | head -n1)}"
ARCH="${ARCH:-$(dpkg --print-architecture 2>/dev/null || uname -m)}"
APPIMAGE_ARCH="$ARCH"
if [ "$APPIMAGE_ARCH" = "amd64" ]; then
    APPIMAGE_ARCH="x86_64"
fi
JOBS="${JOBS:-$(nproc)}"
TARGET="${1:-all}"

if [ -z "$VERSION" ]; then
    echo "Could not determine project version" >&2
    exit 1
fi

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

configure_build() {
    local autostart="$1"
    local generator=()

    if command -v ninja >/dev/null 2>&1; then
        generator=(-G Ninja)
    fi

    cmake -S "$ROOT" -B "$BUILD_DIR" "${generator[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DAETHER_INSTALL_AUTOSTART="$autostart"
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
}

build_deb() {
    need cmake
    need dpkg-deb

    echo "Building .deb ${VERSION} (${ARCH})"
    configure_build ON

    local stage="$DIST/deb-root"
    rm -rf "$stage"
    DESTDIR="$stage" cmake --install "$BUILD_DIR"

    mkdir -p "$stage/DEBIAN"
    local installed_size
    installed_size="$(du -sk "$stage/usr" | cut -f1)"

    cat > "$stage/DEBIAN/control" <<EOF
Package: aethertrack
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Installed-Size: ${installed_size}
Maintainer: AetherTrack Maintainers <noreply@example.com>
Depends: libc6 (>= 2.34), libgcc-s1, libstdc++6, libgtkmm-4.0-0 (>= 4.10), libsqlite3-0, libx11-6
Description: ultra-lightweight infinite skill-tracking timer
 AetherTrack keeps one running stopwatch per skill and remembers the total
 time spent on each activity across sessions and restarts.
EOF

    local output="$DIST/aethertrack_${VERSION}_${ARCH}.deb"
    rm -f "$output"
    dpkg-deb --root-owner-group --build "$stage" "$output"
    echo "Created $output"

    # Stable, version-less alias used by the one-line web installer:
    #   .../releases/latest/download/aethertrack_<arch>.deb
    local stable="$DIST/aethertrack_${ARCH}.deb"
    cp -f "$output" "$stable"
    echo "Created $stable"
}

fetch_tool() {
    local destination="$1"
    local url="$2"

    if [ -x "$destination" ]; then
        return
    fi

    echo "Downloading $(basename "$destination")"
    if command -v wget >/dev/null 2>&1; then
        wget -q -O "$destination" "$url"
    elif command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$destination" "$url"
    else
        echo "wget or curl is required to download AppImage tooling" >&2
        exit 1
    fi
    chmod +x "$destination"
}

build_appimage() {
    need cmake

    echo "Building AppImage ${VERSION} (${APPIMAGE_ARCH})"
    configure_build OFF

    mkdir -p "$DIST" "$TOOLS_DIR"
    local linuxdeploy="$TOOLS_DIR/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    local appimagetool="$TOOLS_DIR/appimagetool-${APPIMAGE_ARCH}.AppImage"
    local appdir="$DIST/AetherTrack.AppDir"
    local output="$DIST/AetherTrack-${VERSION}-${APPIMAGE_ARCH}.AppImage"

    fetch_tool "$linuxdeploy" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    fetch_tool "$appimagetool" \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${APPIMAGE_ARCH}.AppImage"

    rm -rf "$appdir"
    mkdir -p "$appdir"
    DESTDIR="$appdir" cmake --install "$BUILD_DIR"

    cp "$ROOT/packaging/io.github.aethertrack.AetherTrack.desktop" "$appdir/"
    cp "$ROOT/packaging/io.github.aethertrack.AetherTrack.png" "$appdir/"

    export APPIMAGE_EXTRACT_AND_RUN=1
    "$linuxdeploy" --appdir "$appdir" \
        --executable "$appdir/usr/bin/aethertrack" \
        --desktop-file "$appdir/io.github.aethertrack.AetherTrack.desktop" \
        --icon-file "$appdir/io.github.aethertrack.AetherTrack.png"

    rm -f "$appdir/AppRun"
    cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export AETHER_ASSET_DIR="$HERE/usr/share/AetherTrack/assets"
export AETHER_ICON_DIR="$HERE/usr/share/icons"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
exec "$HERE/usr/bin/aethertrack" "$@"
EOF
    chmod +x "$appdir/AppRun"

    rm -f "$output"
    ARCH="$APPIMAGE_ARCH" "$appimagetool" "$appdir" "$output"
    chmod +x "$output"
    echo "Created $output"
}

case "$TARGET" in
    deb)
        build_deb
        ;;
    appimage)
        build_appimage
        ;;
    all)
        build_deb
        build_appimage
        ;;
    *)
        echo "Usage: $0 [deb|appimage|all]" >&2
        exit 2
        ;;
esac
