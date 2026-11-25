-- spindle bot script.
--
-- decide(obs, profile, memory) runs once per simulation tick (240 Hz) for each bot and returns true
-- to press the bot's fire button. That press is the bot's only output: it goes through exactly the
-- same station input queue, fire interval, tries, round timer and powerup rules as a human press.
--
-- obs is what the bot can see, delayed by profile.reaction seconds (the delay is applied by the game,
-- the script cannot see the present or the round's rotation program):
--   angle, speed, accel          rotor angle (radians), speed (rad/s, + = clockwise), acceleration
--   attached[1..attached_count]  relative angles of needles already in the circle
--   needles_left, in_flight      needles still to launch / currently flying (delayed)
--   flight_time                  seconds from launch to attachment
--   min_separation               smallest legal angular gap between two needles (radians; smaller while slim)
--   attach_angle                 world angle where a needle attaches (bottom of the circle)
--   fire_ready                   the bot's own fire interval has elapsed (not delayed)
--   pickups[1..pickup_count]     relative angles of powerup rings still on the circle
--   claim_angle                  a needle landing within this angle of a ring takes it
--   revision                     changes whenever the needles in the circle change (placed or burnt)
--   fire_enabled                 the fire meter is on this round
--   combo, combo_target          fire-meter combo now / the combo that sets the station on fire
--   chain_left                   seconds before the combo drops if no needle lands (0 = no chain)
--   close_call                   a needle landing nearer than this to another counts double (radians)
--   on_fire, fire_left           the station is on fire / seconds of fire left; needles launched
--                                while on fire burn through whatever they touch
--   round_time, time_left        seconds into the round / until the round timer runs out
--   delay, dt                    seconds
-- memory is a per-round table the script may use freely; it is reset at the start of every round.
--
-- How a shot is chosen:
--   1. Every tick the bot notes which relative angles its next press would actually hit. A circle that
--      swings back and forth may never bring some gaps round (often the best gap sits right at the
--      turnaround), so only gaps its own aim has passed recently count as reachable.
--   2. Its safety margin comes from its own skill: timing spread times speed, plus whatever it does
--      not model (part of its reaction delay, and acceleration for profiles without model_accel).
--   3. It starts out wanting a comfortable spot near the best reachable gap and, the longer it waits,
--      settles for anything its skill can make, down to just above touching. It never waits for a
--      gap that is not coming round.
--
-- Profile values are uncalibrated: tune by feel.
--   reaction     seconds of observation delay (enforced by the game, 0..0.5)
--   compensate   how much of its own delay the bot accounts for (0..1)
--   jitter       standard deviation of the bot's timing error per shot, in seconds
--   blunder      chance per shot of a gross timing mistake
--   clearance    preferred gap to the nearest needle, in multiples of min_separation
--   near_best    when crowded, aim for this fraction of the best reachable gap
--   rhythm       minimum seconds between two of its shots
--   patience     seconds of waiting until it settles for any spot its skill allows
--   window       seconds a passing angle counts as reachable
--   greed        chance per shot that it goes for a powerup ring passing by
--   blaze        chance, each time it catches fire, that it mashes fire while burning
--   mash         seconds between its presses while it mashes (a human-like rate, not the game's limit)
--   hurry        with this many seconds of combo chain left it settles for any safe spot (0 = never)
--   model_accel  whether it extrapolates speed changes (reversals, surges)

profiles = {
  easy   = { reaction = 0.28, compensate = 0.55, jitter = 0.045, blunder = 0.06,  clearance = 2.4, near_best = 0.80, rhythm = 0.40, patience = 3.0, window = 3.0, greed = 0.35, blaze = 0.5, mash = 0.20, hurry = 0.0,  model_accel = false },
  normal = { reaction = 0.22, compensate = 0.85, jitter = 0.026, blunder = 0.025, clearance = 2.1, near_best = 0.88, rhythm = 0.28, patience = 2.2, window = 3.0, greed = 0.6,  blaze = 0.8, mash = 0.15, hurry = 0.35, model_accel = false },
  hard   = { reaction = 0.16, compensate = 1.00, jitter = 0.015, blunder = 0.010, clearance = 1.9, near_best = 0.92, rhythm = 0.20, patience = 1.6, window = 3.0, greed = 0.85, blaze = 1.0, mash = 0.12, hurry = 0.5,  model_accel = true },
  expert = { reaction = 0.12, compensate = 1.00, jitter = 0.008, blunder = 0.003, clearance = 1.8, near_best = 0.95, rhythm = 0.14, patience = 1.2, window = 3.0, greed = 1.0,  blaze = 1.0, mash = 0.10, hurry = 0.6,  model_accel = true },
}

local PI = math.pi
local TWO_PI = 2 * math.pi
local BINS = 180               -- reachability map resolution: 2 degree bins
local BIN = TWO_PI / BINS
local HALF_BINS = BINS // 2

local function wrap(a)
  return (a + PI) % TWO_PI - PI
end

-- Approximately normal, mean 0, standard deviation 1 (sum of three uniforms).
local function gauss()
  return (math.random() + math.random() + math.random() - 1.5) * 2
end

-- Needles in the circle plus the bot's own needles still in flight, sorted in [0, 2pi).
local function needle_set(obs, pending)
  local s = {}
  for i = 1, obs.attached_count do s[i] = obs.attached[i] % TWO_PI end
  for i = 1, #pending do s[#s + 1] = pending[i].angle % TWO_PI end
  table.sort(s)
  return s
end

-- Distance to the nearest needle at the centre of every bin, in one sweep over the sorted set.
local function clearance_map(s)
  local map = {}
  local n = #s
  if n == 0 then
    for b = 0, BINS - 1 do map[b] = PI end
    return map
  end
  local j = 1
  for b = 0, BINS - 1 do
    local x = (b + 0.5) * BIN
    while j <= n and s[j] < x do j = j + 1 end
    local after = (j <= n) and s[j] or (s[1] + TWO_PI)
    local before = (j > 1) and s[j - 1] or (s[n] - TWO_PI)
    map[b] = math.min(after - x, x - before)
  end
  return map
end

-- Half of the widest gap: the best clearance anywhere on the circle, reachable or not.
local function best_clearance(s)
  local n = #s
  if n <= 1 then return PI end
  local widest = s[1] + TWO_PI - s[n]
  for i = 2, n do
    local gap = s[i] - s[i - 1]
    if gap > widest then widest = gap end
  end
  return widest / 2
end

local function clearance_at(angle, s)
  local c = PI
  for i = 1, #s do
    local d = math.abs(wrap(angle - s[i]))
    if d < c then c = d end
  end
  return c
end

-- Record that this relative angle is under the launcher now (filling any bins skipped since the last tick).
local function mark_seen(mem, land, t)
  local seen = mem.seen
  local b = math.floor((land % TWO_PI) / BIN) % BINS
  local prev = mem.prev_bin
  if prev ~= nil and prev ~= b then
    local d = (b - prev + HALF_BINS) % BINS - HALF_BINS
    if math.abs(d) <= 24 then
      local step = (d > 0) and 1 or -1
      local k = prev
      while k ~= b do
        k = (k + step) % BINS
        seen[k] = t
      end
    end
  end
  seen[b] = t
  mem.prev_bin = b
end

-- Best clearance among bins seen within the window, and how many bins that is.
local function reachable_best(mem, map, t, window)
  local best, covered = nil, 0
  local seen = mem.seen
  for b = 0, BINS - 1 do
    local when = seen[b]
    if when ~= nil and t - when <= window then
      covered = covered + 1
      local c = map[b]
      if best == nil or c > best then best = c end
    end
  end
  return best, covered
end

local function shoot(mem, land, t)
  mem.pending[#mem.pending + 1] = { angle = land, t = t }
  mem.last_shot = t
  mem.noise = nil
  return true
end

function decide(obs, p, mem)
  local t = obs.round_time
  mem.seen = mem.seen or {}
  mem.pending = mem.pending or {}
  local pending = mem.pending

  -- Forget own shots that must have landed and become visible by now.
  local horizon = obs.flight_time + obs.delay + 0.05
  local i = 1
  while i <= #pending do
    if t - pending[i].t > horizon then table.remove(pending, i) else i = i + 1 end
  end

  -- One timing error per shot, like a human pressing a little early or late. It is drawn before the
  -- bot starts waiting, so the reachability map below records where this press would really land.
  if mem.noise == nil then
    local n = gauss() * p.jitter
    if math.random() < p.blunder then
      n = n * 4 + ((math.random() < 0.5) and -1 or 1) * p.jitter * 3
    end
    mem.noise = n
    mem.greedy = math.random() < p.greed
    mem.seen = {}      -- a new press lands differently: map reachability afresh
    mem.prev_bin = nil
  end

  -- Where a needle fired now would land, as the bot judges it (its timing error included).
  local lead0 = obs.delay * p.compensate + obs.flight_time
  local lead = lead0 + mem.noise
  local predicted = obs.angle + obs.speed * lead
  if p.model_accel then predicted = predicted + 0.5 * obs.accel * lead * lead end
  local land = wrap(obs.attach_angle - predicted)
  mark_seen(mem, land, t)

  -- Catching fire: decide once per fire whether to make the most of it.
  local burning = obs.on_fire and (obs.fire_left - obs.delay * p.compensate) > obs.dt * 2
  if obs.on_fire and not mem.was_on_fire then mem.blazing = math.random() < p.blaze end
  mem.was_on_fire = obs.on_fire

  if not obs.fire_ready or obs.needles_left <= 0 then return false end

  -- On fire every needle burns through whatever it touches, so there is nothing to wait for.
  if burning and mem.blazing then
    if mem.last_shot and t - mem.last_shot < p.mash then return false end
    return shoot(mem, land, t)
  end

  if mem.last_shot and t - mem.last_shot < p.rhythm then return false end

  -- Needle set and per-bin clearances, rebuilt only when the set changes.
  local sig = obs.revision * 64 + #pending
  if mem.sig ~= sig then
    mem.sig = sig
    mem.needles = needle_set(obs, pending)
    mem.map = clearance_map(mem.needles)
    mem.global_best = best_clearance(mem.needles)
  end
  local c = clearance_at(land, mem.needles)

  -- Margins from the bot's own skill.
  local ms = obs.min_separation
  local floor = ms * 1.02
  local w = math.abs(obs.speed)
  local unmodelled = (1 - p.compensate) * obs.delay * w
  if not p.model_accel then unmodelled = unmodelled + 0.5 * math.abs(obs.accel) * lead0 * lead0 end
  local safe = floor + 2 * p.jitter * w + unmodelled

  -- A powerup ring passing under the launcher is worth a safe shot.
  if mem.greedy and obs.pickup_count > 0 and c >= safe then
    for k = 1, obs.pickup_count do
      if math.abs(wrap(land - obs.pickups[k])) <= obs.claim_angle * 0.6 then
        return shoot(mem, land, t)
      end
    end
  end

  -- What is worth waiting for: the best gap that actually comes round (until enough of the circle has
  -- been seen, assume all of it can).
  local reach, covered = reachable_best(mem, mem.map, t, p.window)
  local basis = reach or mem.global_best
  if covered < BINS // 3 then basis = math.max(reach or 0, mem.global_best) end

  local ideal = math.max(safe, math.min(p.clearance * ms, basis * p.near_best))
  local settle = math.max(floor, math.min(safe, basis - BIN * 0.5 - w * obs.dt * 2))
  local waited = t - (mem.last_shot or 0)
  local k = math.min(1, waited / p.patience)
  if obs.time_left < 5 then k = 1 end
  -- Keep the fire meter's chain alive when it is about to drop.
  if obs.fire_enabled and not obs.on_fire and obs.combo > 0 and p.hurry > 0 and obs.chain_left - obs.delay < p.hurry then k = 1 end
  local need = ideal + (settle - ideal) * k

  if c >= need then return shoot(mem, land, t) end
  return false
end
