--!strict
-- Save states: D-pad left saves the match as it is right now, D-pad right puts it back: both
-- fighters, percents, the CPU mid-action, items, the stage, the RNG. One state, kept until the
-- match ends.
--
-- A save and a load are asked for from the frame event and happen at the next boundary between
-- logic frames. The buttons are read from the viewer's controllers (tgg.input.controller), so they
-- never reach the simulation. Off while a replay plays: the replay player has its own controls
-- on the D-pad.
--
-- With tgg.practice-stage installed it's also Practice > Free practice, and it stays off in
-- another drill's match, which has its own resets on the D-pad.
local tgg = require("@tgg")

local ID = "tgg.save-states"
-- Displayed frames a message stays up, the last FADE of them fading
local SHOW = 60
local FADE = 15
-- Displayed frames the Free practice D-pad hint shows at match start
local HINT_SHOW = 240
local HINT_FADE = 24

local TONE_INFO, TONE_GOOD, TONE_WARN = 0, 1, 2

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

local saved: tgg.Snapshot? = nil
local was_in_match = false
local message = ""
local message_age = SHOW
local message_tone = TONE_INFO
local hint_age = HINT_SHOW

-- The mods it uses when they're there, looked up once every mod has loaded
local practice: { [string]: (...any) -> any }? = nil
local replays: { [string]: (...any) -> any }? = nil
tgg.on.init(function()
	practice = tgg.mod("tgg.practice-stage")
	replays = tgg.mod("tgg.replay-player")
end)

local function say(text: string, tone: number)
	message = text
	message_age = 0
	message_tone = tone
end

tgg.menu.add("practice/free", {
	label = "Free practice",
	description = "An empty stage. D-pad left saves, D-pad right loads.",
	action = function()
		if practice then
			practice.start(ID, "", false, false)
		end
	end,
})

local function usable(): boolean
	local drill = if practice then practice.drill() else nil
	if drill ~= nil then
		return tgg.in_match() and drill == ID
	end
	return tgg.in_match()
		and not (replays ~= nil and replays.playing())
		and (tgg.settings.where == "Every match" or tgg.mode.current() == tgg.mode.TRAINING)
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

-- Draws a compact GameCube D-pad glyph with left (`dir < 0`) or right (`dir > 0`) lit
local function draw_dpad(cx: number, cy: number, size: number, dir: number, a: number)
	local r = size * 0.5
	local arm = size * 0.28
	local span = size * 0.72
	local half = span * 0.5
	local base_col = { r = 48, g = 56, b = 86, a = alpha(235, a) }
	local rim_col = { r = 106, g = 134, b = 226, a = alpha(120, a) }
	local dim_col = { r = 112, g = 122, b = 154, a = alpha(255, a) }
	local lit_col = { r = 236, g = 240, b = 252, a = alpha(255, a) }
	tgg.draw.circle(cx, cy, r, base_col)
	tgg.draw.circle_outline(cx, cy, r - 0.5, 1, rim_col)
	tgg.draw.rect(cx - arm * 0.5, cy - half, arm, span, dim_col)
	tgg.draw.rect(cx - half, cy - arm * 0.5, span, arm, dim_col)
	if dir < 0 then
		tgg.draw.rect(cx - half, cy - arm * 0.5, half + arm * 0.2, arm, lit_col)
	else
		tgg.draw.rect(cx - arm * 0.2, cy - arm * 0.5, half + arm * 0.2, arm, lit_col)
	end
end

tgg.on.frame(function()
	if message_age < SHOW then
		message_age += 1
	end
	if hint_age < HINT_SHOW then
		hint_age += 1
	end
	local in_match = tgg.in_match()
	if not in_match and was_in_match and saved then
		-- a snapshot only loads into its own match
		tgg.snapshot.free(saved)
		saved = nil
	end
	if in_match and not was_in_match then
		local drill = if practice then practice.drill() else nil
		hint_age = if drill == ID then 0 else HINT_SHOW
	end
	was_in_match = in_match
	if not usable() then
		return
	end
	local input = pressed()
	if bit32.btest(input, tgg.button.LEFT) then
		if saved then
			tgg.snapshot.free(saved)
		end
		saved = tgg.snapshot.save()
		if saved then
			say("Saved", TONE_INFO)
			tgg.log("saved")
		else
			say("Couldn't save", TONE_WARN)
		end
	elseif bit32.btest(input, tgg.button.RIGHT) then
		if saved == nil then
			say("Nothing saved yet", TONE_WARN)
		elseif tgg.snapshot.load(saved) ~= tgg.snapshot.OK then
			say("Couldn't load", TONE_WARN)
		end
	end
end)

tgg.on.snapshot_loaded(function()
	if usable() then
		say("Loaded", TONE_GOOD)
		tgg.log(`loaded (states take {tgg.snapshot.bytes() // 1048576} MB)`)
	end
end)

tgg.on.hud_draw(function()
	if not tgg.in_match() then
		return
	end

	-- Opening Free practice D-pad hint pill at top-center
	if hint_age < HINT_SHOW and usable() then
		local left = HINT_SHOW - hint_age
		local a = if left < HINT_FADE then left / HINT_FADE else 1
		local sz = 14
		local icon = 15
		local w_save = tgg.draw.text_width(sz, "Save")
		local w_load = tgg.draw.text_width(sz, "Load")
		local gap = 18
		local igap = 6
		local content = icon + igap + w_save + gap + icon + igap + w_load
		local w = content + 26
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
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Save")
		cx += w_save + gap
		draw_dpad(cx + icon * 0.5, cy, icon, 1, a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Load")
	end

	-- Save / Load toast pill
	if message_age < SHOW then
		local left = SHOW - message_age
		local a = if left < FADE then left / FADE else 1
		local sz = 17
		local tw = tgg.draw.text_width(sz, message)
		local h = 30
		local w = 14 + 8 + 8 + tw + 16
		local x = 320 - w * 0.5
		local y = 66
		local cy = y + h * 0.5

		local accent: tgg.Color
		local border: tgg.Color
		if message_tone == TONE_GOOD then
			accent = { r = 116, g = 234, b = 144, a = alpha(255, a) }
			border = { r = 116, g = 234, b = 144, a = alpha(170, a) }
		elseif message_tone == TONE_WARN then
			accent = { r = 255, g = 210, b = 68, a = alpha(255, a) }
			border = { r = 255, g = 210, b = 68, a = alpha(170, a) }
		else
			accent = { r = 108, g = 148, b = 255, a = alpha(255, a) }
			border = { r = 154, g = 182, b = 255, a = alpha(180, a) }
		end

		rounded_rect(x, y + 2, w, h, h * 0.5, { r = 0, g = 0, b = 0, a = alpha(115, a) }, nil, 0)
		rounded_rect(x, y, w, h, h * 0.5, { r = 12, g = 17, b = 42, a = alpha(230, a) }, border, 1.2)
		tgg.draw.circle(x + 17, cy, 4, accent)
		tgg.draw.text(
			x + 28,
			cy - sz * 0.52,
			sz,
			{ r = 244, g = 247, b = 255, a = alpha(255, a) },
			tgg.align.LEFT,
			message
		)
	end
end)
