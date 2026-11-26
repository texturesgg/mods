--!strict
-- Save states: 3 quick slots for practice. D-pad left saves to the active slot, D-pad right loads
-- it, and D-pad up cycles slots 1 -> 2 -> 3. Saves everything: both fighters, percents, the CPU
-- mid-action, items, the stage, and the RNG. Kept until the match ends.
--
-- Saves and loads are requested from the frame event and happen at the next boundary between logic
-- frames. Buttons come from the viewer's controllers (tgg.input.controller), so they never reach
-- the simulation. Off while a replay plays: the replay player has its own controls on the D-pad.
--
-- With tgg.practice-stage installed it also adds Practice > Free practice, with optional auto-load
-- on KO, and stays out of the way of other drills that use the D-pad for their own resets.
local tgg = require("@tgg")

local ID = "tgg.save-states"
local SLOT_COUNT = 3
-- Displayed frames a toast message stays up, the last FADE of them fading
local SHOW = 60
local FADE = 15
-- Displayed frames the slot bar stays up after cycling when all slots are empty
local SLOT_SHOW = 150
-- Displayed frames the Free practice D-pad hint shows at match start
local HINT_SHOW = 240
local HINT_FADE = 24
-- Displayed frames after a KO before auto-loading the active slot in Free practice
local KO_LOAD_DELAY = 12

local TONE_INFO, TONE_GOOD, TONE_WARN = 0, 1, 2

local COS4 = { 1, 0.8660254, 0.5, 0 }
local SIN4 = { 0, 0.5, 0.8660254, 1 }

local slots: { tgg.Snapshot? } = { nil, nil, nil }
local active_slot = 1
local loading_slot = 1
local was_in_match = false
local message = ""
local message_age = SHOW
local message_tone = TONE_INFO
local slot_age = SLOT_SHOW
local hint_age = HINT_SHOW
local was_ko = false
local ko_pending = false
local ko_frames = 0

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
	slot_age = 0
end

local function free_all_slots()
	for i = 1, SLOT_COUNT do
		local snap = slots[i]
		if snap ~= nil then
			tgg.snapshot.free(snap)
			slots[i] = nil
		end
	end
end

local function any_slot_saved(): boolean
	for i = 1, SLOT_COUNT do
		if slots[i] ~= nil then
			return true
		end
	end
	return false
end

local function is_free_practice(): boolean
	return practice ~= nil and practice.drill() == ID
end

local function is_ko_action(action: number): boolean
	return action >= tgg.action.DEAD_DOWN and action <= tgg.action.DEAD_UP_FALL_HIT_CAMERA_ICE
end

tgg.menu.add("practice/free", {
	label = "Free practice",
	description = "An empty stage with 3 save slots. Left saves, right loads, up switches slots.",
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
		and not (replays ~= nil and replays.playing ~= nil and replays.playing())
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

-- Draws a compact GameCube D-pad glyph with left, right, or up lit
local function draw_dpad(cx: number, cy: number, size: number, dir: string, a: number)
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
	if dir == "left" then
		tgg.draw.rect(cx - half, cy - arm * 0.5, half + arm * 0.2, arm, lit_col)
	elseif dir == "up" then
		tgg.draw.rect(cx - arm * 0.5, cy - half, arm, half + arm * 0.2, lit_col)
	else
		tgg.draw.rect(cx - arm * 0.2, cy - arm * 0.5, half + arm * 0.2, arm, lit_col)
	end
end

tgg.on.frame(function()
	if message_age < SHOW then
		message_age += 1
	end
	if slot_age < SLOT_SHOW then
		slot_age += 1
	end
	if hint_age < HINT_SHOW then
		hint_age += 1
	end
	local in_match = tgg.in_match()
	if not in_match and was_in_match then
		-- Snapshots only load into their own match
		free_all_slots()
		active_slot = 1
		loading_slot = 1
		was_ko = false
		ko_pending = false
		ko_frames = 0
	end
	if in_match and not was_in_match then
		active_slot = 1
		loading_slot = 1
		was_ko = false
		ko_pending = false
		ko_frames = 0
		local free = is_free_practice()
		hint_age = if free then 0 else HINT_SHOW
		slot_age = if free then 0 else SLOT_SHOW
	end
	was_in_match = in_match
	if not usable() then
		return
	end

	-- Optional auto-load on KO in Free practice when the active slot has a saved state
	if is_free_practice() and tgg.settings.auto_load_ko and slots[active_slot] ~= nil then
		local p1 = tgg.fighter.at(0, false)
		local ko_now = p1 ~= nil and is_ko_action(p1.action)
		if ko_now and not was_ko then
			ko_pending = true
			ko_frames = 0
		end
		was_ko = ko_now
		if ko_pending then
			ko_frames += 1
			if ko_frames >= KO_LOAD_DELAY then
				ko_pending = false
				ko_frames = 0
				local snap = slots[active_slot]
				if snap ~= nil then
					loading_slot = active_slot
					tgg.snapshot.load(snap)
				end
			end
		end
	else
		was_ko = false
		ko_pending = false
		ko_frames = 0
	end

	local input = pressed()
	if bit32.btest(input, tgg.button.UP) then
		active_slot = (active_slot % SLOT_COUNT) + 1
		ko_pending = false
		ko_frames = 0
		say(`Slot {active_slot}`, TONE_INFO)
		tgg.log(`slot {active_slot}`)
	elseif bit32.btest(input, tgg.button.LEFT) then
		local prev = slots[active_slot]
		if prev ~= nil then
			tgg.snapshot.free(prev)
		end
		local snap = tgg.snapshot.save()
		slots[active_slot] = snap
		ko_pending = false
		ko_frames = 0
		if snap ~= nil then
			say(`Saved slot {active_slot}`, TONE_INFO)
			tgg.log(`saved slot {active_slot}`)
		else
			say("Couldn't save", TONE_WARN)
		end
	elseif bit32.btest(input, tgg.button.RIGHT) then
		local snap = slots[active_slot]
		ko_pending = false
		ko_frames = 0
		if snap == nil then
			say(`Slot {active_slot} is empty`, TONE_WARN)
		else
			loading_slot = active_slot
			if tgg.snapshot.load(snap) ~= tgg.snapshot.OK then
				say("Couldn't load", TONE_WARN)
			end
		end
	end
end)

tgg.on.snapshot_loaded(function()
	was_ko = false
	ko_pending = false
	ko_frames = 0
	if usable() then
		say(`Loaded slot {loading_slot}`, TONE_GOOD)
		tgg.log(`loaded slot {loading_slot} (states take {tgg.snapshot.bytes() // 1048576} MB)`)
	end
end)

tgg.on.hud_draw(function()
	if not tgg.in_match() or not usable() then
		return
	end

	local show_slots = is_free_practice() or any_slot_saved() or slot_age < SLOT_SHOW
	local hint_visible = hint_age < HINT_SHOW

	-- Compact rounded [1] [2] [3] slot-pill indicator at top-center
	if show_slots then
		local a = 1
		if not is_free_practice() and not any_slot_saved() then
			local left = SLOT_SHOW - slot_age
			if left < FADE then
				a = left / FADE
			end
		end
		local chip_w = 28
		local chip_h = 18
		local gap = 5
		local pad_x = 6
		local pad_y = 3
		local w = pad_x * 2 + chip_w * SLOT_COUNT + gap * (SLOT_COUNT - 1)
		local h = chip_h + pad_y * 2
		local x = 320 - w * 0.5
		local y = 8

		rounded_rect(x, y + 1.5, w, h, h * 0.5, { r = 0, g = 0, b = 0, a = alpha(100, a) }, nil, 0)
		rounded_rect(
			x,
			y,
			w,
			h,
			h * 0.5,
			{ r = 12, g = 17, b = 42, a = alpha(224, a) },
			{ r = 106, g = 134, b = 226, a = alpha(125, a) },
			1.1
		)

		local cx = x + pad_x
		local cy = y + pad_y
		local sz = 12.5
		for i = 1, SLOT_COUNT do
			local is_active = i == active_slot
			local occupied = slots[i] ~= nil
			local fill: tgg.Color
			local border: tgg.Color
			local num_col: tgg.Color
			if is_active then
				fill = { r = 246, g = 200, b = 56, a = alpha(255, a) }
				border = { r = 255, g = 242, b = 182, a = alpha(240, a) }
				num_col = { r = 16, g = 19, b = 34, a = alpha(255, a) }
			elseif occupied then
				fill = { r = 24, g = 40, b = 76, a = alpha(230, a) }
				border = { r = 116, g = 234, b = 144, a = alpha(175, a) }
				num_col = { r = 244, g = 247, b = 255, a = alpha(255, a) }
			else
				fill = { r = 18, g = 25, b = 56, a = alpha(200, a) }
				border = { r = 96, g = 124, b = 214, a = alpha(78, a) }
				num_col = { r = 148, g = 160, b = 196, a = alpha(225, a) }
			end

			rounded_rect(cx, cy, chip_w, chip_h, chip_h * 0.5, fill, border, 1.0)
			if occupied then
				tgg.draw.text(cx + 10.5, cy + (chip_h - sz) * 0.5 - 0.3, sz, num_col, tgg.align.CENTER, tostring(i))
				local dot_col = if is_active
					then { r = 16, g = 120, b = 64, a = alpha(255, a) }
					else { r = 116, g = 234, b = 144, a = alpha(255, a) }
				tgg.draw.circle(cx + chip_w - 7.5, cy + chip_h * 0.5, 2.7, dot_col)
			else
				tgg.draw.text(cx + chip_w * 0.5, cy + (chip_h - sz) * 0.5 - 0.3, sz, num_col, tgg.align.CENTER, tostring(i))
			end
			cx += chip_w + gap
		end
	end

	-- Opening Free practice D-pad hint pill below the slot bar
	if hint_visible then
		local left = HINT_SHOW - hint_age
		local a = if left < HINT_FADE then left / HINT_FADE else 1
		local sz = 14
		local icon = 15
		local w_save = tgg.draw.text_width(sz, "Save")
		local w_load = tgg.draw.text_width(sz, "Load")
		local w_slot = tgg.draw.text_width(sz, "Slot")
		local gap = 16
		local igap = 6
		local content = (icon + igap + w_save) + gap + (icon + igap + w_load) + gap + (icon + igap + w_slot)
		local w = content + 26
		local h = 26
		local x = 320 - w * 0.5
		local y = 38
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
		draw_dpad(cx + icon * 0.5, cy, icon, "left", a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Save")
		cx += w_save + gap
		draw_dpad(cx + icon * 0.5, cy, icon, "right", a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Load")
		cx += w_load + gap
		draw_dpad(cx + icon * 0.5, cy, icon, "up", a)
		cx += icon + igap
		tgg.draw.text(cx, cy - sz * 0.52, sz, { r = 232, g = 237, b = 252, a = alpha(255, a) }, tgg.align.LEFT, "Slot")
	end

	-- Save / Load / Slot toast pill
	if message_age < SHOW then
		local left = SHOW - message_age
		local a = if left < FADE then left / FADE else 1
		local sz = 16
		local tw = tgg.draw.text_width(sz, message)
		local h = 28
		local w = 14 + 8 + 8 + tw + 16
		local x = 320 - w * 0.5
		local y = if hint_visible then 70 else 40
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
