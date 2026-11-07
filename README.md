<p align="center">
  <img src="packaging/io.github.aethertrack.AetherTrack.png" width="96" alt="AetherTrack icon">
</p>

# AetherTrack

Ultra-lightweight infinite skill-tracking timer for Linux, built with GTK4 and
SQLite. Keep one running stopwatch per skill and track the total time spent on
each activity across sessions and restarts.

## Install

### One-line installer (Ubuntu 26.04 / Debian-based)

```bash
curl -fsSL https://github.com/jasilicon/AetherTrack/releases/latest/download/aethertrack_amd64.deb -o /tmp/aethertrack.deb && sudo apt install -y /tmp/aethertrack.deb
```

Or use the hosted installer script (detects `amd64` / `arm64`):

```bash
curl -fsSL https://raw.githubusercontent.com/jasilicon/AetherTrack/main/install.sh | bash
```

### Manual `.deb` install

Download `aethertrack_amd64.deb` from the
[latest release](https://github.com/jasilicon/AetherTrack/releases/latest), then:

```bash
sudo apt install ./aethertrack_amd64.deb
```

### Build from source

Install the build dependencies on Ubuntu 24.04+ / 26.04:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config ninja-build \
                 libgtkmm-4.0-dev libsqlite3-dev libx11-dev
```

Build and run without installing:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/aethertrack
```

Install system-wide:

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build
```

This installs the executable, desktop entry, and application icons. A system
autostart entry is also installed unless you configure with
`-DAETHER_INSTALL_AUTOSTART=OFF`.

## Build packages

```bash
bash packaging/package.sh deb       # -> dist/aethertrack_<version>_<arch>.deb
bash packaging/package.sh appimage  # -> dist/AetherTrack-<version>-<arch>.AppImage
```

`package.sh deb` also writes the stable alias `dist/aethertrack_<arch>.deb`
used by the one-line installer.

## Usage

Launch **AetherTrack** from your application menu or run:

```bash
aethertrack
```

Use the `+` button to add activities, then start/pause the timer. Click
**Insights & History** to view lifetime mastery, a 90-day heatmap, and recent
sessions. Use the collapse button to switch to the always-on-top micro pill.

## Uninstall

If installed from the `.deb`:

```bash
sudo apt remove aethertrack
```

If installed manually with `cmake --install`:

```bash
sudo xargs rm -v < build/install_manifest.txt
```

## Releases

Push a version tag to build and publish the `.deb` automatically:

```bash
git tag v0.1.2
git push origin v0.1.2
```

The release workflow uploads both the versioned `.deb` and the stable
`aethertrack_amd64.deb` asset used by the one-line installer.
