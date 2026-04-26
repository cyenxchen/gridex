# Gridex — Linux

AI-Native Database IDE for Linux. Qt 6 + C++20.

Part of the Gridex multi-platform project. See the top-level repo for macOS and Windows ports.

## Status

Phase 1 scaffolding. Empty `MainWindow` launches; layers + CI wired up. Subsequent phases add database adapters, services, and full UI per the plan.

## Build requirements

- **C++20** compiler: GCC ≥ 11 or Clang ≥ 14
- **CMake** ≥ 3.24
- **Ninja** (recommended generator)
- **Qt 6 LTS** (≥ 6.4): `Qt6::Widgets`
- Mesa/OpenGL dev headers

### Distro packages

**Ubuntu 22.04** (Qt 6.2 in main — use `jurplel/install-qt-action` or a 6.4 backport PPA)

**Ubuntu 24.04**

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
```

**Debian 12**

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
```

**Fedora 40**

```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config \
    qt6-qtbase-devel mesa-libGL-devel
```

## Configure & build

From the repository root:

```bash
cmake -S linux -B linux/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build linux/build --parallel
./linux/build/gridex
```

## Directory layout

```
linux/
├── CMakeLists.txt              # root build
├── cmake/                      # FindXxx.cmake modules (populated in later phases)
├── src/
│   ├── main.cpp                # app entry
│   ├── Core/                   # protocols, models, enums, errors (no Qt)
│   ├── Domain/                 # use cases, repository interfaces (no Qt) — later phase
│   ├── Data/                   # adapters, persistence, credentials — later phase
│   ├── Services/               # query engine, MCP, AI, SSH — later phase
│   ├── Presentation/           # Qt Widgets views + view models
│   └── App/                    # DI container, config, lifecycle
├── resources/                  # .qrc, icons — later phase
├── tests/                      # Qt Test — later phase
└── packaging/
    ├── appimage/               # AppImage build pipeline
    ├── debian/                 # .deb — Phase 8
    └── rpm/                    # .rpm — Phase 8
```

## AppImage

After a successful build and with `linuxdeploy` + `linuxdeploy-plugin-qt` in `PATH`:

```bash
./linux/packaging/appimage/build-appimage.sh
```

Output goes to `linux/dist/`.

## CI

`.github/workflows/linux-build.yml` runs builds on Ubuntu 22.04, Ubuntu 24.04, Debian 12, and Fedora 40.

## Roadmap

See `/home/danghuynh/.claude/plans/piped-hopping-newt.md` for the full 8-phase plan, or `plans/reports/brainstorm-260415-1008-linux-qt-port.md` for the architecture brainstorm.
