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

### On a phone or tablet

There is no service key, so the menu hides behind a gesture: **hold one finger on the top-left corner
of the screen for 3 seconds** (the corner is the top-left 7 % x 12 % of the screen; taps there never
count as game input, so players cannot open it by accident). The menu then shows touch controls:

- tap a row to choose it; tap the chosen row again to open it or run it;
- on the chosen row, tap `<` (the left end of the value) or `>` (the right end) to change the value;
- the **back** button at the top right (or Android's back button) returns to the main page; on the
  main page it jumps to **save & exit**, which you then tap to save and close.

The same gesture works on any touch screen, including a touch monitor on a Windows cabinet.

## Player options (phones)

On a phone or tablet a gear sits in the top-right corner of the attract and insert-coin screens. Tapping
it opens a single **options** page for players: volume, game sounds, menu sounds, reduce flashes and
player colours. Leaving it (done, back, or the back button) saves. It never shows the operator pages.

## Pages

| Page | What it does |
|---|---|
| input test | Live state of every station's fire, start, left and right, every system key, every connected device (type, name, assigned station, buttons and axes) and the last input event. Hold back for 1 s or press service to leave. |
| controller assignment | Assignment mode (`auto` / `fixed`), what a disconnect does (`pause` / `continue`), the controller on each station (select, then press any button on the controller to assign it; left clears the station), and "remember these for fixed mode" to store the current controllers' GUIDs. |
| keybinds | Select an action (fire, start, left, right per station, then the cabinet keys), then press the new key. Also "keyboard per station" and, per station, the keyboard it listens to (select, then press any key on that keyboard; left resets it to any keyboard). |
| display test | Square grid, diagonals, the four station circles with moving needles, resolution, renderer, vsync and frame time. |
| video | Resolution (auto or any mode the monitor reports), fullscreen mode, vsync, reduce flashes, player colours, monitor, frame cap; "apply now" previews it. |
| sound test | Plays each sound (including the powerup, shield, ignite and burn sounds); shows the audio device or "unavailable (silent)". |
| volumes | Master, game sounds, coin and menu sounds (heard while changing), attract sound. |
| game settings | Tries, round time limit, rotation speed, needle speed, ready delay, start round, level seed, powerups on/off and each ring type (shield, skip, slim, mend, heat, purge, swift) on/off, the fire meter on/off, the combo to catch fire, seconds on fire, the combo chain window, and the round end screen (full / short / off). |
| coin settings | Free play, credits per coin, credits per player, max credits, vote time, game over time, initials time, exit hold (0 = locked cabinet). |
| bot settings | Bots with one player, bots with two or more players, bot difficulty, demo difficulty. |
| game modes | Vote time and each of the nine game modes on/off (with every mode off, battle stays on). |
| bookkeeping | Coins, service credits, singleplayer and multiplayer games, wins by players and by bots, play time, the last five winners; "clear bookkeeping" (press twice). |
| high scores | Every mode's tables (all time and today) with name, score, round and date; "clear high scores" (press twice). Saved in highscores.ini. |
| reset defaults | Loads all defaults into the menu (press twice); Save & Exit keeps them. |
| save & exit | Applies every change at once (input, audio, display, credits, bots) and writes settings.ini, keeping its comments. |
| exit without saving | Discards every change, including previewed video and volume changes. |
| shut down | Quits spindle (press twice). The only way out of a locked cabinet. |

The footer shows the settings health (number of invalid values in settings.ini, see the log), the
controller database state, and the most recent warning from the log.
