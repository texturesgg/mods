--!strict
-- What the player sees: each ledge dash's breakdown above the player's percent.
local tgg = require("@tgg")
local ledges = require("./ledges")

local state = tgg.state(ledges.defaults())

-- Displayed frames a result stays up, the last FADE of them fading
local SHOW = 150
local FADE = 20
-- How far above the HUD the panel sits
local HUD_ABOVE = 95

local function fresh()
	return {
		seen = { 0, 0, 0, 0 }, -- state.res[].seq shown so far
		age = { SHOW, SHOW, SHOW, SHOW }, -- displayed frames since the last result
		shown = { ledges.result(), ledges.result(), ledges.result(), ledges.result() },
	}
end

local in_match = false
local view = fresh()

-- Before each logic frame: takes the results whose frame is final
tgg.on.advance(function()
	if not tgg.in_match() then
		in_match = false
		return
	end
	if not in_match then
		view = fresh()
		in_match = true
	end
	local final = tgg.frame_final()
	for p = 1, 4 do
		local r = state.res[p]
		if r.seq < view.seen[p] then
			view.seen[p] = r.seq -- the state went back (a rollback past it)
		end
		if r.seq > view.seen[p] and r.frame <= final then
			view.seen[p] = r.seq
			view.shown[p] = table.clone(r)
			view.age[p] = 0
		end
	end
end)

tgg.on.frame(function()
	for p = 1, 4 do
		if view.age[p] < SHOW then
			view.age[p] += 1
		end
	end
end)

-- Colors take whole channels, as C's do: a float's alpha cut to a byte
local function alpha(scale: number, a: number): number
	return math.floor(tgg.f32(scale * a))
end

tgg.on.hud_draw(function()
	if not ledges.showing() then
		return
	end
	local bounds = tgg.draw.bounds()
	for p = 1, 4 do
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
		local w = tgg.f32(20 + math.max(tgg.draw.text_width(20, line1), tgg.draw.text_width(16, line2)))
		local h = if line2 ~= "" then 46 else 28
		-- Centered on the percent, kept inside the window, in the game's 32-bit floats
		local cx = x
		if tgg.f32(cx - w / 2) < tgg.f32(bounds.x + 4) then
			cx = tgg.f32(tgg.f32(bounds.x + 4) + w / 2)
		end
		local right = tgg.f32(tgg.f32(bounds.x + bounds.w) - 4)
		if tgg.f32(cx + w / 2) > right then
			cx = tgg.f32(right - w / 2)
		end
		local top = tgg.f32(tgg.f32(y - HUD_ABOVE) - h)
		tgg.draw.rect(tgg.f32(cx - w / 2), top, w, h, { r = 0, g = 0, b = 0, a = alpha(150, a) })
		local good = r.kind == ledges.DASH and r.left > 0
		local color = if good
			then { r = 130, g = 235, b = 130, a = alpha(255, a) }
			else { r = 255, g = 120, b = 100, a = alpha(255, a) }
		tgg.draw.text(cx, tgg.f32(top + 4), 20, color, tgg.align.CENTER, line1)
		if line2 ~= "" then
			tgg.draw.text(cx, tgg.f32(top + 26), 16, { r = 230, g = 230, b = 240, a = alpha(255, a) }, tgg.align.CENTER, line2)
		end
	end
end)
