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

## Standings

The game-over screen ranks players: winner first, then by the round they were eliminated in (later is
better), then by the tick they failed on (later is better), then by needles placed over the match.

## Timing and input

The simulation runs at a fixed 240 Hz. Every input event carries SDL's nanosecond timestamp and is
applied on the tick in which it happened, not on the frame in which it was read, so frame rate never
changes when a needle is launched. Speeds and timings are uncalibrated starting values and are meant
to be tuned by feel (`[Gameplay]` scales, `data/scripts/levels.lua`).
