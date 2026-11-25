--!strict
-- What the player sees: a 20XX-style flash on the fighter's model plus each ledge dash's
-- GALINT and frame breakdown in a rounded space-navy card above the player's percent.
local tgg = require("@tgg")
local ledges = require("./ledges")

-- Displayed frames a result stays up, the last FADE of them fading
local SHOW = 150
local FADE = 20
-- Displayed frames the fighter model flash lasts (strong hold then fade)
local FLASH_GOOD = 12
local FLASH_BAD = 14
-- How far above the HUD the panel sits
local HUD_ABOVE = 95

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

local function fresh()
	return {
		age = { SHOW, SHOW, SHOW, SHOW }, -- displayed frames since the last result
		best = { 0, 0, 0, 0 }, -- best GALINT frames this match, per port
		shown = { ledges.result(), ledges.result(), ledges.result(), ledges.result() },
	}
end

local in_match = false
local view = fresh()

-- Colors take whole channels, as C's do: a float's alpha cut to a byte
local function alpha(scale: number, a: number): number
	return math.floor(tgg.f32(scale * a))
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
	local max_r = math.min(w, h) * 0.5
	local rr = math.clamp(r, 0, max_r)
	if rr < 0.75 then
		tgg.draw.rect(x, y, w, h, fill)
		if border and border_w > 0 then
			tgg.draw.rect_outline(x, y, w, h, border_w, border)
		end
		return
	end
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

-- Applies the 20XX-style model flash for port p (1 to 4) for the next drawn frame
local function apply_flash(p: number)
	if not tgg.settings.flash or not ledges.showing() then
		return
	end
	local player = tgg.player.at(p - 1)
	if player == nil or player.kind ~= tgg.player.HUMAN then
		return
	end
	local r = view.shown[p]
	local good = r.kind == ledges.DASH and r.left > 0
	local neutral = r.kind == ledges.DASH and r.left == 0
	local total = if good then FLASH_GOOD else FLASH_BAD
	if view.age[p] >= total then
		return
	end
	local fighter = tgg.fighter.at(p - 1, false)
	if fighter == nil then
		return
	end
	local hold = if good then 4 else 5
	local strength = if view.age[p] < hold
		then 1
		else tgg.f32((total - view.age[p]) / (total - hold))
	if good then
		tgg.draw.fighter_tint(fighter, { r = 136, g = 232, b = 255, a = alpha(205, strength) })
	elseif neutral then
		tgg.draw.fighter_tint(fighter, { r = 255, g = 210, b = 68, a = alpha(195, strength) })
	else
		tgg.draw.fighter_tint(fighter, { r = 255, g = 42, b = 42, a = alpha(215, strength) })
	end
end

-- A new match starts over
tgg.on.advance(function()
	if not tgg.in_match() then
		in_match = false
	elseif not in_match then
		view = fresh()
		in_match = true
	end
end)

-- Each attempt's result, once its frame is final
tgg.on.posted(function(value)
	local r: ledges.Result = value
	local p = r.port + 1
	view.shown[p] = r
	view.age[p] = 0
	if r.kind == ledges.DASH and r.left > view.best[p] then
		view.best[p] = r.left
	end
	apply_flash(p)
	if ledges.showing() then
		local a, b = ledges.describe(r)
		tgg.log(`P{p}: {a}{if b ~= "" then ", " else ""}{b}`)
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
	if not ledges.showing() then
		return
	end
	local bounds = tgg.draw.bounds()
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
		local r = view.shown[p]
		local left = SHOW - view.age[p]
		local a = if left < FADE then tgg.f32(left / FADE) else 1
		local line1, line2 = ledges.describe(r)
		if r.kind == ledges.DASH and view.best[p] > 0 then
			line2 = `{line2}   Best {view.best[p]}f`
		end
		local w = tgg.f32(28 + math.max(tgg.draw.text_width(19, line1), tgg.draw.text_width(15, line2)))
		local h = if line2 ~= "" then 54 else 32
		-- Centered on the percent, kept inside the window, in the game's 32-bit floats
		local cx = x
		if tgg.f32(cx - w / 2) < tgg.f32(bounds.x + 6) then
			cx = tgg.f32(tgg.f32(bounds.x + 6) + w / 2)
		end
		local right = tgg.f32(tgg.f32(bounds.x + bounds.w) - 6)
		if tgg.f32(cx + w / 2) > right then
			cx = tgg.f32(right - w / 2)
		end
		local top = tgg.f32(tgg.f32(y - HUD_ABOVE) - h)
		local box_x = tgg.f32(cx - w / 2)

		local good = r.kind == ledges.DASH and r.left > 0
		local neutral = r.kind == ledges.DASH and r.left == 0
		local border: tgg.Color
		local title_col: tgg.Color
		if good then
			border = { r = 116, g = 234, b = 144, a = alpha(165, a) }
			title_col = { r = 124, g = 238, b = 148, a = alpha(255, a) }
		elseif neutral then
			border = { r = 255, g = 210, b = 68, a = alpha(165, a) }
			title_col = { r = 255, g = 214, b = 76, a = alpha(255, a) }
		else
			border = { r = 255, g = 114, b = 102, a = alpha(170, a) }
			title_col = { r = 255, g = 118, b = 104, a = alpha(255, a) }
		end

		rounded_rect(box_x, top + 2, w, h, 7, { r = 0, g = 0, b = 0, a = alpha(110, a) }, nil, 0)
		rounded_rect(box_x, top, w, h, 7, { r = 12, g = 17, b = 42, a = alpha(224, a) }, border, 1.25)
		tgg.draw.text(cx, tgg.f32(top + 6), 19, title_col, tgg.align.CENTER, line1)
		if line2 ~= "" then
			tgg.draw.rect(
				tgg.f32(box_x + 10),
				tgg.f32(top + 28),
				tgg.f32(w - 20),
				1,
				{ r = 128, g = 152, b = 230, a = alpha(68, a) }
			)
			tgg.draw.text(
				cx,
				tgg.f32(top + 32),
				15,
				{ r = 220, g = 228, b = 246, a = alpha(255, a) },
				tgg.align.CENTER,
				line2
			)
		end
	end
end)
