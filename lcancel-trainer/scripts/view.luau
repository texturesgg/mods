--!strict
-- What the player sees: the result of each landing by the player's percent, and the rate.
local tgg = require("@tgg")
local landings = require("./landings")

local state = tgg.state(landings.defaults())

-- Displayed frames a result stays up, the last FADE of them fading
local SHOW = 100
local FADE = 20
-- The gap above the percent
local HUD_ABOVE = 95

local function fresh()
	return {
		seen = { 0, 0, 0, 0 }, -- state.port[].seq counted so far
		hits = { 0, 0, 0, 0 }, -- L-cancels
		tries = { 0, 0, 0, 0 }, -- landings that could be L-canceled
		age = { SHOW, SHOW, SHOW, SHOW }, -- displayed frames since the last result
		result = { 0, 0, 0, 0 },
		frames = { 0, 0, 0, 0 },
	}
end

local in_match = false
local view = fresh()

-- Before each logic frame: counts the landings whose frame is final
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
		local st = state.port[p]
		if st.seq < view.seen[p] then
			view.seen[p] = st.seq -- the state went back (a rollback past it)
		end
		if st.seq > view.seen[p] and st.frame <= final then
			view.seen[p] = st.seq
			view.tries[p] += 1
			if st.result == landings.CANCEL then
				view.hits[p] += 1
			end
			view.result[p] = st.result
			view.frames[p] = st.frames
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
	if not landings.showing() then
		return
	end
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
		local left = SHOW - view.age[p]
		local a = if left < FADE then tgg.f32(left / FADE) else 1
		local text = landings.label(view.result[p], view.frames[p])
		local rate = ""
		if tgg.settings.rate and view.tries[p] > 0 then
			rate = `{view.hits[p]}/{view.tries[p]}  {view.hits[p] * 100 // view.tries[p]}%`
		end
		local w = tgg.f32(20 + math.max(tgg.draw.text_width(20, text), tgg.draw.text_width(16, rate)))
		local h = if rate ~= "" then 46 else 28
		local top = tgg.f32(tgg.f32(y - HUD_ABOVE) - h)
		tgg.draw.rect(tgg.f32(x - w / 2), top, w, h, { r = 0, g = 0, b = 0, a = alpha(150, a) })
		local good = { r = 130, g = 235, b = 130, a = alpha(255, a) }
		local bad = { r = 255, g = 120, b = 100, a = alpha(255, a) }
		tgg.draw.text(x, top + 4, 20, if view.result[p] == landings.CANCEL then good else bad, tgg.align.CENTER, text)
		if rate ~= "" then
			tgg.draw.text(x, top + 26, 16, { r = 230, g = 230, b = 240, a = alpha(255, a) }, tgg.align.CENTER, rate)
		end
	end
end)
