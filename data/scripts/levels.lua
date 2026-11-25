-- spindle level script (original level design for spindle).
--
-- level(round, limits) is called once per round. Every surviving station receives exactly the same
-- result, and one shared rotor drives all circles, so conditions are identical for everyone.
--
-- Units: angles in degrees (0 = right of the circle, increasing clockwise on screen), speeds in
-- degrees per second (positive = clockwise), times in seconds.
-- math.random is seeded by the game from (match seed, round): the same seed always rebuilds the same
-- rounds. The game validates the result (counts, finiteness, spacing, needles fitting around the
-- circle) and falls back to the built-in copy of this script if anything is out of bounds.
--
-- limits fields: min_separation_deg, max_total, max_launch, max_prepins, max_speed_deg, max_segments,
-- max_pickups, powerups (list of the powerup kinds the operator enabled; empty = no rings).
-- Tuning values below are uncalibrated: tune by feel.

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

-- Evenly spaced needles with a random phase. The jitter is bounded so that two neighbours can
-- never come closer than min_gap as long as 360 / count >= min_gap.
local function spaced_prepins(count, min_gap)
  local pins = {}
  if count <= 0 then return pins end
  local step = 360 / count
  local phase = math.random() * 360
  local jitter = math.max(0, (step - min_gap) * 0.35)
  for i = 0, count - 1 do
    pins[#pins + 1] = (phase + i * step + (math.random() * 2 - 1) * jitter) % 360
  end
  return pins
end

local function angle_gap(a, b)
  return math.abs((a - b + 180) % 360 - 180)
end

-- Powerup rings: a needle landing on a ring takes its power. They start at round 3 and sit well clear
-- of the pre-placed needles (so every ring can be taken): in a crowded circle they go into the
-- middle of the wider gaps. At most one ring per round is a mend.
local function place_pickups(round, prepins, limits)
  local kinds = limits.powerups
  if #kinds == 0 or round < 3 then return {} end
  local count = 1
  if round >= 9 then count = 2 end
  if round >= 16 then count = 3 end
  count = math.min(count, limits.max_pickups)
  local clear_of_pins = limits.min_separation_deg * 1.45
  local apart = limits.min_separation_deg * 2.4

  -- Candidate angles: anywhere when the circle is empty, otherwise inside gaps wide enough.
  local candidates = {}
  if #prepins == 0 then
    for _ = 1, 24 do candidates[#candidates + 1] = math.random() * 360 end
  else
    local sorted = {}
    for i, p in ipairs(prepins) do sorted[i] = p % 360 end
    table.sort(sorted)
    for i = 1, #sorted do
      local a = sorted[i]
      local b = (i < #sorted) and sorted[i + 1] or (sorted[1] + 360)
      local band = (b - a) / 2 - clear_of_pins
      if band >= 0 then
        candidates[#candidates + 1] = ((a + b) / 2 + (math.random() * 2 - 1) * band * 0.8) % 360
      end
    end
  end
  -- Shuffle so the rings land in different gaps every round.
  for i = #candidates, 2, -1 do
    local j = math.random(1, i)
    candidates[i], candidates[j] = candidates[j], candidates[i]
  end

  local out = {}
  local mend_used = false
  for _, a in ipairs(candidates) do
    if #out >= count then break end
    local ok = true
    for _, q in ipairs(out) do
      if angle_gap(a, q.angle) < apart then ok = false break end
    end
    if ok then
      local kind = kinds[math.random(1, #kinds)]
      if kind == "mend" then
        if mend_used then kind = nil else mend_used = true end
      end
      if kind then out[#out + 1] = { angle = a, kind = kind } end
    end
  end
  return out
end

-- Rotation patterns, unlocked as rounds go on.
local function steady(dir, base)
  return dir * base, { { speed = dir * base, ramp = 0, hold = 30 } }
end

local function surge(dir, base)
  return dir * base, {
    { speed = dir * base, ramp = 0.6, hold = 2.4 + math.random() },
    { speed = dir * base * 1.35, ramp = 0.8, hold = 1.2 },
  }
end

local function swing(dir, base, round)
  local hold = clamp(2.4 - (round - 8) * 0.15, 1.4, 2.4) + math.random() * 0.6
  return dir * base, {
    { speed = dir * base, ramp = 0.35, hold = hold },
    { speed = -dir * base, ramp = 0.35, hold = hold },
  }
end

local function breathe(dir, base)
  return dir * base * 0.45, {
    { speed = dir * base * 1.45, ramp = 1.1, hold = 0.5 },
    { speed = dir * base * 0.45, ramp = 1.0, hold = 0.4 },
  }
end

local function medley(dir, base)
  local segments = {}
  local d = dir
  for _ = 1, 4 + math.random(0, 2) do
    local kind = math.random(1, 4)
    if kind == 1 then
      segments[#segments + 1] = { speed = 0, ramp = 0.25, hold = 0.35 + math.random() * 0.3 }
    elseif kind == 2 then
      d = -d
      segments[#segments + 1] = { speed = d * base, ramp = 0.3, hold = 0.8 + math.random() * 1.2 }
    elseif kind == 3 then
      segments[#segments + 1] = { speed = d * base * 1.5, ramp = 0.6, hold = 0.6 }
    else
      segments[#segments + 1] = { speed = d * base * 0.6, ramp = 0.5, hold = 0.9 }
    end
  end
  segments[#segments + 1] = { speed = d * base, ramp = 0.4, hold = 1.0 }
  return 0, segments
end

function level(round, limits)
  local launch = clamp(6 + math.floor((round - 1) * 0.5), 6, math.min(18, limits.max_launch))
  local pre = clamp(1 + math.floor((round - 1) / 2), 1, math.min(14, limits.max_prepins))
  if launch + pre > limits.max_total then
    pre = math.max(0, limits.max_total - launch)
  end

  local base = clamp(75 + 5 * (round - 1), 75, 230)
  local dir = (math.random() < 0.5) and 1 or -1

  local initial, segments
  if round <= 3 then
    initial, segments = steady(dir, base)
  elseif round <= 7 then
    initial, segments = surge(dir, base)
  elseif round <= 12 then
    initial, segments = swing(dir, base, round)
  elseif round <= 18 then
    initial, segments = breathe(dir, base)
  else
    initial, segments = medley(dir, base)
  end

  -- Order matters for reproducibility: rotation, then pre-placed needles, then rings.
  local prepins = spaced_prepins(pre, limits.min_separation_deg * 1.3)
  return {
    launch = launch,
    prepins = prepins,
    rotation = { initial = initial, segments = segments },
    pickups = place_pickups(round, prepins, limits),
  }
end
