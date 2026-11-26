--!strict
-- Wavedash trainer presentation: 20XX-style fighter_tint flash plus a rounded space-navy card
-- above the player's percent with timing (Perfect 1f / 2f / Early / Too late), stick angle in
-- degrees, slide distance, and a recent-attempts chip strip.
local tgg = require("@tgg")
local wavedashes = require("./wavedashes")

-- Displayed frames a result stays up, the last FADE of them fading
local SHOW = 150
local FADE = 20
-- Displayed frames the 20XX model tint flash lasts
local FLASH_GOOD = 12
local FLASH_BAD = 14
-- How far above the player's HUD the card sits
local HUD_ABOVE = 95
-- Recent attempts kept in the history strip
local MAX_HISTORY = 8

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

type HistoryItem = {
	chip: string,
	grade: number,
}

local function fresh()
	return {
		age = { SHOW, SHOW, SHOW, SHOW },
		perfects = { 0, 0, 0, 0 },
		tries = { 0, 0, 0, 0 },
		shown = {
			wavedashes.result(),
			wavedashes.result(),
			wavedashes.result(),
			wavedashes.result(),
		},
		history = {
			{} :: { HistoryItem },
			{} :: { HistoryItem },
			{} :: { HistoryItem },
			{} :: { HistoryItem },
		},
	}
end

local in_match = false
local view = fresh()

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
	local rr = math.clamp(r, 0, math.min(w, h) * 0.5)
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

local function grade_colors(grade: number, a: number): (tgg.Color, tgg.Color)
	if grade == wavedashes.GRADE_PERFECT then
		return { r = 116, g = 234, b = 144, a = alpha(175, a) }, { r = 124, g = 238, b = 148, a = alpha(255, a) }
	elseif grade == wavedashes.GRADE_GOOD then
		return { r = 108, g = 204, b = 255, a = alpha(170, a) }, { r = 136, g = 222, b = 255, a = alpha(255, a) }
	elseif grade == wavedashes.GRADE_WARN then
		return { r = 255, g = 210, b = 68, a = alpha(170, a) }, { r = 255, g = 214, b = 76, a = alpha(255, a) }
	end
	return { r = 255, g = 114, b = 102, a = alpha(175, a) }, { r = 255, g = 118, b = 104, a = alpha(255, a) }
end

-- Applies the 20XX-style fighter tint flash for port p (1 to 4)
local function apply_flash(p: number)
	if not tgg.settings.flash or not wavedashes.showing() then
		return
	end
	local player = tgg.player.at(p - 1)
	if player == nil or player.kind ~= tgg.player.HUMAN then
		return
	end
	local r = view.shown[p]
	local g = wavedashes.grade(r)
	local is_good = g == wavedashes.GRADE_PERFECT or g == wavedashes.GRADE_GOOD
	local total = if is_good then FLASH_GOOD else FLASH_BAD
	if view.age[p] >= total then
		return
	end
	local fighter = tgg.fighter.at(p - 1, false)
	if fighter == nil then
		return
	end
	local hold = if is_good then 4 else 5
	local strength = if view.age[p] < hold
		then 1
		else tgg.f32((total - view.age[p]) / (total - hold))
	if g == wavedashes.GRADE_PERFECT then
		tgg.draw.fighter_tint(fighter, { r = 136, g = 240, b = 255, a = alpha(205, strength) })
	elseif g == wavedashes.GRADE_GOOD then
		tgg.draw.fighter_tint(fighter, { r = 136, g = 238, b = 164, a = alpha(195, strength) })
	elseif g == wavedashes.GRADE_WARN then
		tgg.draw.fighter_tint(fighter, { r = 255, g = 210, b = 68, a = alpha(195, strength) })
	else
		tgg.draw.fighter_tint(fighter, { r = 255, g = 42, b = 42, a = alpha(215, strength) })
	end
end

tgg.on.advance(function()
	if not tgg.in_match() then
		in_match = false
	elseif not in_match then
		view = fresh()
		in_match = true
	end
end)

tgg.on.posted(function(value)
	local r: wavedashes.Result = value
	local p = r.port + 1
	local g = wavedashes.grade(r)
	view.shown[p] = r
	view.age[p] = 0
	view.tries[p] += 1
	if g == wavedashes.GRADE_PERFECT then
		view.perfects[p] += 1
	end
	local hist = view.history[p]
	table.insert(hist, { chip = wavedashes.chip(r), grade = g })
	while #hist > MAX_HISTORY do
		table.remove(hist, 1)
	end
	apply_flash(p)
	if wavedashes.showing() then
		tgg.log(`P{p}: {wavedashes.headline(r)} ({wavedashes.detail(r)})`)
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
	if not wavedashes.showing() then
		return
	end
	local bounds = tgg.draw.bounds()
	for p = 1, 4 do
		apply_flash(p)
		local player = tgg.player.at(p - 1)
		if view.age[p] >= SHOW or player == nil or player.kind ~= tgg.player.HUMAN then
			continue
		end
		local x, y = player.hud_x, player.hud_y
		if x < 0 then
			continue
		end
		local r = view.shown[p]
		local g = wavedashes.grade(r)
		local left = SHOW - view.age[p]
		local a = if left < FADE then tgg.f32(left / FADE) else 1
		local line1 = wavedashes.headline(r)
		local line2 = wavedashes.detail(r)
		if r.kind == wavedashes.LAND and view.tries[p] > 0 then
			local pct = view.perfects[p] * 100 // view.tries[p]
			line2 = `{line2}   {pct}% 1f`
		end

		local hist = view.history[p]
		local show_strip = tgg.settings.strip and #hist > 0
		local chip_gap = 4
		local chip_h = 16
		local chip_sz = 10.5
		local strip_w = 0
		local chip_widths: { number } = {}
		if show_strip then
			for i = 1, #hist do
				local cw = math.max(20, tgg.draw.text_width(chip_sz, hist[i].chip) + 8)
				chip_widths[i] = cw
				strip_w += cw
				if i > 1 then
					strip_w += chip_gap
				end
			end
		end

		local text_w = math.max(tgg.draw.text_width(19, line1), tgg.draw.text_width(14, line2))
		local w = tgg.f32(28 + math.max(text_w, strip_w))
		local h = if show_strip then 74 else 54

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

		local border, title_col = grade_colors(g, a)

		rounded_rect(box_x, top + 2, w, h, 7, { r = 0, g = 0, b = 0, a = alpha(110, a) }, nil, 0)
		rounded_rect(box_x, top, w, h, 7, { r = 12, g = 17, b = 42, a = alpha(224, a) }, border, 1.25)
		tgg.draw.text(cx, tgg.f32(top + 6), 19, title_col, tgg.align.CENTER, line1)
		tgg.draw.rect(
			tgg.f32(box_x + 10),
			tgg.f32(top + 28),
			tgg.f32(w - 20),
			1,
			{ r = 128, g = 152, b = 230, a = alpha(68, a) }
		)
		local detail_col: tgg.Color
		if r.kind == wavedashes.LAND and r.has_angle and r.angle >= 16 and r.angle <= 24 then
			detail_col = { r = 192, g = 244, b = 212, a = alpha(255, a) }
		else
			detail_col = { r = 220, g = 228, b = 246, a = alpha(255, a) }
		end
		tgg.draw.text(cx, tgg.f32(top + 32), 14, detail_col, tgg.align.CENTER, line2)

		if show_strip then
			local sx = tgg.f32(cx - strip_w * 0.5)
			local sy = tgg.f32(top + 52)
			for i = 1, #hist do
				local item = hist[i]
				local cw = chip_widths[i]
				local cb, ct = grade_colors(item.grade, a)
				local is_newest = i == #hist
				local fill: tgg.Color = if is_newest
					then { r = 26, g = 36, b = 84, a = alpha(235, a) }
					else { r = 16, g = 22, b = 52, a = alpha(205, a) }
				rounded_rect(sx, sy, cw, chip_h, 4, fill, cb, if is_newest then 1.1 else 0.85)
				tgg.draw.text(sx + cw * 0.5, sy + (chip_h - chip_sz) * 0.5 - 0.3, chip_sz, ct, tgg.align.CENTER, item.chip)
				sx += cw + chip_gap
			end
		end
	end
end)
