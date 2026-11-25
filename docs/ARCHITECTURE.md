# Architecture

spindle is one static executable built from two libraries:

- **`spindle_core`** (SDL-free): rules, gameplay, collision, bots, configuration, arcade logic, the
  stroke font and layout maths. Everything here is deterministic and covered by `spindle_tests`
  without a window, a GPU or an audio device.
- **`spindle_platform`** (SDL3): window, renderer, input devices, audio, the boot sequence, screens,
  the service menu and the self-test driver.

Dependency rule: nothing in the core includes SDL. The platform turns SDL into plain data for the core
(input actions, sizes) and turns core state into pixels and sound.

## Modules

| Module | Library | Responsibility |
|---|---|---|
| `engine/` | core: `Log`; platform: the rest | `App` main loop, `Boot` (the 10 steps), `Paths` (portable folder or user folder), `SelfTest`, `Branding` (title, version), RAII SDL handles |
| `config/` | core | `IniDocument` (comment- and order-preserving INI), `AtomicFile` (temp + flush + rename), `Settings` (one schema drives loading, validation, saving and the default file) |
| `assets/` | core: `EmbeddedAssets`, `Sha256`, `ControllerDbPin`; platform: `Downloader`, `ControllerDb` | resources compiled into the exe; the one optional download (WinHTTP, https only, size-capped, pinned SHA-256) |
| `collision/` | core | pure angular maths: wrap, separation threshold `2 asin(r/L)`, flight-time contact tests, sub-step count |
| `scripting/` | core | `LuaSandbox`: one isolated Lua 5.4 state with base/math/string/table only, a memory cap, an instruction budget per call and a seeded PCG32 `math.random` |
| `gameplay/` | core | `Rotor` (the shared rotation), `Playfield` (one station: placements, flights, collision, powerup effects), `Powerups` (ring kinds and constants), `LevelDef` + validation, `LevelScript` (Lua levels and rings with fallback), `MatchRules` (pure T1-T11 decisions), `Match` (rounds, phases, tries, elimination, mend) |
| `players/` | core | `PlayerManager`: the mode rule and bot fill (stations keep their physical columns) |
| `bots/` | core | `BotBrain` (sandboxed `bot.lua` per bot), `BotController` (delayed observation, fire presses only); `bot.lua` keeps a reachability map of the angles its aim passes so swinging circles never stall it |
| `arcade/` | core | `ArcadeMachine` (attract, insert coin, join, playing, game over), `Credits`, `Bookkeeping` |
| `input/` | core: `Keybinds`; platform: `DeviceRegistry`, `InputSystem` | keyboards, gamepads and raw joysticks, hotplug, assignment, bindings, timestamped actions |
| `audio/` | core: `Wav`, `SoundId`; platform: `AudioSystem` | WAV parsing; a fixed 16-voice mixer on one SDL audio stream |
| `render/` | core: `StrokeFont`, `Layout`, `Geometry2D`; platform: `Renderer2D`, `DisplaySetup` | procedural drawing with 1-pixel alpha feathering, the four-column layout, window and display modes |
| `menus/` | core: `MenuModel`; platform: `Screens`, `ServiceMenu` | every cabinet screen and the operator menu |

## The frame

```
SDL events ──> InputSystem ──(timestamped queue)──┐
                                                  │ drained per tick, by timestamp
fixed 240 Hz tick ────────────────────────────────┘
   ├─ stations lost / restored ──> ArcadeMachine (pause / resume)
   ├─ service menu open?  yes ──> ServiceMenu.update      (cabinet frozen)
   │                      no  ──> ArcadeMachine.tick(input)
   │                                 ├─ coins -> Credits, Bookkeeping
   │                                 ├─ state machine (attract pages, join window, game over timer)
   │                                 └─ Match.tick(fire presses per station)
   │                                       ├─ BotController.think (before the tick, delayed view)
   │                                       ├─ Rotor.advance  (once)
   │                                       ├─ Playfield.step x alive stations (shared rotor motion,
   │                                       │    powerup rings taken -> shield / skip / slim, mend -> tries)
   │                                       └─ MatchRules when every station is done
   └─ events ──> AudioSystem.play, bookkeeping.ini (atomic)
render (vsync or frame cap) ──> Screens / ServiceMenu ──> Renderer2D ──> one SDL_RenderGeometry
```

- **Timestep**: the simulation always advances in 1/240 s ticks. Real time is consumed in whole ticks;
  if the machine falls more than 250 ms behind, the backlog is dropped (logged) instead of spiralling.
- **Input timing**: SDL stamps every event with a nanosecond timestamp. Events wait in a queue and are
  released to the tick whose end time they precede, so a press lands on the tick it happened in,
  independent of the frame rate.
- **Rendering**: the latest tick is drawn; the rotor angle is extrapolated by the time since that tick
  (at most 4.2 ms) so motion is smooth at any refresh rate. All geometry of a frame goes out in one
  batched `SDL_RenderGeometry` call.
- **Threads**: the game runs on the main thread. The only other thread is SDL's audio callback, which
  reads clips that never change after boot and a voice table guarded by the audio stream lock;
  volumes are atomics.

## Data and resources

| What | Where it lives | Fallback |
|---|---|---|
| level script `scripts/levels.lua` | embedded; optional override `data/scripts/levels.lua` next to the exe | override errors -> embedded for that round; embedded errors -> built-in emergency round |
| bot script `scripts/bots/bot.lua` | embedded; optional override `data/scripts/bots/bot.lua` | override rejected at boot if it does not load -> embedded |
| sound effects | embedded WAVs generated by `tools/gen_sfx.py` | a clip that does not parse stays silent (logged) |
| font, circles, needles, menus | code | - |
| `gamecontrollerdb.txt` | downloaded once, verified against the pinned SHA-256 | SDL's built-in mappings |
| `settings.ini`, `bookkeeping.ini`, `logs/` | next to the exe, else the user folder | defaults / empty counters |

## Boot

`Boot.cpp` runs the ten steps in order and logs each one (`boot N/10: ...`):

1. check required files and dependencies: settings.ini (created if missing), bookkeeping, write
   access, SDL version and drivers, Lua, presence and hash of the controller database
2. download missing components (only the controller database, only if allowed)
3. verify the download against the pin
4. load resources: controller mappings, level and bot scripts, sounds, key bindings
5. detect the display
6. read the preferred resolution and mode
7. create the window (hidden) in that mode
8. display-dependent resources: renderer and layout
9. initialize the arcade: audio device, bookkeeping, the arcade state machine, the service menu
10. show the window and enter attract mode

A failure that makes the cabinet unusable (no SDL video, no renderer, no Lua) shows a message box and
exits; everything else degrades and is logged (no audio device: silent; no controller database:
built-in mappings).

## Testing

- `spindle_tests` (core): unit tests for every module of the core, including one test per rule
  case T1-T11 and bot matches at every difficulty.
- `spindle --selftest DIR`: drives the real executable through SDL (pushed keyboard events, SDL virtual
  joysticks for an Xbox-type pad, a PlayStation-type pad and an unmapped arcade stick), across all four
  stations, a disconnect and reconnect, the service menu (changing and saving settings), and the
  T1, T2, T4, T5 and T10 rule cases. It writes screenshots and `selftest_report.txt`.
