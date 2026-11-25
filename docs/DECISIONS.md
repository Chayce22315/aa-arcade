# Engineering decisions

Each entry: the decision, then why.

**Original game in the genre, title "spindle".** The core mechanic (needles fired into a turning circle)
is kept; the title, typography, sounds, level designs and presentation are spindle's own, and nothing
implies affiliation with any existing game. The repository keeps its name.

**C++20, MSVC x64 first; MinGW and Linux as secondary toolchains.** MSVC is the Windows reference
compiler and is built in CI; MinGW-w64 cross-compiles the same sources from Linux; Linux builds run the
unit tests and the headless self-test during development.

**SDL 3.4.20 from the official release tarball, pinned by SHA-256, linked statically with a static CRT
(`/MT`).** A cabinet should need one file, not an installer or a redistributable. Pinning the tarball
hash makes the build reproducible; `-DSPINDLE_SYSTEM_SDL3=ON` uses an installed SDL3 instead.

**Lua 5.4.7 vendored.** Small, MIT, no network needed at build time. Taken from the `lua/lua` git tag
`v5.4.7` (lua.org was not reachable from the build environment), minus the interpreter and test files.

**SDL_Renderer with `SDL_RenderGeometry`, no shaders.** The look is white geometry on black. A
one-pixel alpha feather on every edge gives clean anti-aliasing on every SDL backend (Direct3D 11/12,
OpenGL, Vulkan, software) without GLSL or a GPU-specific path. GLSL was listed as optional in the
brief; nothing in this visual style needs it, so it is not used.

**Built-in stroke font.** Text is drawn as polylines with the same anti-aliased lines as the needles:
crisp at any resolution, no font file, no font licence, one consistent minimal style. Lowercase for
the title and descriptive text, uppercase for the classic arcade prompts (INSERT COIN, PRESS START,
CREDIT, GAME OVER).

**Fixed 240 Hz simulation with timestamped input.** Collision and fairness should not depend on the
monitor. 240 Hz keeps a launch quantized to 4.2 ms; event timestamps put each press on the tick it
happened.

**One shared rotor object per round.** Identical rotation on every station is guaranteed by
construction (one object, read by all), not by keeping four copies in sync.

**Exact rotor integration.** Speed changes are piecewise linear, so the angle is integrated exactly
(`v t + a t^2 / 2`) with segment boundaries split inside a tick; results are deterministic for a given
seed.

**Collision by geometry, not by pixels.** A ball in flight is tested against every attached ball with
sub-steps of at most a quarter radius of movement, plus the exact angular threshold at attachment.
Pure functions, unit-tested.

**Levels and bots in Lua, sandboxed.** The brief asks for Lua for bots and gameplay scripting. Each
script gets its own state with only base, math, string and table; no file, OS or module access; a
memory cap; an instruction budget per call; a seeded deterministic `math.random`. C++ validates every
level and enforces the bot rules, so a bad script cannot break fairness or freeze the cabinet.

**Bots see a delayed world.** The reaction delay is applied in C++ (a ring buffer of snapshots); the
script never sees the present or the rotation program, and its only output is a fire press through
the same station input path as a human.

**Rule additions beyond the brief, both documented in RULES.md.** A round timer (a timeout is a failed
attempt), because otherwise an idle player stalls a round forever; and the T11 abandoned-match guard,
because otherwise idle players in a sudden death would tie forever and the cabinet would never return
to attract.

**A disconnect pauses the whole match.** Pausing one station would let the others keep playing on a
shared rotor the missing player could not see; pausing everyone keeps the round identical for all.
Reconnecting the same controller model re-attaches it automatically; a station's keyboard Start also
resumes. `disconnect_action = continue` is available for venues that prefer it.

**Everything essential embedded; one optional, pinned download.** Lua scripts and sounds are compiled
into the executable, so the game runs offline from a single file. The only thing downloaded is the
community controller database, which improves generic-controller support; it is accepted only if its
size and SHA-256 match the pin, written atomically, and the game works without it.

**WinHTTP for the download.** It ships with Windows, uses the system proxy and certificate store, and
adds no dependency. Non-Windows builds have no downloader and say so in the log.

**Portable cabinet files.** settings.ini, bookkeeping.ini and logs live next to the exe; if that folder
is read-only they move to the user's application data folder. All writes are atomic.

**settings.ini is never silently rewritten.** Invalid values fall back to defaults and are reported;
only Save & Exit in the service menu writes the file, keeping the operator's comments.

**Service key as the operator entry point.** F2 opens the menu from any state and freezes the
cabinet; inside the menu, the service key returns to the main page, and on the main page it saves and
exits. A locked cabinet (`exit_hold_seconds = 0`) also ignores window-close requests; Shut Down in the
service menu is then the way out.

**Sound effects synthesized by a Python tool and committed.** Deterministic recipes
(`tools/gen_sfx.py`), checked in CI with `--check`; the build itself never needs Python.

**Self-test drives SDL, not a mock.** Keyboard events go through SDL's event queue and controllers are
SDL virtual joysticks, so hotplug, gamepad type detection, assignment and disconnect handling are the
real code paths. Physical hardware (real pads, Bluetooth, multiple keyboards, monitors, speakers) still
has to be checked on the cabinet.

**Powerups are earned on the circle and only help the one who earns them.** The rings come from the
level script, so every station gets identical rings and identical chances; taking one is a timing
skill (land a needle on it). No effect touches the shared rotation or another station, which keeps
the brief's guarantees: one rotation for all, identical round conditions, independent collisions.
No rings in sudden death, so tie-breaks stay pure.

**One image, generated and committed like the sounds.** The fire meter's flame is the only picture in
spindle: `data/art/flame.png` (128 x 256 RGBA, 12.8 KB). A flame drawn from triangles at meter size
looked like a cone, and a soft orange glow is exactly what a texture does cheaply. It is produced by
`tools/gen_art.py` (stdlib only, deterministic, checked in CI with `--check`), embedded in the exe,
header-checked at boot step 4 (pure `PngInfo`, unit-tested) and turned into a texture at step 8 with
SDL 3.4's built-in `SDL_LoadPNG_IO` (no image library). It is recreated on a render-device reset, and
if it ever fails to load the screens draw a plain glow instead, so it is never required to play.
Textured quads share the shape batch (`Renderer2D::sprite`); a texture change flushes it, which keeps
draw order.

**The fire meter rewards fast, close play and is strong on purpose.** It is the combo the brief asked
for: quick needles chain, close calls count double, a full meter sets the station on fire for 1.5 s
and fire needles burn through whatever they touch. Burning is a real advantage (a player who catches
fire late in a round can often finish it), so the meter is tuned to need sustained risk: 8 points
within 1 s gaps, a touch or a fail empties it, it never fills while burning, and only a cleared round
banks it. Everything is per station (identical conditions for all), there is none in sudden death,
and bots press fire while burning at a human mashing rate rather than the game's minimum interval.
All four values are operator settings and uncalibrated.

**Bots judge gaps by reachability, not by size alone.** On rounds where the circle swings back and
forth, the widest gap can sit where the circle turns around and never reach the launcher; bots that
waited for "the big gap" stalled until the round timer ran out. The bot now records which angles its
own aim (timing error included) actually passes, aims for the best of those, sizes its safety margin
from its own skill, and with patience settles for any spot it can make. A regression test replays
the swing rounds with the seeds that used to stall.

