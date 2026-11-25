--!strict
-- Ledge dash drill: Practice > Ledge dash. Training Mode with nobody else and a clean screen
-- (tgg.practice-stage); puts you straight onto the left ledge at match start and keeps that as
-- your reset point, so every rep is just the ledge dash itself.
--
-- A try starts when the fighter leaves the ledge and is over when they're standing on the stage
-- and can act, when they're KO'd, or after TRY_MAX displayed frames. A short beat later it puts
-- you right back on the ledge (ledgedash-trainer keeps the previous card readable across resets).
-- D-pad left swaps between left and right ledges; D-pad right resets immediately.
local tgg = require("@tgg")

local ID = "tgg.ledgedash-drill"
-- Displayed frames a try may take, and the beat before resetting (fast enough to keep flow;
-- ledgedash-trainer's card stays visible across snapshot resets)
local TRY_MAX = 120
local BEAT_LAND = 15
local BEAT_KO = 8
local WARP_TIMEOUT = 24
-- Displayed frames the controls hint / missing-trainer note stays up
local HINT_SHOW = 240
local HINT_FADE = 24
local MISSING_SHOW = 300

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

local practice = assert(tgg.mod("tgg.practice-stage"))

tgg.menu.add("practice/ledgedash", {
	label = "Ledge dash",
	description = "Start on the ledge. Swap sides or reset fast between reps.",
	action = function()
		practice.start(ID, "melee/final-destination", false, false)
	end,
})

local BOOT, WARPING, WAITING, HANGING, TRYING, DONE = 0, 1, 2, 3, 4, 5
local phase = BOOT
local side = -1 -- -1 left ledge, +1 right ledge
local frames = 0 -- displayed frames in this phase
local match_frames = 0 -- displayed frames since match started
local reset_clean = false -- true once the saved reset point is past Training's "GO!" banner
local beat = BEAT_LAND
local hint_shown = false
local hint_age = HINT_SHOW
local missing_age = MISSING_SHOW
local was_mine = false

local function on_ledge(action: number): boolean
	return action == tgg.action.CLIFF_CATCH or action == tgg.action.CLIFF_WAIT
end

local function ko(action: number): boolean
	return action >= tgg.action.DEAD_DOWN and action <= tgg.action.REBIRTH
end

-- Standing on the stage, past any landing lag
local function standing(fighter: tgg.Fighter): boolean
	local action = fighter.action
	return fighter.grounded
		and action ~= tgg.action.LANDING_FALL_SPECIAL
		and action ~= tgg.action.LANDING
		and not on_ledge(action)
end

-- What went down on any controller since the last displayed frame
local function pressed(): number
	local any = 0
	for port = 0, 3 do
		local pad = tgg.input.controller(port)
		if pad then
			any = bit32.bor(any, pad.pressed)
		end
	end
	return any
end

local function alpha(scale: number, a: number): number
	return math.floor(scale * a)
end

local function rounded_rect(
	x: number,
	y: number,
	w: number,
	h: number,
	r: number,
	fill: tgg.Color,
	border: tgg.Color?,
	border_w: number
)
	local rr = math.clamp(r, 0, math.min(w, h) * 0.5)
	local pts = table.create(32)
	local n = 0
	for i = 1, 4 do
		n += 1
		pts[n] = (x + w - rr) + rr * SIN4[i]
		n += 1
		pts[n] = (y + rr) - rr * COS4[i]
	end
	for i = 1, 4 do
		n += 1
		pts[n] = (x + w - rr) + rr * COS4[i]
		n += 1
		pts[n] = (y + h - rr) + rr * SIN4[i]
	end
	for i = 1, 4 do
		n += 1
		pts[n] = (x + rr) - rr * SIN4[i]
		n += 1
		pts[n] = (y + h - rr) + rr * COS4[i]
	end
	for i = 1, 4 do
		n += 1
		pts[n] = (x + rr) - rr * COS4[i]
		n += 1
		pts[n] = (y + rr) - rr * SIN4[i]
	end
	tgg.draw.polygon(pts, fill)
	if border and border_w > 0 then
		tgg.draw.polygon_outline(pts, border_w, border)
	end
end

local function draw_dpad(cx: number, cy: number, size: number, dir: number, a: number)
	local r = size * 0.5
	local arm = size * 0.28
	local span = size * 0.72
	local half = span * 0.5
	tgg.draw.circle(cx, cy, r, { r = 48, g = 56, b = 86, a = alpha(235, a) })
	tgg.draw.circle_outline(cx, cy, r - 0.5, 1, { r = 106, g = 134, b = 226, a = alpha(120, a) })
	tgg.draw.rect(cx - arm * 0.5, cy - half, arm, span, { r = 112, g = 122, b = 154, a = alpha(255, a) })
	tgg.draw.rect(cx - half, cy - arm * 0.5, span, arm, { r = 112, g = 122, b = 154, a = alpha(255, a) })
	local lit = { r = 236, g = 240, b = 252, a = alpha(255, a) }
	if dir < 0 then
		tgg.draw.rect(cx - half, cy - arm * 0.5, half + arm * 0.2, arm, lit)
	else
		tgg.draw.rect(cx - arm * 0.2, cy - arm * 0.5, half + arm * 0.2, arm, lit)
	end
end

local function enter(next: number)
	phase = next
	frames = 0
end

local function mine(): boolean
	return tgg.in_match() and practice.drill() == ID
end

tgg.on.frame(function()
	local fighter = if tgg.in_match() then tgg.fighter.at(0, false) else nil
	local active = practice.drill() == ID and fighter ~= nil
	if active and not was_mine then
		side = -1
		match_frames = 0
		reset_clean = false
		hint_shown = false
		hint_age = HINT_SHOW
		missing_age = if tgg.mod("tgg.ledgedash-trainer") ~= nil then MISSING_SHOW else 0
		enter(BOOT)
	end
	was_mine = active
	if hint_age < HINT_SHOW then
		hint_age += 1
	end
	if missing_age < MISSING_SHOW then
		missing_age += 1
	end
	if not active or fighter == nil then
		enter(BOOT)
		return
	end

	local action = fighter.action
	local input = pressed()
	frames += 1
	match_frames += 1

	-- D-pad left swaps between left and right ledges at any time
	if phase ~= BOOT and bit32.btest(input, tgg.button.LEFT) then
		side = -side
		if practice.put_on_ledge(side) then
			practice.say(if side < 0 then "Left ledge" else "Right ledge")
			enter(WARPING)
			return
		end
	end

	-- D-pad right resets immediately to the current ledge
	if bit32.btest(input, tgg.button.RIGHT) and phase ~= BOOT and phase ~= WAITING and practice.reset(true) then
		enter(HANGING)
		return
	end

	if phase == BOOT then
		if practice.put_on_ledge(side) then
			enter(WARPING)
		end
	elseif phase == WARPING then
		-- Wait until the queued tick puts P1 in CLIFF_CATCH on the requested side
		local wanted_facing = if side < 0 then 1 else -1
		if action == tgg.action.CLIFF_CATCH and fighter.facing == wanted_facing then
			practice.keep_reset(false)
			reset_clean = match_frames >= 65
			if not hint_shown then
				hint_shown = true
				hint_age = 0
			end
			enter(HANGING)
		elseif frames >= WARP_TIMEOUT then
			-- Stage has no ledges (e.g. Flat Zone): fall back gracefully
			enter(WAITING)
		end
	elseif phase == WAITING then
		if on_ledge(action) then
			practice.keep_reset(true)
			reset_clean = match_frames >= 65
			if not hint_shown then
				hint_shown = true
				hint_age = 0
			end
			enter(HANGING)
		end
	elseif phase == HANGING then
		if not on_ledge(action) then
			enter(TRYING)
		end
	elseif phase == TRYING then
		if standing(fighter) then
			beat = BEAT_LAND
			enter(DONE)
		elseif ko(action) or frames >= TRY_MAX then
			beat = BEAT_KO
			enter(DONE)
		end
	elseif phase == DONE then
		if frames >= beat then
			if not reset_clean and match_frames >= 65 and practice.put_on_ledge(side) then
				-- Replace the frame-1 reset point once "GO!" is gone so resets never replay "GO!"
				enter(WARPING)
			elseif practice.reset(false) then
				enter(HANGING)
			end
		end
	end
end)

tgg.on.hud_draw(function()
	if not mine() then
		return
	end

	if phase == WAITING then
		local text = "Grab the ledge to set your reset point"
		local sz = 15
		local tw = tgg.draw.text_width(sz, text)
		local h = 28
		local w = 14 + 8 + 7 + tw + 14
		local x = 320 - w * 0.5
		local y = 10
		local cy = y + h * 0.5
		rounded_rect(x, y + 1.5, w, h, h * 0.5, { r = 0, g = 0, b = 0, a = 110 }, nil, 0)
		rounded_rect(
			x,
			y,
			w,
			h,
			h * 0.5,
			{ r = 9, g = 13, b = 32, a = 228 },
			{ r = 255, g = 210, b = 68, a = 185 },
			1.2
		)
		tgg.draw.circle(x + 16, cy, 3.8, { r = 255, g = 210, b = 68, a = 255 })
		tgg.draw.text(x + 26, cy - sz * 0.52, sz, { r = 244, g = 247, b = 255, a = 255 }, tgg.align.LEFT, text)
	elseif hint_age < HINT_SHOW then
		local left = HINT_SHOW - hint_age
		local a = if left < HINT_FADE then left / HINT_FADE else 1
		local sz = 14
		local icon = 15
		local w1 = tgg.draw.text_width(sz, "Swap ledge")
		local w2 = tgg.draw.text_width(sz, "Reset")
		local gap = 18
		local igap = 6
		local w = icon + igap + w1 + gap + icon + igap + w2 + 26
		local h = 26
		local x = 320 - w * 0.5
		local y = 10
		local cy = y + h * 0.5
		rounded_rect(x, y + 1.5, w, h, h * 0.5, { r = 0, g = 0, b = 0, a = alpha(100, a) }, nil, 0)
		rounded_rect(
			x,
			y,
			w,
			h,
			h * 0.5,
			{ r = 9, g = 13, b = 32, a = alpha(224, a) },
			{ r = 106, g = 134, b = 226, a = alpha(125, a) },
			1.1
		)
		local cx = x + 13
		draw_dpad(cx + icon * 0.5, cy, icon, -1, a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Swap ledge")
		cx += w1 + gap
		draw_dpad(cx + icon * 0.5, cy, icon, 1, a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Reset")
	end

	if missing_age < MISSING_SHOW then
		local left = MISSING_SHOW - missing_age
		local a = if left < HINT_FADE then left / HINT_FADE else 1
		local text = "Install Ledge dash trainer to see GALINT timing"
		local sz = 14
		local tw = tgg.draw.text_width(sz, text)
		local h = 26
		local w = tw + 26
		local x = 320 - w * 0.5
		local y = 44
		local cy = y + h * 0.5
		rounded_rect(
			x,
			y,
			w,
			h,
			h * 0.5,
			{ r = 12, g = 17, b = 42, a = alpha(220, a) },
			{ r = 255, g = 210, b = 68, a = alpha(150, a) },
			1.1
		)
		tgg.draw.text(320, cy - sz * 0.52, sz, { r = 244, g = 247, b = 255, a = alpha(255, a) }, tgg.align.CENTER, text)
	end
end)
