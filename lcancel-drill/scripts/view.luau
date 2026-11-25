--!strict
-- L-cancel drill: Practice > L-cancel. Training Mode with a clean screen (tgg.practice-stage),
-- starting on Final Destination with a stationary infinite-shield CPU so you practice real
-- shield-hitlag L-cancel timing. D-pad left toggles between Shield CPU and Empty stage;
-- D-pad right resets positions back to center.
local tgg = require("@tgg")

local ID = "tgg.lcancel-drill"
-- Wait until Training's "GO!" banner has faded (~60 frames) and P2 is in active Guard
local MIN_RESET_FRAME = 65
-- Displayed frames the controls hint / missing-trainer note stays up
local HINT_SHOW = 240
local MISSING_SHOW = 300
local FADE = 24

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

local practice = assert(tgg.mod("tgg.practice-stage"))

tgg.menu.add("practice/lcancel", {
	label = "L-cancel",
	description = "SHFFL on an infinite-shield CPU or empty stage with L-cancel timing.",
	action = function()
		practice.start(ID, "melee/final-destination", true, false)
	end,
})

local shield_on = true
local setup_frames = 0
local saved_reset = false
local hint_age = HINT_SHOW
local missing_age = MISSING_SHOW
local was_in_match = false

local function mine(): boolean
	return tgg.in_match() and practice.drill() == ID
end

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

tgg.on.frame(function()
	local in_match = mine()
	if in_match and not was_in_match then
		shield_on = true
		setup_frames = 0
		saved_reset = false
		practice.set_cpu_shield(true)
		hint_age = 0
		missing_age = if tgg.mod("tgg.lcancel-trainer") ~= nil then MISSING_SHOW else 0
	end
	was_in_match = in_match
	if hint_age < HINT_SHOW then
		hint_age += 1
	end
	if missing_age < MISSING_SHOW then
		missing_age += 1
	end
	if not in_match then
		return
	end

	-- Keep the center-stage snapshot once "GO!" is gone and P2 is in active Guard
	if not saved_reset then
		setup_frames += 1
		local p2 = tgg.fighter.at(1, false)
		if setup_frames >= MIN_RESET_FRAME and p2 ~= nil and p2.action == tgg.action.GUARD then
			practice.keep_reset(false)
			saved_reset = true
		end
		return
	end

	local input = pressed()
	if bit32.btest(input, tgg.button.LEFT) then
		shield_on = not shield_on
		practice.set_cpu_shield(shield_on)
		if shield_on then
			practice.reset(false)
			practice.say("Shield CPU: On")
		else
			practice.say("Empty stage")
		end
	elseif bit32.btest(input, tgg.button.RIGHT) then
		practice.reset(true)
	end
end)

tgg.on.hud_draw(function()
	if not mine() then
		return
	end

	if hint_age < HINT_SHOW then
		local left = HINT_SHOW - hint_age
		local a = if left < FADE then left / FADE else 1
		local sz = 14
		local icon = 15
		local label1 = "Shield CPU"
		local label2 = "Reset"
		local w1 = tgg.draw.text_width(sz, label1)
		local w2 = tgg.draw.text_width(sz, label2)
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
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, label1)
		cx += w1 + gap
		draw_dpad(cx + icon * 0.5, cy, icon, 1, a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, label2)
	end

	if missing_age < MISSING_SHOW then
		local left = MISSING_SHOW - missing_age
		local a = if left < FADE then left / FADE else 1
		local text = "Install L-cancel trainer to see your timing"
		local sz = 14
		local tw = tgg.draw.text_width(sz, text)
		local h = 26
		local w = 14 + 8 + 7 + tw + 14
		local x = 320 - w * 0.5
		local y = 44
		local cy = y + h * 0.5
		rounded_rect(x, y + 1.5, w, h, h * 0.5, { r = 0, g = 0, b = 0, a = alpha(105, a) }, nil, 0)
		rounded_rect(
			x,
			y,
			w,
			h,
			h * 0.5,
			{ r = 12, g = 17, b = 42, a = alpha(228, a) },
			{ r = 255, g = 210, b = 68, a = alpha(175, a) },
			1.2
		)
		tgg.draw.circle(x + 16, cy, 3.8, { r = 255, g = 210, b = 68, a = alpha(255, a) })
		tgg.draw.text(
			x + 26,
			cy - sz * 0.52,
			sz,
			{ r = 244, g = 247, b = 255, a = alpha(255, a) },
			tgg.align.LEFT,
			text
		)
	end
end)
