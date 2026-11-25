-- spindle bot script.
--
-- decide(obs, profile, memory) runs once per simulation tick (240 Hz) for each bot and returns true
-- to press the bot's fire button. That press is the bot's only output: it goes through exactly the
-- same station input queue, fire interval, tries and round timer as a human press.
--
-- obs is what the bot can see, delayed by profile.reaction seconds (the delay is applied by the game,
-- the script cannot see the present or the round's rotation program):
--   angle, speed, accel     rotor angle (radians), speed (rad/s, + = clockwise), acceleration
--   attached[1..attached_count]  relative angles of needles already in the circle
--   needles_left, in_flight  needles still to launch / currently flying (delayed)
--   flight_time             seconds from launch to attachment
--   min_separation          smallest legal angular gap between two needles (radians)
--   attach_angle            world angle where a needle attaches (bottom of the circle)
--   fire_ready              the bot's own fire interval has elapsed (not delayed)
--   round_time, delay, dt   seconds
-- memory is a per-round table the script may use freely; it is reset at the start of every round.
--
-- Profile values are uncalibrated: tune by feel.
--   reaction   seconds of observation delay (enforced by the game, 0..0.5)
--   compensate how much of its own delay the bot accounts for (0..1)
--   jitter     standard deviation of the bot's timing error per shot, in seconds
--   blunder    chance per shot of a gross timing mistake
--   clearance  preferred gap to the nearest needle, in multiples of min_separation
--   near_best  in a crowded circle, settle for this fraction of the widest gap available
--   rhythm     minimum seconds between two of its shots
--   model_accel  whether it extrapolates speed changes (reversals, surges)

profiles = {
  easy   = { reaction = 0.28, compensate = 0.55, jitter = 0.045, blunder = 0.06,  clearance = 2.4, near_best = 0.80, rhythm = 0.40, model_accel = false },
  normal = { reaction = 0.22, compensate = 0.85, jitter = 0.026, blunder = 0.025, clearance = 2.1, near_best = 0.88, rhythm = 0.28, model_accel = false },
  hard   = { reaction = 0.16, compensate = 1.00, jitter = 0.015, blunder = 0.010, clearance = 1.9, near_best = 0.92, rhythm = 0.20, model_accel = true },
  expert = { reaction = 0.12, compensate = 1.00, jitter = 0.008, blunder = 0.003, clearance = 1.8, near_best = 0.95, rhythm = 0.14, model_accel = true },
}

local PI = math.pi
local TWO_PI = 2 * math.pi

local function wrap(a)
  return (a + PI) % TWO_PI - PI
end

-- Approximately normal, mean 0, standard deviation 1 (sum of three uniforms).
local function gauss()
  return (math.random() + math.random() + math.random() - 1.5) * 2
end

local function sorted_copy(t, n, extra)
  local s = {}
  for i = 1, n do s[i] = t[i] % TWO_PI end
  for i = 1, #extra do s[#s + 1] = extra[i].angle % TWO_PI end
  table.sort(s)
  return s
end

-- Half of the widest gap between neighbouring needles: the best clearance any shot could get.
local function best_clearance(sorted)
  local n = #sorted
  if n == 0 then return PI end
  if n == 1 then return PI end
  local widest = sorted[1] + TWO_PI - sorted[n]
  for i = 2, n do
    local gap = sorted[i] - sorted[i - 1]
    if gap > widest then widest = gap end
  end
  return widest / 2
end

local function clearance_at(angle, sorted)
  local c = PI
  for i = 1, #sorted do
    local d = math.abs(wrap(angle - sorted[i]))
    if d < c then c = d end
  end
  return c
end

function decide(obs, p, mem)
  if not obs.fire_ready or obs.needles_left <= 0 then return false end

  -- Forget shots that must have landed and become visible by now.
  local pending = mem.pending or {}
  mem.pending = pending
  local horizon = obs.flight_time + obs.delay + 0.05
  local i = 1
  while i <= #pending do
    if obs.round_time - pending[i].t > horizon then
      table.remove(pending, i)
    else
      i = i + 1
    end
  end

  if mem.last_shot and obs.round_time - mem.last_shot < p.rhythm then return false end

  -- One timing error per shot, like a human pressing a little early or late.
  if mem.noise == nil then
    local n = gauss() * p.jitter
    if math.random() < p.blunder then
      n = n * 4 + ((math.random() < 0.5) and -1 or 1) * p.jitter * 3
    end
    mem.noise = n
  end

  local lead = obs.delay * p.compensate + obs.flight_time + mem.noise
  local predicted = obs.angle + obs.speed * lead
  if p.model_accel then predicted = predicted + 0.5 * obs.accel * lead * lead end
  local land = wrap(obs.attach_angle - predicted)

  local needles = sorted_copy(obs.attached, obs.attached_count, pending)
  local clearance = clearance_at(land, needles)
  local best = best_clearance(needles)
  local needed = math.min(p.clearance * obs.min_separation, best * p.near_best)
  if needed < obs.min_separation * 1.02 then needed = obs.min_separation * 1.02 end

  if clearance >= needed then
    pending[#pending + 1] = { angle = land, t = obs.round_time }
    mem.last_shot = obs.round_time
    mem.noise = nil
    return true
  end
  return false
end
