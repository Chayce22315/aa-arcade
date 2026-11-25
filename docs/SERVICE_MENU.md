# Service / test menu

Press the service key (default `F2`) in any state. The cabinet freezes (a match pauses, coins are not
taken) until the menu is closed.

## Controls

| Key | Default | Action |
|---|---|---|
| menu up / down | `Up` / `Down` | move |
| menu left / right | `Left` / `Right` | change the value |
| menu select | `Return` | open / confirm / change |
| menu back | `Backspace` | back to the main page (on the main page: jump to Save & Exit) |
| service | `F2` | sub-page: back to the main page; main page: save & exit |

Any controller also navigates: D-pad to move and change, south button (A / cross) to select, east
button (B / circle) for back. Cabinets with only player buttons can use P1 start to move down and
P1 fire to select / change.

## Pages

| Page | What it does |
|---|---|
| input test | Live state of every station's fire and start, every system key, every connected device (type, name, assigned station, buttons and axes) and the last input event. Hold back for 1 s or press service to leave. |
| controller assignment | Assignment mode (`auto` / `fixed`), what a disconnect does (`pause` / `continue`), the controller on each station (select, then press any button on the controller to assign it; left clears the station), and "remember these for fixed mode" to store the current controllers' GUIDs. |
| keybinds | Select an action, then press the new key. Also "keyboard per station" and, per station, the keyboard it listens to (select, then press any key on that keyboard; left resets it to any keyboard). |
| display test | Square grid, diagonals, the four station circles with moving needles, resolution, renderer, vsync and frame time. |
| video | Resolution (auto or any mode the monitor reports), fullscreen mode, vsync, monitor, frame cap; "apply now" previews it. |
| sound test | Plays each sound (including the powerup, shield, ignite and burn sounds); shows the audio device or "unavailable (silent)". |
| volumes | Master, game sounds, coin and menu sounds (heard while changing), attract sound. |
| game settings | Tries, round time limit, rotation speed, needle speed, ready delay, start round, level seed, powerups on/off and each ring type (shield, skip, slim, mend, heat, purge, swift) on/off, the fire meter on/off, the combo to catch fire, seconds on fire and the combo chain window. |
| coin settings | Free play, credits per coin, credits per player, max credits, join window, game over time, exit hold (0 = locked cabinet). |
| bot settings | Bots with one player, bots with two or more players, bot difficulty, demo difficulty. |
| bookkeeping | Coins, service credits, singleplayer and multiplayer games, wins by players and by bots, play time; "clear bookkeeping" (press twice). |
| reset defaults | Loads all defaults into the menu (press twice); Save & Exit keeps them. |
| save & exit | Applies every change at once (input, audio, display, credits, bots) and writes settings.ini, keeping its comments. |
| exit without saving | Discards every change, including previewed video and volume changes. |
| shut down | Quits spindle (press twice). The only way out of a locked cabinet. |

The footer shows the settings health (number of invalid values in settings.ini, see the log), the
controller database state, and the most recent warning from the log.
