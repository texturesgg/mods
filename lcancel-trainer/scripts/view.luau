--!strict
-- What the player sees: a 20XX-style white or red flash on the fighter's model, plus the result
-- of each landing by the player's percent and the running rate.
local tgg = require("@tgg")
local landings = require("./landings")

-- Displayed frames a result stays up, the last FADE of them fading
local SHOW = 100
local FADE = 20
-- Displayed frames the fighter model flash lasts (strong hold then fade)
local FLASH_HIT = 12
local FLASH_MISS = 14
-- The gap above the percent
local HUD_ABOVE = 95

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

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

local function fresh()
	return {
		hits = { 0, 0, 0, 0 }, -- L-cancels
		tries = { 0, 0, 0, 0 }, -- landings that could be L-canceled
		age = { SHOW, SHOW, SHOW, SHOW }, -- displayed frames since the last result
		result = { 0, 0, 0, 0 },
		frames = { 0, 0, 0, 0 },
	}
end

local in_match = false
local view = fresh()

-- Colors take whole channels, as C's do: a float's alpha cut to a byte
local function alpha(scale: number, a: number): number
	return math.floor(tgg.f32(scale * a))
end

-- Applies the 20XX model flash for port p (1 to 4) for the next drawn frame
local function apply_flash(p: number)
	if not tgg.settings.flash or not landings.showing() then
		return
	end
	local player = tgg.player.at(p - 1)
	if player == nil or player.kind ~= tgg.player.HUMAN then
		return
	end
	local hit = view.result[p] == landings.CANCEL
	local total = if hit then FLASH_HIT else FLASH_MISS
	if view.age[p] >= total then
		return
	end
	local fighter = tgg.fighter.at(p - 1, false)
	if fighter == nil then
		return
	end
	local hold = if hit then 4 else 5
	local strength = if view.age[p] < hold
		then 1
		else tgg.f32((total - view.age[p]) / (total - hold))
	if hit then
		tgg.draw.fighter_tint(fighter, { r = 255, g = 255, b = 255, a = alpha(205, strength) })
	else
		tgg.draw.fighter_tint(fighter, { r = 255, g = 34, b = 34, a = alpha(215, strength) })
	end
end

-- A new match starts the counts over
tgg.on.advance(function()
	if not tgg.in_match() then
		in_match = false
	elseif not in_match then
		view = fresh()
		in_match = true
	end
end)

-- Each landing's result, once its frame is final
tgg.on.posted(function(value)
	local r: landings.Result = value
	local p = r.port + 1
	view.tries[p] += 1
	if r.result == landings.CANCEL then
		view.hits[p] += 1
	end
	view.result[p] = r.result
	view.frames[p] = r.frames
	view.age[p] = 0
	apply_flash(p)
	if landings.showing() then
		tgg.log(`P{p}: {landings.label(r.result, r.frames)}`)
	end
end)

tgg.on.frame(function()
	for p = 1, 4 do
		if view.age[p] < SHOW then
			view.age[p] += 1
		end
	end
end)

tgg.on.hud_draw(function()
	if not landings.showing() then
		return
	end
	for p = 1, 4 do
		apply_flash(p)
		local player = tgg.player.at(p - 1)
		if view.age[p] >= SHOW or player == nil or player.kind ~= tgg.player.HUMAN then
			continue
		end
		-- Where the game draws the percent: x its center, y the bottom of the HUD; below 0 without one
		local x, y = player.hud_x, player.hud_y
		if x < 0 then
			continue
		end
		local left = SHOW - view.age[p]
		local a = if left < FADE then tgg.f32(left / FADE) else 1
		local hit = view.result[p] == landings.CANCEL
		local text = landings.label(view.result[p], view.frames[p])
		local rate = ""
		if tgg.settings.rate and view.tries[p] > 0 then
			rate = `{view.hits[p]}/{view.tries[p]}  {view.hits[p] * 100 // view.tries[p]}%`
		end
		local w = tgg.f32(28 + math.max(tgg.draw.text_width(19, text), tgg.draw.text_width(15, rate)))
		local h = if rate ~= "" then 54 else 32
		local top = tgg.f32(tgg.f32(y - HUD_ABOVE) - h)
		local box_x = tgg.f32(x - w / 2)
		local border = if hit
			then { r = 116, g = 234, b = 144, a = alpha(165, a) }
			else { r = 255, g = 114, b = 102, a = alpha(170, a) }
		rounded_rect(box_x, top + 2, w, h, 7, { r = 0, g = 0, b = 0, a = alpha(110, a) }, nil, 0)
		rounded_rect(box_x, top, w, h, 7, { r = 12, g = 17, b = 42, a = alpha(224, a) }, border, 1.25)
		local good = { r = 124, g = 238, b = 148, a = alpha(255, a) }
		local bad = { r = 255, g = 118, b = 104, a = alpha(255, a) }
		tgg.draw.text(x, tgg.f32(top + 6), 19, if hit then good else bad, tgg.align.CENTER, text)
		if rate ~= "" then
			tgg.draw.rect(
				tgg.f32(box_x + 10),
				tgg.f32(top + 28),
				tgg.f32(w - 20),
				1,
				{ r = 128, g = 152, b = 230, a = alpha(68, a) }
			)
			tgg.draw.text(x, tgg.f32(top + 32), 15, { r = 220, g = 228, b = 246, a = alpha(255, a) }, tgg.align.CENTER, rate)
		end
	end
end)
