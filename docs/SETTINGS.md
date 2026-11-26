# settings.ini

spindle reads `settings.ini` from the folder next to `spindle.exe` (or from the user folder when that
one is not writable, see the README). If the file is missing, it is created with every key, its default
and a comment describing the valid values.

## How values are handled

- **Missing key**: the default is used (logged at debug level).
- **Invalid value** (unparsable, out of range, unknown choice, empty where a value is required): the
  default is used, a warning naming the key and the reason goes to `logs/spindle.log`, and the service
  menu footer shows the number of invalid values.
- **Malformed lines and duplicate keys** are reported the same way; for duplicates the last one wins.
- **Unknown keys** are ignored.
- The game never rewrites the file on its own. It is only written by **Save & Exit** in the service
  menu, which updates the values in place and keeps every comment and the key order. Writes are atomic
  (temporary file, flush, rename), so a power cut cannot leave a half-written file.
- `true/false`, `yes/no`, `on/off` are accepted wherever `0 | 1` is shown. `fullscreen = 1` / `0` are
  accepted as `borderless` / `windowed`.
- **Phones and tablets** write the same file into the app's private data folder, with one different
  default: `free_play = 1` (a phone has no coin slot). The display settings are ignored there: the game
  always fills the screen in landscape.
- `powerup_types` is a comma list of `shield`, `skip`, `slim`, `mend`, `heat`, `purge`, `swift`, `gold`, `ghost` (or `none`); it is stored in
  that canonical order. An unknown name makes the whole value invalid (default used).

Check a file without starting the game:

```
python tools/validate_settings.py --exe spindle.exe settings.ini
```

The tool reads the schema from the executable itself (`spindle --write-default-settings FILE`), so it
can never disagree with the game.

## Key names

Keyboard bindings use SDL scancode names (`A`, `1`, `Space`, `Return`, `Left Ctrl`, `F2`, `Up`, ...).
Short aliases are accepted: `LCtrl`, `RCtrl`, `LShift`, `RShift`, `LAlt`, `RAlt`, `Esc`, `Enter`, `Del`,
`Ins`, `PgUp`, `PgDn`. Several keys can share an action: `fire = LCtrl, Space`. A key name that SDL
does not know is reported and the action falls back to its default key.

Controller buttons use SDL gamepad names: `a`, `b`, `x`, `y` (the four face buttons by position:
south, east, west, north, whatever the label on the pad), `back`, `guide`, `start`, `leftstick`,
`rightstick`, `leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`, `dpright`, `misc1`, ...
`gamepad_coin = none` disables coin insertion from controllers.

Controllers without an SDL mapping (unknown joysticks, many arcade encoders) work in a fallback mode:
buttons 1-4 fire, any other button is start, the hat navigates menus.

## One keyboard per station

With `[Input] per_keyboard = 1` spindle enables SDL's Windows raw keyboard input, so every physical
keyboard reports separately. Each station can then be tied to one keyboard with `p1_keyboard` ..
`p4_keyboard` (the device name; easiest to set in the service menu under keybinds, "P1 keyboard",
by pressing a key on that keyboard). A station whose keyboard is not connected accepts its keys from
any keyboard, so the cabinet keeps working.

## Reference

### [Display]

Video output. Changes made in the service menu apply immediately.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `resolution` | `auto` | text | auto (desktop resolution of display_index) or WIDTHxHEIGHT, e.g. 1920x1080 |
| `fullscreen` | `borderless` | borderless \| exclusive \| windowed | borderless desktop, exclusive mode switch, or windowed |
| `vsync` | `1` | 0 \| 1 | 1 = sync to the display refresh |
| `display_index` | `0` | 0..15 | which monitor to use, 0 = primary |
| `max_fps` | `0` | 0..1000 | frame cap when vsync = 0; 0 = uncapped (values 1..29 are raised to 30) |

### [Audio]

Volumes are percentages.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `master_volume` | `80` | 0..100 | overall volume |
| `sfx_volume` | `100` | 0..100 | gameplay sounds |
| `ui_volume` | `80` | 0..100 | coin, join and menu sounds |
| `music_volume` | `60` | 0..100 | the live music (0 = off): a calm loop on attract; in a match the bass, drums and hats come in as the humans' fire meters fill, the lead while one of them is on fire |
| `attract_sound` | `1` | 0 \| 1 | 1 = play sounds and music during attract mode |

### [Gameplay]

Round rules shared by every station. Speed values are uncalibrated: tune by feel.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `tries` | `3` | 1..9 | tries per player per match; a failed round costs one |
| `round_time_limit` | `45` | 10..300 | seconds; a player still playing at the limit fails the round |
| `rotation_speed_scale` | `1.0` | 0.5..2.0 | multiplies every circle rotation speed |
| `needle_speed_scale` | `1.0` | 0.5..2.0 | multiplies needle flight speed |
| `ready_delay` | `1.5` | 0.5..5.0 | seconds between rounds before every survivor starts together |
| `start_round` | `1` | 1..50 | round number a match starts at |
| `seed` | `0` | text | 0 = new random levels each match; any other number repeats the same levels |
| `powerups` | `1` | 0 \| 1 | 1 = powerup rings appear on the circles from round 3 (never in sudden death) |
| `powerup_types` | `shield, skip, slim, mend, heat, purge, swift, gold, ghost` | text | which rings may appear: shield, skip, slim, mend, heat, purge, swift, gold, ghost (comma list, or none); heat needs the fire meter |
| `fire_meter` | `1` | 0 \| 1 | 1 = quick needles fill a combo meter; full = on fire, needles burn through (never in sudden death) |
| `fire_combo` | `8` | 4..30 | combo points to catch fire; a needle is 1, a close call 2 |
| `fire_seconds` | `1.5` | 0.5..5.0 | seconds a player stays on fire |
| `fire_chain_window` | `1.0` | 0.5..5.0 | seconds allowed between needles before the combo drops |
| `round_end_screen` | `full` | full \| short \| off | full = scores are shot into the circles and the crown moves; short = the same, quicker; off = a plain pause |

### [Bots]

Computer players follow exactly the same rules as humans.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `singleplayer_bots` | `3` | 0..3 | bots joining a game with exactly one human |
| `multiplayer_bots` | `0` | 0..2 | bots joining a game with two or more humans (capped to free stations) |
| `difficulty` | `normal` | easy \| normal \| hard \| expert | bot skill in real games |
| `demo_difficulty` | `hard` | easy \| normal \| hard \| expert | bot skill in the attract demo |

### [Arcade]

Coin and cabinet behaviour.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `free_play` | `0` | 0 \| 1 | 1 = no credits needed |
| `credits_per_coin` | `1` | 1..9 | credits added per coin |
| `credits_per_game` | `1` | 1..9 | credits each player pays to join |
| `max_credits` | `99` | 1..99 | coins above this are still counted but add no credits |
| `vote_seconds` | `30` | 10..60 | seconds of the join and game mode vote screen after the first start (replaces `join_window`, which is now ignored) |
| `game_over_time` | `8` | 3..30 | seconds the results stay on screen |
| `exit_hold_seconds` | `2` | 0..10 | hold the exit key this long to quit; 0 = locked cabinet (service menu only) |
| `initials_seconds` | `15` | 10..30 | seconds a player has to shoot their initials into the high score table |

### [Feedback]

Vibration on phones and rumble on gamepads. Each station feels only its own moments: a needle sticking,
a close call, a fail, catching fire, a clear, the crown, letters shot into the high score table, the vote
cursor; every player feels the digits landing on the round end screen. CPUs and the attract demo never
buzz. Several touch players on one phone share its vibration (one buzz per kind of moment per tick).

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `vibration` | `1` | 0 \| 1 | 1 = the phone vibrates for touch players (Android through its vibrator, iPhone through its haptic engine) |
| `strength` | `70` | 0..100 | how strong vibration and rumble are; phones whose vibrator has no strength control only vary the length |
| `gamepad_rumble` | `1` | 0 \| 1 | 1 = a gamepad rumbles for its own station's moments (pads without rumble ignore it) |

Both appear in the service menu's volumes page and, on phones, behind the gear on attract; every change
buzzes once so it can be felt. On a phone without a vibrator spindle can use, "vibration" is greyed out.

### [Accessibility]

Easier to read, easier on the eyes.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `accent_colors` | `0` | 0 \| 1 | 1 = each station gets its own colour (P1 cyan, P2 pink, P3 green, P4 orange) |
| `reduce_flashes` | `0` | 0 \| 1 | 1 = softer fail flashes and no shaking |

### [Modes]

Game modes players can vote for (1 = on). With every mode off, battle stays on.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `battle` | `1` | 0 \| 1 | last one standing wins |
| `marathon` | `1` | 0 \| 1 | play till everyone is out - best score wins |
| `blitz` | `1` | 0 \| 1 | 90 seconds, no knock-outs - best score wins |
| `race` | `1` | 0 \| 1 | first to clear takes the round - first to 5 |
| `crown` | `1` | 0 \| 1 | hold the crown at round ends - 8 rounds |
| `sabotage` | `1` | 0 \| 1 | clears and close calls send junk to rivals |
| `teams` | `1` | 0 \| 1 | P1 + P2 against P3 + P4, shared tries |
| `chaos` | `1` | 0 \| 1 | every round rolls a mutator |
| `daily` | `1` | 0 \| 1 | today's levels, same for everyone |

### [Keys.Player1]

Keyboard bindings: SDL scancode names, several keys separated by commas.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `fire` | `LCtrl` | text | launch a needle |
| `start` | `1` | text | join / start |
| `left` | `W` | text | previous letter / mode |
| `right` | `D` | text | next letter / mode |

### [Keys.Player2]

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `fire` | `A` | text | launch a needle |
| `start` | `2` | text | join / start |
| `left` | `J` | text | previous letter / mode |
| `right` | `K` | text | next letter / mode |

### [Keys.Player3]

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `fire` | `RCtrl` | text | launch a needle |
| `start` | `3` | text | join / start |
| `left` | `Left` | text | previous letter / mode |
| `right` | `Right` | text | next letter / mode |

### [Keys.Player4]

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `fire` | `L` | text | launch a needle |
| `start` | `4` | text | join / start |
| `left` | `F` | text | previous letter / mode |
| `right` | `H` | text | next letter / mode |

### [Keys.System]

Cabinet and service keys.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `coin` | `5` | text | coin mech |
| `service_coin` | `9` | text | adds credits without counting a coin |
| `service` | `F2` | text | opens the service / test menu |
| `exit` | `Escape` | text | hold to quit (see exit_hold_seconds) |
| `menu_up` | `Up` | text | service menu navigation |
| `menu_down` | `Down` | text |  |
| `menu_left` | `Left` | text |  |
| `menu_right` | `Right` | text |  |
| `menu_select` | `Return` | text |  |
| `menu_back` | `Backspace` | text |  |

### [Input]

Controllers. Button names are SDL gamepad names (a, b, x, y, back, start, leftshoulder, ...).

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `assignment` | `auto` | auto \| fixed | auto: start on a free controller claims the lowest free station; fixed: p1_gamepad.. GUIDs |
| `per_keyboard` | `0` | 0 \| 1 | 1 = each station can be tied to its own physical keyboard (Windows raw input) |
| `disconnect_action` | `pause` | pause \| continue | what a match does when a player's controller disconnects |
| `gamepad_fire` | `a,b,x,y,leftshoulder,rightshoulder` | text | buttons that launch a needle |
| `gamepad_start` | `start` | text | button that joins / starts |
| `gamepad_coin` | `back` | text | button that inserts a coin, or none |
| `p1_gamepad` | (empty) | text | controller GUID per station, used when assignment = fixed |
| `p2_gamepad` | (empty) | text |  |
| `p3_gamepad` | (empty) | text |  |
| `p4_gamepad` | (empty) | text |  |
| `p1_keyboard` | (empty) | text | keyboard device name per station, used when per_keyboard = 1 |
| `p2_keyboard` | (empty) | text |  |
| `p3_keyboard` | (empty) | text |  |
| `p4_keyboard` | (empty) | text |  |

### [Assets]

Everything the game needs is built in. The only download is the optional controller database.

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `download` | `1` | 0 \| 1 | 1 = fetch gamecontrollerdb.txt when it is missing or damaged |
| `controller_db_url` | `https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/f510b74438d08d7dd55ed06673f473bd1cc28b85/gamecontrollerdb.txt` | text | must serve the exact file pinned inside spindle.exe |
| `download_timeout` | `10` | 1..60 | seconds |

### [System]

| Key | Default | Valid | Meaning |
|---|---|---|---|
| `log_level` | `info` | error \| warn \| info \| debug | detail written to logs/spindle.log |

`display_index` is accepted up to 15; at runtime a monitor that is not present falls back to the
primary monitor with a warning. `controller_db_url` may point at a mirror, but the file it serves must
be byte-identical to the pinned file (size and SHA-256 are compiled into the executable), otherwise it
is rejected and SDL's built-in controller mappings are used.
