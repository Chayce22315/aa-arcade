cmake_minimum_required(VERSION 3.25)

project(spindle VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT everywhere (including SDL) so spindle.exe needs no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(SPINDLE_SYSTEM_SDL3 "Use an installed SDL3 (find_package) instead of the pinned release" OFF)
option(SPINDLE_BUILD_TESTS "Build the spindle_tests unit test executable" ON)
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
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
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
  audio/round_start.wav)
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
  ${SPINDLE_GENERATED_DIR}/spindle/EmbeddedData.cpp
  src/audio/Wav.cpp
  src/collision/Collision.cpp
  src/scripting/LuaSandbox.cpp
  src/gameplay/Rotor.cpp
  src/gameplay/LevelDef.cpp
  src/gameplay/LevelScript.cpp
  src/gameplay/Playfield.cpp
  src/gameplay/MatchRules.cpp
  src/gameplay/Match.cpp
  src/players/PlayerManager.cpp
  src/bots/BotRuntime.cpp
  src/bots/BotController.cpp
  src/arcade/Credits.cpp
  src/arcade/Bookkeeping.cpp
  src/arcade/ArcadeMachine.cpp
  src/input/Keybinds.cpp
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
  src/render/DisplaySetup.cpp
  src/input/DeviceRegistry.cpp
  src/input/InputSystem.cpp
  src/audio/AudioSystem.cpp
  src/menus/Screens.cpp
  src/menus/ServiceMenu.cpp)
target_link_libraries(spindle_platform PUBLIC spindle_core SDL3::SDL3)
if(WIN32)
  target_link_libraries(spindle_platform PUBLIC winhttp)
endif()
spindle_set_warnings(spindle_platform)

# ---------------------------------------------------------------- executable
add_executable(spindle WIN32 src/main.cpp)
target_link_libraries(spindle PRIVATE spindle_platform)
spindle_set_warnings(spindle)
if(MINGW)
  target_link_options(spindle PRIVATE -static -static-libgcc -static-libstdc++)
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
    tests/TestMatchRules.cpp
    tests/TestMatch.cpp
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
