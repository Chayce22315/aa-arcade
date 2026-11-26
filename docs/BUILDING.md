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

## Android

The game is the same C++; `android/` is a small Gradle project that builds it with CMake into
`libmain.so` (plus `libSDL3.so`) and packages it with SDL's Java glue (vendored from SDL 3.4.20 in
`android/app/src/main/java/org/libsdl/app/`, zlib licence) and `SpindleActivity`.

Requirements: JDK 17, the Android SDK with platform 35, an NDK (28.2 by default) and CMake 3.25+ from
the SDK manager. Installing the SDK means accepting Google's Android SDK licence in the SDK manager or
Android Studio.

```
cd android
./gradlew assembleRelease                      # app/build/outputs/apk/release/app-release.apk
./gradlew assembleDebug -Pspindle.abis=x86_64  # a debug build for the emulator
```

- ABIs: `arm64-v8a` and `x86_64` (Chromebooks, emulators). Override with `-Pspindle.abis=...`; the NDK
  and CMake versions with `-Pspindle.ndkVersion=...` and `-Pspindle.cmakeVersion=...`.
- **Signing.** Without a key the release APK is signed with the debug key: it installs fine (side-loading,
  itch.io) but the Play Store will not take it. For a real release, create your own keystore once (keep
  it and its passwords safe and out of the repository; losing it means you can never update the app)
  and build with `SPINDLE_KEYSTORE`, `SPINDLE_KEYSTORE_PASSWORD`, `SPINDLE_KEY_ALIAS`,
  `SPINDLE_KEY_PASSWORD` set in the environment.
- **Signing in CI.** Add four repository secrets (GitHub: Settings, Secrets and variables, Actions):
  `SPINDLE_KEYSTORE_B64` (the keystore file in base64, e.g. `base64 -w0 spindle.keystore`),
  `SPINDLE_KEYSTORE_PASSWORD`, `SPINDLE_KEY_ALIAS` and `SPINDLE_KEY_PASSWORD`. The Android job then signs
  `spindle-android.apk` with that key; without them it keeps using the debug key and says so in its log.
  An APK signed with your key cannot update one signed with the debug key (Android refuses a different
  signature): uninstall the debug-signed one once. Each release raises `versionCode` in
  `android/app/build.gradle` so it installs over the previous one.
- Install on a phone: `adb install app-release.apk`, or copy the APK over and open it (allow installing
  from that source when Android asks).
- Run the self-test on a device or emulator: `bash ci/android_selftest.sh app-debug.apk out/` (it starts
  the app with `--es args "--selftest"` and collects the report and screenshots).

## iOS (iPhone and iPad)

Needs a Mac with Xcode 15 or newer. CMake generates an Xcode project; the app bundle uses
`ios/Info.plist.in` and the icon in `ios/Assets.xcassets`.

```
cmake -S . -B build/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4
open build/ios/spindle.xcodeproj
```

- **Simulator:** build the `spindle` scheme for any iPhone or iPad simulator. From the command line:
  `cmake --build build/ios --config Release -- -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO`, then
  `python3 ci/ios_selftest.py build/ios/Release-iphonesimulator/spindle.app out/` runs the self-test.
- **The .ipa from CI:** every push builds `spindle-ios-unsigned.ipa` (arm64, iOS 16.4+). It is
  deliberately unsigned: iPhones only run apps signed for them, and signing needs an Apple account
  (spindle never holds anyone's certificates). To put it on your own phone, open it with a sideloading
  tool that signs it with your Apple ID (Sideloadly or AltStore, for example; with a free Apple ID the
  app runs for 7 days before it has to be signed again), or build from Xcode as below. You can upload the
  .ipa to itch.io, but players can only install it the same way, by signing it themselves.
- **Your own iPhone:** configure with `-DSPINDLE_IOS_TEAM_ID=<your team id>` (Xcode > Settings >
  Accounts; a free Apple ID works, but the app then expires after 7 days), plug the phone in, pick it
  as the run destination and press Run. Apple decides signing; spindle only passes your team id on.
- **App Store / TestFlight:** needs the paid Apple Developer Program and its review; itch.io cannot
  distribute iOS apps.
- The bundle identifier for both platforms is `io.itch.chayce22315.spindle` (`-DSPINDLE_BUNDLE_ID=...`
  and `applicationId` in `android/app/build.gradle` change it).
- iOS 16.4 is the minimum: Apple's C++ library only formats floating point numbers from iOS 16.3.

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
| `gen_art.py` | regenerates `data/art/flame.png` (the fire meter's flame) and the app icons for Android and iOS; `--check` verifies the committed files |
| `run_selftest.py` | runs and collects the self-test (see above) |
| `validate_settings.py --exe spindle settings.ini` | checks a settings file against the game's schema |
| `level_report.py --exe spindle` | generates rounds through the real level script and checks the curve |
| `update_controller_db.py COMMIT` | pins a new SDL_GameControllerDB commit (rewrites `src/assets/ControllerDbPin.hpp`) |

## Changing content

- **Levels**: edit `data/scripts/levels.lua` and rebuild (it is embedded). For a cabinet, a copy at
  `data/scripts/levels.lua` next to the exe overrides the embedded one without rebuilding.
- **Bots**: the same with `data/scripts/bots/bot.lua`. Each difficulty has a `names` pool there; a CPU
  takes one by its seed and shows it in its column header and on the results.
- **Without the exe's folder** (phones, or a locked install): `scripts/levels.lua` and `scripts/bot.lua`
  in the writable folder (where `settings.ini` lives) work as the same overrides. The exe's own
  `data/scripts/` copy wins when both exist. A bot script that does not load is rejected at boot with a
  warning in the log and the embedded one is used; a level script that fails falls back per round.
- **Sounds**: edit the recipes in `tools/gen_sfx.py`, run it, commit the WAVs, rebuild.
- **The flame image**: edit `tools/gen_art.py`, run it, commit `data/art/flame.png`, rebuild.
- **Title**: `src/engine/Branding.hpp` and the CMake project name.

## Continuous integration

`.github/workflows/build.yml` builds with MSVC on `windows-latest`, runs the unit tests, the data checks
and the headless self-test, and uploads `spindle-windows-x64.zip` and the self-test screenshots as
artifacts. A second job builds the core on Linux and runs the unit tests. A third builds the whole game on
Linux (SDL with its X11, Wayland, ALSA and PulseAudio backends, which it loads at run time), runs the
tests and the headless self-test, and uploads `spindle-linux-x64.tar.gz` (the executable, README, licence
and docs; it needs a glibc at least as new as the runner's Ubuntu).

**Releases are automatic.** When every job passes on a push to `main` (for example after merging a pull
request), the release job publishes the four files (`spindle-windows-x64.zip`, `spindle-linux-x64.tar.gz`,
`spindle-android.apk`, `spindle-ios-unsigned.ipa`) as the GitHub release `v<version>`, the version being
`project(spindle VERSION ...)` in `CMakeLists.txt`. The notes come from `data/whats_new.txt`
(`ci/release_notes.sh`). A new version creates a new release marked latest; another push with the same
version replaces that release's files and moves its tag to the new commit. To publish a new version,
raise the version in `CMakeLists.txt` (and `versionCode` / `versionName` in `android/app/build.gradle`)
and update `data/whats_new.txt`. Because the file names never change,
`https://github.com/Chayce22315/aa-arcade/releases/latest/download/<file>` always downloads the newest
release's file. Other branches and pull requests build and test but never release.

The phone jobs: **Android** builds `spindle-android.apk` (release, arm64 + x86_64) with the runner's SDK
and NDK, then a second job installs the x86_64 debug build on an Android emulator and runs the full
self-test there; **iOS** builds the app for the simulator on `macos-latest` and runs the self-test in an
iPhone simulator. Reports, logs and screenshots are uploaded as artifacts.
