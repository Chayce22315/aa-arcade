cmake_minimum_required(VERSION 3.25)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  # std::format of floating point needs iOS 16.3+ in Apple's C++ library.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "16.4" CACHE STRING "Minimum iOS version")
endif()

project(spindle VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++ modules here. Without this CMake 3.28+ scans every C++20 file for them, and with some
# toolchains (CMake 4 + the Android NDK) it picks a mismatched clang-scan-deps and fails.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT everywhere (including SDL) so spindle.exe needs no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Phones (docs/BUILDING.md): Android builds libmain.so for the Gradle project in android/, iOS builds an
# app bundle with the Xcode generator. Neither builds the desktop test runner by default.
if(ANDROID OR IOS)
  set(SPINDLE_MOBILE ON)
else()
  set(SPINDLE_MOBILE OFF)
endif()
if(SPINDLE_MOBILE)
  set(SPINDLE_TESTS_DEFAULT OFF)
else()
  set(SPINDLE_TESTS_DEFAULT ON)
endif()

option(SPINDLE_SYSTEM_SDL3 "Use an installed SDL3 (find_package) instead of the pinned release" OFF)
option(SPINDLE_BUILD_TESTS "Build the spindle_tests unit test executable" ${SPINDLE_TESTS_DEFAULT})
set(SPINDLE_BUNDLE_ID "io.itch.chayce22315.spindle" CACHE STRING "Application id (Android) / bundle identifier (iOS)")
set(SPINDLE_IOS_TEAM_ID "" CACHE STRING "Apple development team id for signed iOS device builds (empty: simulator / unsigned)")
option(SPINDLE_BUILD_GAME "Build the SDL3 platform layer and spindle executable" ON)
option(SPINDLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in spindle code" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Warnings)
include(EmbedData)

# ---------------------------------------------------------------- SDL3
if(NOT SPINDLE_BUILD_GAME)
  # Core and unit tests only; no SDL needed.
elseif(SPINDLE_SYSTEM_SDL3)
  find_package(SDL3 3.4 REQUIRED CONFIG)
else()
  include(FetchContent)
  if(ANDROID)
    # Android's Java side (SDLActivity) loads libSDL3.so and libmain.so separately.
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  # Official SDL release asset, pinned by SHA-256 (release-3.4.20, commit f00dc919ca35b9756316e86c4777392de442f9b2).
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
    URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)
endif()

# ---------------------------------------------------------------- Lua
add_subdirectory(third_party/lua-5.4.7)

# ---------------------------------------------------------------- generated sources
set(SPINDLE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(cmake/Version.hpp.in ${SPINDLE_GENERATED_DIR}/spindle/Version.hpp @ONLY)

set(SPINDLE_EMBEDDED_FILES
  scripts/levels.lua
  scripts/bots/bot.lua
  audio/needle_hit.wav
  audio/fail.wav
  audio/clear.wav
  audio/coin.wav
  audio/join.wav
  audio/menu_move.wav
  audio/menu_select.wav
  audio/winner.wav
  audio/round_start.wav
  audio/powerup.wav
  audio/shield.wav
  audio/ignite.wav
  audio/burn.wav
  audio/tally_tick_1.wav
  audio/tally_tick_2.wav
  audio/tally_tick_3.wav
  audio/tally_tick_4.wav
  audio/tally_done.wav
  audio/crown.wav
  audio/letter_shot.wav
  audio/initials_done.wav
  audio/vote_tick.wav
  audio/vote_cast.wav
  audio/wheel_tick.wav
  audio/wheel_stop.wav
  audio/mode_intro.wav
  art/flame.png)
spindle_embed_files(
  OUTPUT ${SPINDLE_GENERATED_DIR}/spindle/EmbeddedData.cpp
  BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data
  FILES ${SPINDLE_EMBEDDED_FILES})

# ---------------------------------------------------------------- core (SDL-free)
add_library(spindle_core STATIC
  src/engine/Log.cpp
  src/config/IniDocument.cpp
  src/config/AtomicFile.cpp
  src/config/Settings.cpp
  src/assets/Sha256.cpp
  src/assets/EmbeddedAssets.cpp
  src/assets/PngInfo.cpp
  ${SPINDLE_GENERATED_DIR}/spindle/EmbeddedData.cpp
  src/audio/Wav.cpp
  src/collision/Collision.cpp
  src/scripting/LuaSandbox.cpp
  src/gameplay/Powerups.cpp
  src/gameplay/Rotor.cpp
  src/gameplay/LevelDef.cpp
  src/gameplay/LevelScript.cpp
  src/gameplay/Playfield.cpp
  src/gameplay/MatchRules.cpp
  src/gameplay/Scoring.cpp
  src/gameplay/Modes.cpp
  src/gameplay/Match.cpp
  src/players/PlayerManager.cpp
  src/bots/BotRuntime.cpp
  src/bots/BotController.cpp
  src/arcade/Credits.cpp
  src/arcade/Bookkeeping.cpp
  src/arcade/HighScores.cpp
  src/arcade/InitialsEntry.cpp
  src/arcade/VoteBox.cpp
  src/arcade/ArcadeMachine.cpp
  src/input/Keybinds.cpp
  src/input/TouchZones.cpp
  src/render/StrokeFont.cpp
  src/render/Layout.cpp
  src/menus/MenuModel.cpp)
target_include_directories(spindle_core PUBLIC src ${SPINDLE_GENERATED_DIR})
target_link_libraries(spindle_core PUBLIC spindle_lua)
spindle_set_warnings(spindle_core)

# ---------------------------------------------------------------- platform shell (SDL)
if(SPINDLE_BUILD_GAME)
add_library(spindle_platform STATIC
  src/engine/Paths.cpp
  src/engine/Boot.cpp
  src/engine/App.cpp
  src/engine/SelfTest.cpp
  src/assets/Downloader.cpp
  src/assets/ControllerDb.cpp
  src/render/Renderer2D.cpp
  src/render/Textures.cpp
  src/render/DisplaySetup.cpp
  src/input/DeviceRegistry.cpp
  src/input/InputSystem.cpp
  src/audio/AudioSystem.cpp
  src/menus/Screens.cpp
  src/menus/screens/Common.cpp
  src/menus/screens/Field.cpp
  src/menus/screens/Tally.cpp
  src/menus/screens/Initials.cpp
  src/menus/screens/Attract.cpp
  src/menus/screens/Lobby.cpp
  src/menus/screens/Results.cpp
  src/menus/ServiceMenu.cpp
  src/engine/Haptics.cpp)
target_link_libraries(spindle_platform PUBLIC spindle_core SDL3::SDL3)
if(IOS)
  # iPhone haptics come from UIKit (SDL has none there).
  enable_language(OBJCXX)
  target_sources(spindle_platform PRIVATE src/engine/ios/HapticsIos.mm)
  set_source_files_properties(src/engine/ios/HapticsIos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wno-pedantic")
  target_link_libraries(spindle_platform PUBLIC "-framework UIKit")
endif()
if(WIN32)
  target_link_libraries(spindle_platform PUBLIC winhttp)
endif()
spindle_set_warnings(spindle_platform)

# ---------------------------------------------------------------- executable
if(ANDROID)
  # SDLActivity runs SDL_main from libmain.so (see android/).
  add_library(main SHARED src/main.cpp)
  target_link_libraries(main PRIVATE spindle_platform)
  spindle_set_warnings(main)
elseif(IOS)
  set(SPINDLE_IOS_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets)
  add_executable(spindle MACOSX_BUNDLE src/main.cpp ${SPINDLE_IOS_ASSETS})
  set_source_files_properties(${SPINDLE_IOS_ASSETS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_link_libraries(spindle PRIVATE spindle_platform)
  spindle_set_warnings(spindle)
  set_target_properties(spindle PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME spindle
    MACOSX_BUNDLE_GUI_IDENTIFIER ${SPINDLE_BUNDLE_ID}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${SPINDLE_BUNDLE_ID}
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET}
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${SPINDLE_IOS_TEAM_ID}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
else()
  add_executable(spindle WIN32 src/main.cpp)
  target_link_libraries(spindle PRIVATE spindle_platform)
  spindle_set_warnings(spindle)
  if(MINGW)
    target_link_options(spindle PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
endif()

# ---------------------------------------------------------------- tests
if(SPINDLE_BUILD_TESTS)
  enable_testing()
  add_executable(spindle_tests
    tests/TestMain.cpp
    tests/TestSha256.cpp
    tests/TestIni.cpp
    tests/TestSettings.cpp
    tests/TestKeybinds.cpp
    tests/TestCollision.cpp
    tests/TestRotor.cpp
    tests/TestLevels.cpp
    tests/TestPlayfield.cpp
    tests/TestPowerups.cpp
    tests/TestFire.cpp
    tests/TestMobile.cpp
    tests/TestMatchRules.cpp
    tests/TestMatch.cpp
    tests/TestScoring.cpp
    tests/TestHighScores.cpp
    tests/TestInitials.cpp
    tests/TestModes.cpp
    tests/TestVote.cpp
    tests/TestPlayerManager.cpp
    tests/TestBots.cpp
    tests/TestCredits.cpp
    tests/TestArcade.cpp
    tests/TestWav.cpp
    tests/TestStrokeFont.cpp
    tests/TestMenuModel.cpp)
  target_link_libraries(spindle_tests PRIVATE spindle_core)
  spindle_set_warnings(spindle_tests)
  if(MINGW)
    target_link_options(spindle_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME spindle_tests COMMAND spindle_tests)
endif()
