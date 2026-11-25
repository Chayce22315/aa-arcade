# Building and testing

Requirements: CMake 3.25 or newer, a C++20 compiler, Python 3.9+ for the tools (not for the build),
and network access at configure time to fetch SDL 3.4.20 (or a local copy, see below).

## Windows (MSVC, the reference build)

Visual Studio 2022 or newer with the "Desktop development with C++" workload (the preset uses the newest Visual Studio generator CMake finds):

```
cmake --preset msvc-x64-release
cmake --build --preset msvc-x64-release
build\msvc-x64\Release\spindle_tests.exe
build\msvc-x64\Release\spindle.exe --windowed
```

`spindle.exe` is self-contained (SDL3, Lua and the C runtime are static). Copy it anywhere; it creates
`settings.ini`, `bookkeeping.ini` and `logs\` next to itself on first start.

## Windows from Linux (MinGW-w64)

```
sudo apt-get install g++-mingw-w64-x86-64-posix
cmake --preset mingw-x64-release
cmake --build --preset mingw-x64-release
```

## Linux development build

The game logic is platform independent; a Linux build is used for development, the unit tests and the
headless self-test. Desktop builds need SDL3's usual X11 or Wayland development packages; a headless
build without them works too:

```
cmake --preset linux-debug -DSDL_X11=OFF -DSDL_WAYLAND=OFF
cmake --build --preset linux-debug
```

Unit tests only, without SDL at all:

```
cmake -S . -B build/core -DSPINDLE_BUILD_GAME=OFF
cmake --build build/core
build/core/spindle_tests
```

## Options

| CMake option | Default | Effect |
|---|---|---|
| `SPINDLE_BUILD_GAME` | `ON` | build the SDL platform layer and `spindle` |
| `SPINDLE_BUILD_TESTS` | `ON` | build `spindle_tests` |
| `SPINDLE_SYSTEM_SDL3` | `OFF` | use an installed SDL3 (`find_package`) instead of the pinned release |
| `SPINDLE_WARNINGS_AS_ERRORS` | `ON` | `/W4 /WX` or `-Wall -Wextra ... -Werror` on spindle's own code |
| `FETCHCONTENT_SOURCE_DIR_SDL3` | - | use an already extracted `SDL3-3.4.20` source folder (offline builds) |

## Tests

- **Unit tests**: `spindle_tests [filter]` runs every case (or those whose name contains `filter`).
- **Self-test**: `python tools/run_selftest.py path/to/spindle[.exe] --out selftest-run` runs the
  scripted end-to-end test with SDL's offscreen video and dummy audio drivers, prints the report,
  converts the screenshots to PNG and returns the executable's exit code. The same thing by hand:
  `SDL_VIDEODRIVER=offscreen SDL_AUDIO_DRIVER=dummy spindle --root DIR --selftest DIR/out`.

## Developer tools (`tools/`, Python standard library only)

| Tool | Purpose |
|---|---|
| `gen_sfx.py` | regenerates `data/audio/*.wav` from the recipes; `--check` verifies the committed files |
| `run_selftest.py` | runs and collects the self-test (see above) |
| `validate_settings.py --exe spindle settings.ini` | checks a settings file against the game's schema |
| `level_report.py --exe spindle` | generates rounds through the real level script and checks the curve |
| `update_controller_db.py COMMIT` | pins a new SDL_GameControllerDB commit (rewrites `src/assets/ControllerDbPin.hpp`) |

## Changing content

- **Levels**: edit `data/scripts/levels.lua` and rebuild (it is embedded). For a cabinet, a copy at
  `data/scripts/levels.lua` next to the exe overrides the embedded one without rebuilding.
- **Bots**: the same with `data/scripts/bots/bot.lua`.
- **Sounds**: edit the recipes in `tools/gen_sfx.py`, run it, commit the WAVs, rebuild.
- **Title**: `src/engine/Branding.hpp` and the CMake project name.

## Continuous integration

`.github/workflows/build.yml` builds with MSVC on `windows-latest`, runs the unit tests, the data checks
and the headless self-test, and uploads `spindle-windows-x64.zip` and the self-test screenshots as
artifacts. A second job builds the core on Linux and runs the unit tests.
