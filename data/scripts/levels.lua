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
-- limits fields: min_separation_deg, max_total, max_launch, max_prepins, max_speed_deg, max_segments.
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

  return {
    launch = launch,
    prepins = spaced_prepins(pre, limits.min_separation_deg * 1.3),
    rotation = { initial = initial, segments = segments },
  }
end
