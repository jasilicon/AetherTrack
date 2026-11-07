#!/usr/bin/env bash
set -euo pipefail

# One-line web installer for AetherTrack.
#
#   curl -fsSL https://raw.githubusercontent.com/YOUR_USERNAME/AetherTrack/main/install.sh | bash
#
# Override the repository or asset with AETHER_REPO / AETHER_ASSET.

REPO="${AETHER_REPO:-jasilicon/AetherTrack}"
ASSET="${AETHER_ASSET:-}"
ARCH="$(dpkg --print-architecture 2>/dev/null || uname -m)"

if [ -z "$ASSET" ]; then
    case "$ARCH" in
        amd64|arm64)
            ASSET="aethertrack_${ARCH}.deb"
            ;;
        x86_64)
            ASSET="aethertrack_amd64.deb"
            ;;
        aarch64)
            ASSET="aethertrack_arm64.deb"
            ;;
        *)
            echo "Unsupported architecture: ${ARCH}" >&2
            exit 1
            ;;
    esac
fi

if ! command -v apt >/dev/null 2>&1 && ! command -v apt-get >/dev/null 2>&1; then
    echo "This installer requires apt (Debian/Ubuntu)." >&2
    exit 1
fi

URL="https://github.com/${REPO}/releases/latest/download/${ASSET}"
TMP="$(mktemp --suffix=.deb)"
trap 'rm -f "$TMP"' EXIT

echo "Downloading ${URL}"
curl -fsSL "$URL" -o "$TMP"

if command -v apt >/dev/null 2>&1; then
    sudo apt install -y "$TMP"
else
    sudo apt-get install -y "$TMP"
fi

echo "AetherTrack installed. Launch it from your application menu or run: aethertrack"
