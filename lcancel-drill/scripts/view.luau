--!strict
-- L-cancel drill: Practice > L-cancel. Training Mode with nobody else on the stage and a clean
-- screen (tgg.practice-stage), starting on Final Destination, for SHFFLing on your own. The
-- feedback is tgg.lcancel-trainer's: with it installed, every aerial landing says how the
-- L-cancel went. Without it, the drill says so once.
local tgg = require("@tgg")

local ID = "tgg.lcancel-drill"
-- Displayed frames the missing-trainer line shows, the last FADE of them fading
local SHOW = 300
local FADE = 24

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

local practice = assert(tgg.mod("tgg.practice-stage"))

tgg.menu.add("practice/lcancel", {
	label = "L-cancel",
	description = "An empty stage that shows your L-cancel timing after each aerial.",
	action = function()
		practice.start(ID, "melee/final-destination", false, false)
	end,
})

local shown = SHOW
local was_in_match = false

local function mine(): boolean
	return tgg.in_match() and practice.drill() == ID
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

tgg.on.frame(function()
	local in_match = mine()
	if in_match and not was_in_match then
		shown = if tgg.mod("tgg.lcancel-trainer") ~= nil then SHOW else 0
	end
	was_in_match = in_match
	if shown < SHOW then
		shown += 1
	end
end)

tgg.on.hud_draw(function()
	if shown >= SHOW or not mine() then
		return
	end
	local left = SHOW - shown
	local a = if left < FADE then left / FADE else 1
	local text = "Install L-cancel trainer to see your timing"
	local sz = 15
	local tw = tgg.draw.text_width(sz, text)
	local h = 28
	local w = 14 + 8 + 7 + tw + 14
	local x = 320 - w * 0.5
	local y = 14
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
end)
