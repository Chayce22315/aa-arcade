# spindle rules

## The circle

Each station has its own circle. A needle is fired upward from the launcher below the circle; when its
ball reaches the circle it sticks there and turns with the circle. Two balls must never touch. A round
can start with needles already in the circle. A station clears the round when its last needle is
placed. The numbered balls below the circle count down the needles left.

Needles in flight are checked continuously against every needle already in the circle (sub-stepped so
nothing moves more than a quarter of a ball radius between checks), and once more at the moment of
attachment: two balls at the needle length L with radius r touch when their angular gap is below
`2 * asin(r / L)`.

## One rotation for everyone

A round has exactly one rotor. It advances once per simulation tick (240 per second) and every
station reads that same state, so speed and direction are identical on all four circles at every tick.
The rotation can change during a round (reversals, surges, pauses) according to the level script, but
always for everyone at once. Every alive station receives the identical round definition (needles
already placed, needles to place, rotation program, seed) at the same moment.

Each station keeps its own placements, its own needles in flight and its own collision result.

## Modes

| Humans | Mode | Bots added |
|---|---|---|
| 1 | singleplayer | `[Bots] singleplayer_bots` (0-3) |
| 2-4 | multiplayer | `[Bots] multiplayer_bots` (0-2), capped so humans + bots <= 4 |

Bots take the lowest free stations. Both modes use exactly the same rules. Bots obey them too: a bot's
only output is a fire press on its own station, with the same fire interval, tries and timer.

## Rounds and tries

- Every surviving player starts each round together after `ready_delay`.
- Clearing a round costs nothing; the player waits (shown as `clear`) until the round ends.
- Failing a round (a touch, or still playing when `round_time_limit` runs out) costs one try; the
  player waits for the next round.
- At zero tries a player is eliminated for the rest of the match and cannot rejoin.
- A round ends when every player in it has cleared or failed. The round number then goes up for
  everyone.
- Nobody can join a match in progress, and there are no continues.

## Winning

The match ends when exactly one player is left; that player wins. Exactly one winner is always
produced. The cases, each covered by its own unit test (`tests/TestMatchRules.cpp`,
`tests/TestMatch.cpp`) and T2/T4/T5/T10 also by the end-to-end self-test:

| Case | Situation | Result |
|---|---|---|
| T1 | one player is left after a round | that player wins |
| T2 | everyone still alive is eliminated in the same round, failing on different ticks | the player who failed **last** wins: they survived longest |
| T3 | as T2, but the latest failures happened on the same tick | of those, the player with the most needles placed in that round wins |
| T4 | as T3, and the needle counts are equal too | those tied players (only them) play a **sudden-death** round, one try each |
| T5 | sudden death: exactly one player clears | that player wins |
| T6 | sudden death: two or more clear | another sudden-death round between the ones who cleared |
| T7 | sudden death: nobody clears | T2, then T3, then T4 are applied again among them |
| T8 | everyone ran out of time on the same tick | failure ticks are equal, so T3 then T4 decide |
| T9 | some players are eliminated while others still have tries | no tie-break; the match continues |
| T10 | single participant (1 human, 0 bots) | see below |
| T11 | three sudden-death rounds in a row end in a complete tie | most needles placed over the whole match, then most rounds cleared, then the lowest station number wins |

**T10, single participant.** With one human and no bots there is nobody to beat: the same round rules
apply, the match ends when that player runs out of tries, and the game-over screen shows the rounds
cleared. No winner is declared. With the default `singleplayer_bots = 3` a singleplayer game is a
four-station battle.

**T11, abandoned match guard.** Without it, players who walk away during a sudden death would time out
identically forever (T8 -> T4 -> T7 -> T4 ...) and the cabinet would never return to attract. After
three consecutive completely tied sudden-death rounds the match is settled by match totals instead.
Real play never reaches it: a tie needs identical failure ticks and identical needle counts three
times in a row.

## Powerups

From round 3, small rings sit on the circle: they are part of the round definition, so every
station gets the same rings in the same places, and they turn with the shared rotation. A needle
that lands on a ring (within three quarters of the minimum needle gap of its centre) takes its
power. Every effect applies to the station that took it, never to the others, so the shared rotation
and the identical round conditions are untouched.

| Ring | Mark | Effect |
|---|---|---|
| shield | ring in a ring | armed for the rest of the round: the next touch bounces off instead of failing; that needle goes back to the queue and must still be placed. A halo around the core shows it is armed. |
| skip | dash | one needle fewer to place this round (can clear the round on the spot) |
| slim | dot | this station's balls shrink to 70% for the rest of the round, so tighter gaps are legal |
| mend | plus | gives back one lost try, never above the starting tries; at full tries it does nothing |
| heat | small flame | sets the station on fire at once (see the fire meter). Only appears while the fire meter is on. |
| purge | cross | burns away the two pre-placed needles nearest to where the ring was taken (never a player's own needles) |
| swift | double chevron | this station's needles fly 1.6x faster for the rest of the round, so there is less to lead |

- Rounds 3-8 have one ring, 9-15 two, 16 on three (fewer when the circle is too crowded to place
  them clear of the pre-placed needles). At most one ring per round is a mend, and at most one is a heat.
- Rings never appear in sudden death: tie-breaks are decided by skill alone.
- Operators switch powerups on or off and choose the ring types (`[Gameplay] powerups`,
  `powerup_types`, or the service menu's game settings).
- Bots take rings by the same rule: they only get a ring by landing a needle on it.

## Fire meter

Each station has a flame in the bottom-left corner of its column: its fire meter. The flame is grey
when empty and fills with colour from the bottom as the combo grows.

- **Combo.** Every needle placed adds 1 to the combo; a **close call** (landing within 1.4x the
  minimum gap of the nearest needle) adds 2. The next needle has to land within the chain window
  (1 s by default) of the previous one, or the combo drops back to zero. A bar under the `xN` count
  shows how much of the window is left.
- **On fire.** When the combo reaches `fire_combo` (8 by default) the station is **on fire** for
  `fire_seconds` (1.5 s by default): the circle is ringed in flames and `on fire!` shows above it.
  Every needle *launched* while on fire is a fire needle (orange, with a flame trail): it burns away
  any needle it touches, pre-placed or the player's own, instead of failing the round. A fire needle
  launched on the last moment of the fire still burns when it lands. The meter empties on catching
  fire and does not fill while burning.
- **Losing the combo.** A touch breaks the chain, even one a shield bounces off. Failing the round
  loses the combo.
- **Banking.** Clearing a round banks the combo (at most one short of catching fire) into the next
  round; the chain clock restarts when that round starts, so the first needle still has to come
  within the window.
- **Fairness.** The meter and the fire are per station, like the rings: nobody else's circle changes.
  Needles burnt away still count as placed by the player who placed them (for T3 and the standings).
  Burning only removes needles; it never adds one or moves the round clock.
- **No fire in sudden death,** like the rings: the meter is hidden and does not fill.
- Operators set it with `[Gameplay] fire_meter`, `fire_combo`, `fire_seconds` and
  `fire_chain_window`, or in the service menu's game settings. The values are uncalibrated: tune by
  feel.
- Bots fill the meter by the same rule. When they catch fire they press fire at a human-like mashing
  rate (`mash` in the bot profiles), never faster than a person could.

## Standings

The game-over screen ranks players: winner first, then by the round they were eliminated in (later is
better), then by the tick they failed on (later is better), then by needles placed over the match.

## Touch (phones and tablets)

On a phone or tablet the screen is the cabinet: the four stations are its four columns.

- **Joining:** tap a column to join at that station (phones default to free play, so no coin is
  needed); tap your column again to start straight away, or wait for the join timer.
- **One player** (usually a phone against bots): tap anywhere to fire.
- **Several players** (around a tablet): each player taps their own column; taps on a bot's column do
  nothing. Every finger counts on its own, so players can tap at the same time.
- **Leaving the app** (home button, a call) pauses a running match; it shows `paused` and continues when
  a player taps (that tap does not launch a needle). Rules, timers and tie-breaks are unchanged.
- The top-left corner is reserved for the operator gesture (see docs/SERVICE_MENU.md).

## Timing and input

The simulation runs at a fixed 240 Hz. Every input event carries SDL's nanosecond timestamp and is
applied on the tick in which it happened, not on the frame in which it was read, so frame rate never
changes when a needle is launched. Touches are timestamped the same way. Speeds and timings are uncalibrated starting values and are meant
to be tuned by feel (`[Gameplay]` scales, `data/scripts/levels.lua`).
