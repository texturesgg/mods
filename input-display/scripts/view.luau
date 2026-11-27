--!strict
-- Vector GameCube controller overlay built on tgg.draw.*.
-- Reads live fighter inputs recorded by sim.luau (so replays, training, drills, and netplay all
-- reflect the actual fighter's inputs) and enriches local human play with independent analog L/R
-- trigger bars from tgg.input.controller.
local tgg = require("@tgg")
local inputs = require("./inputs")

local state = tgg.state(inputs.defaults())

local TRAIL_LEN = 7
-- Regular octagon with notches at the 8 GameCube cardinal/diagonal angles (0, 45, ..., 315 deg)
local OCT_COS = { 1, 0.7071068, 0, -0.7071068, -1, -0.7071068, 0, 0.7071068 }
local OCT_SIN = { 0, 0.7071068, 1, 0.7071068, 0, -0.7071068, -1, -0.7071068 }

-- Official space-navy dark-mode & GameCube palette (matches mods/direct-connect/src/ui.h)
local COL_SHADOW: tgg.Color = { r = 0, g = 0, b = 0, a = 115 }
local COL_PANEL: tgg.Color = { r = 12, g = 17, b = 42, a = 218 }
local COL_BORDER: tgg.Color = { r = 106, g = 134, b = 226, a = 118 }
local COL_WELL: tgg.Color = { r = 7, g = 10, b = 26, a = 228 }
local COL_WELL_RIM: tgg.Color = { r = 106, g = 134, b = 226, a = 140 }
local COL_AXIS: tgg.Color = { r = 106, g = 134, b = 226, a = 48 }
local COL_STEM: tgg.Color = { r = 152, g = 168, b = 212, a = 195 }
local COL_STICK_IDLE: tgg.Color = { r = 204, g = 212, b = 232, a = 255 }
local COL_STICK_LIT: tgg.Color = { r = 236, g = 242, b = 255, a = 255 }
local COL_STICK_RIM: tgg.Color = { r = 244, g = 247, b = 255, a = 255 }

local COL_C_RIM: tgg.Color = { r = 246, g = 200, b = 56, a = 125 }
local COL_C_STEM: tgg.Color = { r = 246, g = 200, b = 56, a = 170 }
local COL_C_FILL: tgg.Color = { r = 246, g = 200, b = 56, a = 255 }
local COL_C_CAP_RIM: tgg.Color = { r = 255, g = 238, b = 156, a = 255 }

local COL_TRIG_TRACK: tgg.Color = { r = 20, g = 28, b = 66, a = 220 }
local COL_TRIG_RIM: tgg.Color = { r = 106, g = 134, b = 226, a = 110 }
local COL_TRIG_ANALOG: tgg.Color = { r = 108, g = 148, b = 255, a = 230 }
local COL_TRIG_CLICK: tgg.Color = { r = 228, g = 234, b = 248, a = 255 }
local COL_TRIG_CLICK_RIM: tgg.Color = { r = 154, g = 182, b = 255, a = 245 }

local COL_A_IDLE: tgg.Color = { r = 20, g = 68, b = 60, a = 220 }
local COL_A_IDLE_RIM: tgg.Color = { r = 44, g = 206, b = 158, a = 130 }
local COL_A_LIT: tgg.Color = { r = 44, g = 206, b = 158, a = 255 }
local COL_A_LIT_RIM: tgg.Color = { r = 126, g = 240, b = 204, a = 255 }
local COL_A_INK: tgg.Color = { r = 10, g = 42, b = 34, a = 255 }

local COL_B_IDLE: tgg.Color = { r = 78, g = 26, b = 36, a = 220 }
local COL_B_IDLE_RIM: tgg.Color = { r = 224, g = 62, b = 68, a = 130 }
local COL_B_LIT: tgg.Color = { r = 224, g = 62, b = 68, a = 255 }
local COL_B_LIT_RIM: tgg.Color = { r = 252, g = 136, b = 140, a = 255 }

local COL_GREY_IDLE: tgg.Color = { r = 36, g = 44, b = 78, a = 220 }
local COL_GREY_IDLE_RIM: tgg.Color = { r = 134, g = 146, b = 186, a = 125 }
local COL_GREY_LIT: tgg.Color = { r = 196, g = 204, b = 224, a = 255 }
local COL_GREY_LIT_RIM: tgg.Color = { r = 236, g = 242, b = 255, a = 255 }

local COL_Z_IDLE: tgg.Color = { r = 42, g = 36, b = 90, a = 220 }
local COL_Z_IDLE_RIM: tgg.Color = { r = 110, g = 92, b = 224, a = 135 }
local COL_Z_LIT: tgg.Color = { r = 110, g = 92, b = 224, a = 255 }
local COL_Z_LIT_RIM: tgg.Color = { r = 170, g = 158, b = 250, a = 255 }

local COL_DPAD_BASE: tgg.Color = { r = 24, g = 32, b = 64, a = 225 }
local COL_DPAD_ARM: tgg.Color = { r = 82, g = 94, b = 132, a = 240 }
local COL_DPAD_LIT: tgg.Color = { r = 236, g = 240, b = 252, a = 255 }

local COL_TEXT_PRIMARY: tgg.Color = { r = 244, g = 247, b = 255, a = 255 }
local COL_TEXT_MUTED: tgg.Color = { r = 168, g = 180, b = 214, a = 215 }
local COL_TEXT_INK: tgg.Color = { r = 16, g = 19, b = 34, a = 255 }

local replays: { [string]: (...any) -> any }? = nil
tgg.on.init(function()
	replays = tgg.mod("tgg.replay-player")
end)

-- Stick trail ring buffer in view space (never touches simulation state)
local trail_x: { number } = {}
local trail_y: { number } = {}
local trail_port = -1
local last_tick = -1
local was_in_match = false
local detected_replay = false

local function clear_trail()
	table.clear(trail_x)
	table.clear(trail_y)
	last_tick = -1
end

local function is_replay(): boolean
	if replays ~= nil and replays.playing ~= nil then
		return replays.playing() == true
	end
	return detected_replay
end

-- Fallback replay detector when replay-player's script API table is hidden (counted C mod):
-- in an offline VS match with replay-player loaded, recorded human inputs diverge from idle local pads.
local function update_replay_detection()
	if detected_replay or replays == nil or replays.playing ~= nil then
		return
	end
	if tgg.session.players() > 1 or tgg.mode.current() ~= tgg.mode.VS then
		return
	end
	if tgg.control.paused() or tgg.control.fast_forwarding() > 0 or tgg.control.speed() ~= 1 then
		detected_replay = true
		return
	end
	for i = 1, 4 do
		local inp = state.ports[i]
		local pl = tgg.player.at(i - 1)
		if inp.active and pl ~= nil and pl.kind == tgg.player.HUMAN then
			local has_input = math.abs(inp.sx) > tgg.f32(0.15)
				or math.abs(inp.sy) > tgg.f32(0.15)
				or math.abs(inp.cx) > tgg.f32(0.15)
				or math.abs(inp.cy) > tgg.f32(0.15)
				or inp.buttons ~= 0
				or inp.trigger > tgg.f32(0.1)
			if has_input then
				local pad = if inp.controller >= 0 and inp.controller <= 3
					then tgg.input.controller(inp.controller)
					else nil
				if pad == nil then
					detected_replay = true
					return
				end
				local stick_diff = math.abs(inp.sx - pad.stick_x) > 0.25 or math.abs(inp.sy - pad.stick_y) > 0.25
				local btn_diff = bit32.band(inp.buttons, bit32.bnot(tgg.button.A))
					~= bit32.band(pad.held, bit32.bnot(tgg.button.A))
				if stick_diff or btn_diff then
					detected_replay = true
					return
				end
			end
		end
	end
end

local function showing(): boolean
	if not tgg.in_match() then
		return false
	end
	local where = tgg.settings.where
	if where == "Off" then
		return false
	elseif where == "Every match" then
		return true
	end
	-- Default: "Training & replays"
	return tgg.mode.current() == tgg.mode.TRAINING or is_replay()
end

-- Resolves which player slot (1..4) to display, or nil when none is active
local function target_port(): number?
	local pref = tgg.settings.port
	if pref == "Port 1" then
		return if state.ports[1].active then 1 else nil
	elseif pref == "Port 2" then
		return if state.ports[2].active then 2 else nil
	elseif pref == "Port 3" then
		return if state.ports[3].active then 3 else nil
	elseif pref == "Port 4" then
		return if state.ports[4].active then 4 else nil
	end

	-- Auto: online, follow the local player's human slot
	if tgg.session.players() > 1 and not is_replay() then
		local wanted = tgg.session.me()
		local seen = 0
		for i = 1, 4 do
			local pl = tgg.player.at(i - 1)
			if pl ~= nil and pl.kind == tgg.player.HUMAN then
				if seen == wanted and state.ports[i].active then
					return i
				end
				seen += 1
			end
		end
	end

	-- Offline / training / replay: first active human port, falling back to first active port
	for i = 1, 4 do
		local pl = tgg.player.at(i - 1)
		if state.ports[i].active and pl ~= nil and pl.kind == tgg.player.HUMAN then
			return i
		end
	end
	for i = 1, 4 do
		if state.ports[i].active then
			return i
		end
	end
	return nil
end

-- Precomputed fading trail colors for each (count, index) pair: zero per-frame table allocations
local TRAIL_COLS: { { tgg.Color? } } = {}
for count = 2, TRAIL_LEN do
	local row: { tgg.Color? } = {}
	for i = 1, count - 1 do
		local frac = i / count
		local a = math.floor(135 * frac * frac)
		row[i] = if a > 4 then { r = 136, g = 178, b = 255, a = a } else nil
	end
	TRAIL_COLS[count] = row
end

-- Unrolled 16-vertex rounded rectangle into `pts` (zero loops, zero per-frame allocations)
local function write_rounded_pts(pts: { number }, x: number, y: number, w: number, h: number, r: number)
	local rr = math.clamp(r, 0, math.min(w, h) * 0.5)
	local rx = x + w - rr
	local lx = x + rr
	local ty = y + rr
	local by = y + h - rr
	local r866 = rr * 0.8660254
	local r500 = rr * 0.5

	pts[1] = rx
	pts[2] = ty - rr
	pts[3] = rx + r500
	pts[4] = ty - r866
	pts[5] = rx + r866
	pts[6] = ty - r500
	pts[7] = rx + rr
	pts[8] = ty

	pts[9] = rx + rr
	pts[10] = by
	pts[11] = rx + r866
	pts[12] = by + r500
	pts[13] = rx + r500
	pts[14] = by + r866
	pts[15] = rx
	pts[16] = by + rr

	pts[17] = lx
	pts[18] = by + rr
	pts[19] = lx - r500
	pts[20] = by + r866
	pts[21] = lx - r866
	pts[22] = by + r500
	pts[23] = lx - rr
	pts[24] = by

	pts[25] = lx - rr
	pts[26] = ty
	pts[27] = lx - r866
	pts[28] = ty - r500
	pts[29] = lx - r500
	pts[30] = ty - r866
	pts[31] = lx
	pts[32] = ty - rr
end

local function make_rounded_pts(x: number, y: number, w: number, h: number, r: number): { number }
	local pts = table.create(32, 0)
	write_rounded_pts(pts, x, y, w, h, r)
	return pts
end

local function make_octagon_pts(cx: number, cy: number, r: number): { number }
	local pts = table.create(16, 0)
	for i = 1, 8 do
		pts[i * 2 - 1] = cx + OCT_COS[i] * r
		pts[i * 2] = cy + OCT_SIN[i] * r
	end
	return pts
end

-- Reusable scratch polygon for dynamic L/R analog trigger fills
local dyn_pts = table.create(32, 0)

-- Cached static geometry keyed on (x, y, s) so 99.9% of frames allocate nothing and skip polygon math
type CachedLayout = {
	x: number,
	y: number,
	s: number,
	pts_shadow: { number },
	pts_panel: { number },
	pts_l_track: { number },
	pts_r_track: { number },
	pts_stick_oct: { number },
	pts_cstick_oct: { number },
	pts_z: { number },
	pts_y: { number },
	pts_x: { number },
}
local layout_cache: CachedLayout? = nil

local function get_layout(x: number, y: number, w: number, s: number): CachedLayout
	if layout_cache ~= nil and layout_cache.x == x and layout_cache.y == y and layout_cache.s == s then
		return layout_cache
	end
	local h = 76 * s
	local trig_w = 50 * s
	local trig_h = 11 * s
	local trig_y = y + 7 * s
	local trig_r = trig_h * 0.48
	local layout: CachedLayout = {
		x = x,
		y = y,
		s = s,
		pts_shadow = make_rounded_pts(x, y + 2 * s, w, h, 9 * s),
		pts_panel = make_rounded_pts(x, y, w, h, 9 * s),
		pts_l_track = make_rounded_pts(x + 9 * s, trig_y, trig_w, trig_h, trig_r),
		pts_r_track = make_rounded_pts(x + w - 9 * s - trig_w, trig_y, trig_w, trig_h, trig_r),
		pts_stick_oct = make_octagon_pts(x + 30 * s, y + 43 * s, 19 * s),
		pts_cstick_oct = make_octagon_pts(x + 90 * s, y + 53 * s, 12.0 * s),
		pts_z = make_rounded_pts(x + 131 * s, y + 22 * s, 20 * s, 9.5 * s, 9.5 * s * 0.45),
		pts_y = make_rounded_pts(x + 115 * s, y + 32 * s, 18 * s, 9.5 * s, 9.5 * s * 0.46),
		pts_x = make_rounded_pts(x + 141 * s, y + 39 * s, 10 * s, 18 * s, 10 * s * 0.46),
	}
	layout_cache = layout
	return layout
end

-- Flat scratch arrays for HUD overlap detection in panel_origin (zero per-frame allocations)
local hud_x0 = table.create(4, 0)
local hud_x1 = table.create(4, 0)
local hud_y0 = table.create(4, 0)
local hud_y1 = table.create(4, 0)

local function hits_hud(px: number, py: number, w: number, h: number, n_huds: number): number
	for i = 1, n_huds do
		if px < hud_x1[i] and (px + w) > hud_x0[i] and py < hud_y1[i] and (py + h) > hud_y0[i] then
			return i
		end
	end
	return 0
end

-- Computes panel origin (x, y) avoiding Melee's stock/percent HUD, netplay ping pill, and replay bar
local function panel_origin(w: number, h: number): (number, number)
	local b = tgg.draw.bounds()
	local pos = tgg.settings.position
	local margin_x = 12
	local margin_y = 10
	local is_right = (pos == "Bottom right" or pos == "Top right")
	local is_top = (pos == "Top left" or pos == "Top right")

	local x = if is_right then (b.x + b.w - w - margin_x) else (b.x + margin_x)
	local y: number

	if is_top then
		y = b.y + margin_y
		-- Stay below netplay's top-right ping/rollback pill (y=8..30)
		if is_right and tgg.session.players() > 1 then
			y = math.max(y, b.y + 36)
		end
		-- Stay below replay-player's top timeline bar (y=4..48) when visible
		if is_replay() and (tgg.control.paused() or tgg.control.fast_forwarding() > 0 or tgg.control.speed() ~= 1) then
			y = math.max(y, b.y + 54)
		end
	else
		y = b.y + b.h - h - margin_y
		-- Collect active player HUD boxes to avoid overlapping stock icons or percents
		local n_huds = 0
		for i = 0, 3 do
			local pl = tgg.player.at(i)
			if pl ~= nil and pl.kind ~= tgg.player.NONE and pl.hud_x >= 0 then
				n_huds += 1
				hud_x0[n_huds] = pl.hud_x - 72
				hud_x1[n_huds] = pl.hud_x + 72
				hud_y0[n_huds] = pl.hud_y - 92
				hud_y1[n_huds] = pl.hud_y + 12
			end
		end

		local hit = hits_hud(x, y, w, h, n_huds)
		if hit > 0 then
			-- First try sliding horizontally beside the HUD (keeps bottom row clear of trainer cards in 1P Training)
			local slide_x = if is_right then (hud_x0[hit] - w - 10) else (hud_x1[hit] + 10)
			if
				slide_x >= b.x + margin_x
				and (slide_x + w) <= (b.x + b.w - margin_x)
				and hits_hud(slide_x, y, w, h, n_huds) == 0
			then
				x = slide_x
			else
				-- Otherwise lift cleanly above the HUD
				local lift_y = hud_y0[hit] - h - 8
				for i = 1, n_huds do
					if x < hud_x1[i] and (x + w) > hud_x0[i] and lift_y + h > hud_y0[i] then
						lift_y = math.min(lift_y, hud_y0[i] - h - 8)
					end
				end
				y = math.max(b.y + margin_y, lift_y)
			end
		end
	end

	return x, y
end

-- Resolves live button mask, sticks, and separate L/R analog fill (0..1) for port p (1..4)
local function sample_inputs(p: number): (number, number, number, number, number, number, number, boolean, boolean)
	local inp = state.ports[p]
	local sx, sy = inp.sx, inp.sy
	local cx, cy = inp.cx, inp.cy
	local btns = inp.buttons
	local trig = math.clamp(inp.trigger, 0, 1)

	local l_dig = bit32.btest(btns, tgg.button.L)
	local r_dig = bit32.btest(btns, tgg.button.R)
	local z_held = bit32.btest(btns, tgg.button.Z)

	-- For local human play outside replays, read physical analog L/R triggers and raw Z/A separation
	local local_pad: tgg.Controller? = nil
	if not is_replay() and not inp.cpu and inp.controller >= 0 and inp.controller <= 3 then
		local is_local_human = tgg.session.players() <= 1
		if not is_local_human then
			local wanted = tgg.session.me()
			local seen = 0
			for i = 1, 4 do
				local pl = tgg.player.at(i - 1)
				if pl ~= nil and pl.kind == tgg.player.HUMAN then
					if i == p and seen == wanted then
						is_local_human = true
					end
					seen += 1
				end
			end
		end
		if is_local_human then
			local pad = tgg.input.controller(inp.controller)
			if pad ~= nil and math.abs(pad.stick_x - sx) < 0.25 and math.abs(pad.stick_y - sy) < 0.25 then
				local_pad = pad
			end
		end
	end

	local l_ana = 0
	local r_ana = 0
	if local_pad ~= nil then
		l_ana = math.clamp(local_pad.trigger_l, 0, 1)
		r_ana = math.clamp(local_pad.trigger_r, 0, 1)
		if l_dig then
			l_ana = 1
		end
		if r_dig then
			r_ana = 1
		end
		-- Melee sets A when Z is held in fighter.buttons; reflect raw physical A when reading local pad
		if z_held and not bit32.btest(local_pad.held, tgg.button.A) then
			btns = bit32.band(btns, bit32.bnot(tgg.button.A))
		end
	else
		-- Replay or remote/CPU fighter: derive L/R fill from fighter.trigger and digital L/R bits
		-- Z registers as a light trigger press (~0.35) in Melee; suppress phantom trigger bars on Z-only
		local analog = trig
		if z_held and not l_dig and not r_dig and analog <= 0.37 then
			analog = 0
		end
		if l_dig and r_dig then
			l_ana = 1
			r_ana = 1
		elseif l_dig then
			l_ana = 1
		elseif r_dig then
			r_ana = 1
		elseif analog > 0.01 then
			-- Light-shield analog press without digital click: show on both trigger meters
			l_ana = analog
			r_ana = analog
		end
	end

	return sx, sy, cx, cy, btns, l_ana, r_ana, l_dig, r_dig
end

local function draw_trigger_shape(
	track_pts: { number },
	x: number,
	y: number,
	w: number,
	h: number,
	analog: number,
	clicked: boolean,
	from_right: boolean,
	s: number
)
	local rim = if clicked then COL_TRIG_CLICK_RIM else COL_TRIG_RIM
	tgg.draw.polygon(track_pts, COL_TRIG_TRACK)

	local fill_frac = if clicked then 1 else analog
	if fill_frac > 0.02 then
		if fill_frac >= 0.99 then
			tgg.draw.polygon(track_pts, if clicked then COL_TRIG_CLICK else COL_TRIG_ANALOG)
		else
			local fw = math.max(h * 0.85, w * fill_frac)
			local fx = if from_right then (x + w - fw) else x
			write_rounded_pts(dyn_pts, fx, y, fw, h, h * 0.48)
			tgg.draw.polygon(dyn_pts, COL_TRIG_ANALOG)
		end
	end
	tgg.draw.polygon_outline(track_pts, 1.0 * s, rim)
end

tgg.on.frame(function()
	local in_match = tgg.in_match()
	if not in_match then
		if was_in_match then
			clear_trail()
			trail_port = -1
			detected_replay = false
		end
		was_in_match = false
		return
	end
	if not was_in_match then
		clear_trail()
		trail_port = -1
		detected_replay = false
		was_in_match = true
	end
	update_replay_detection()
	if not showing() then
		return
	end
	local p = target_port()
	if p == nil then
		return
	end
	if p ~= trail_port then
		clear_trail()
		trail_port = p
	end
	if state.tick ~= last_tick then
		last_tick = state.tick
		local inp = state.ports[p]
		table.insert(trail_x, inp.sx)
		table.insert(trail_y, inp.sy)
		while #trail_x > TRAIL_LEN do
			table.remove(trail_x, 1)
			table.remove(trail_y, 1)
		end
	end
end)

tgg.on.hud_draw(function()
	if not showing() then
		return
	end
	local p = target_port()
	if p == nil then
		return
	end

	local s = if tgg.settings.size == "Compact" then 0.80 else 1.0
	local w = 160 * s
	local h = 76 * s
	local x, y = panel_origin(w, h)
	local layout = get_layout(x, y, w, s)

	local sx, sy, cx, cy, btns, l_ana, r_ana, l_dig, r_dig = sample_inputs(p)

	-- Pass 1: ALL colored vector shapes in one single MODE_COLOR batch (zero mid-draw GX flushes)
	tgg.draw.polygon(layout.pts_shadow, COL_SHADOW)
	tgg.draw.polygon(layout.pts_panel, COL_PANEL)
	tgg.draw.polygon_outline(layout.pts_panel, 1.15 * s, COL_BORDER)

	-- Top shoulder row: L and R triggers
	local trig_w = 50 * s
	local trig_h = 11 * s
	local trig_y = y + 7 * s
	local l_x = x + 9 * s
	local r_x = x + w - 9 * s - trig_w
	draw_trigger_shape(layout.pts_l_track, l_x, trig_y, trig_w, trig_h, l_ana, l_dig, false, s)
	draw_trigger_shape(layout.pts_r_track, r_x, trig_y, trig_w, trig_h, r_ana, r_dig, true, s)

	-- Left cluster: octagonal Control Stick gate + optional trail + live stick
	local st_cx = x + 30 * s
	local st_cy = y + 43 * s
	local st_r = 19 * s
	local travel = 14.2 * s
	tgg.draw.polygon(layout.pts_stick_oct, COL_WELL)
	tgg.draw.polygon_outline(layout.pts_stick_oct, 1.1 * s, COL_WELL_RIM)
	tgg.draw.line(st_cx - st_r + 3 * s, st_cy, st_cx + st_r - 3 * s, st_cy, 0.8 * s, COL_AXIS)
	tgg.draw.line(st_cx, st_cy - st_r + 3 * s, st_cx, st_cy + st_r - 3 * s, 0.8 * s, COL_AXIS)

	if tgg.settings.trail and #trail_x > 1 then
		local count = #trail_x
		local cols = TRAIL_COLS[count]
		if cols ~= nil then
			for i = 1, count - 1 do
				local col = cols[i]
				if col ~= nil then
					local frac = i / count
					local px0 = st_cx + math.clamp(trail_x[i], -1, 1) * travel
					local py0 = st_cy - math.clamp(trail_y[i], -1, 1) * travel
					local px1 = st_cx + math.clamp(trail_x[i + 1], -1, 1) * travel
					local py1 = st_cy - math.clamp(trail_y[i + 1], -1, 1) * travel
					tgg.draw.line(px0, py0, px1, py1, (1.2 + 1.8 * frac) * s, col)
					tgg.draw.circle(px1, py1, (1.4 + 1.6 * frac) * s, col)
				end
			end
		end
	end

	local dot_x = st_cx + math.clamp(sx, -1, 1) * travel
	local dot_y = st_cy - math.clamp(sy, -1, 1) * travel
	local moved = (sx * sx + sy * sy) > 0.02
	if moved then
		tgg.draw.line(st_cx, st_cy, dot_x, dot_y, 2.2 * s, COL_STEM)
	end
	local cap_r = 5.4 * s
	tgg.draw.circle(dot_x, dot_y, cap_r, if moved then COL_STICK_LIT else COL_STICK_IDLE)
	tgg.draw.circle_outline(dot_x, dot_y, cap_r - 0.4 * s, 1.0 * s, COL_STICK_RIM)

	-- Center-left: compact GameCube D-pad cross
	local dp_cx = x + 61 * s
	local dp_cy = y + 54 * s
	local dp_r = 10.5 * s
	local dp_arm = 4.2 * s
	local dp_span = 15.6 * s
	local dp_half = dp_span * 0.5
	tgg.draw.circle(dp_cx, dp_cy, dp_r, COL_DPAD_BASE)
	tgg.draw.circle_outline(dp_cx, dp_cy, dp_r - 0.4 * s, 0.9 * s, COL_BORDER)
	tgg.draw.rect(dp_cx - dp_arm * 0.5, dp_cy - dp_half, dp_arm, dp_span, COL_DPAD_ARM)
	tgg.draw.rect(dp_cx - dp_half, dp_cy - dp_arm * 0.5, dp_span, dp_arm, COL_DPAD_ARM)
	if bit32.btest(btns, tgg.button.UP) then
		tgg.draw.rect(dp_cx - dp_arm * 0.5, dp_cy - dp_half, dp_arm, dp_half, COL_DPAD_LIT)
	end
	if bit32.btest(btns, tgg.button.DOWN) then
		tgg.draw.rect(dp_cx - dp_arm * 0.5, dp_cy, dp_arm, dp_half, COL_DPAD_LIT)
	end
	if bit32.btest(btns, tgg.button.LEFT) then
		tgg.draw.rect(dp_cx - dp_half, dp_cy - dp_arm * 0.5, dp_half, dp_arm, COL_DPAD_LIT)
	end
	if bit32.btest(btns, tgg.button.RIGHT) then
		tgg.draw.rect(dp_cx, dp_cy - dp_arm * 0.5, dp_half, dp_arm, COL_DPAD_LIT)
	end

	-- Center: Start pill
	local start_lit = bit32.btest(btns, tgg.button.START)
	local start_cx = x + 66 * s
	local start_cy = y + 32 * s
	local start_r = 4.5 * s
	tgg.draw.circle(start_cx, start_cy, start_r, if start_lit then COL_GREY_LIT else COL_GREY_IDLE)
	tgg.draw.circle_outline(
		start_cx,
		start_cy,
		start_r - 0.3 * s,
		0.9 * s,
		if start_lit then COL_GREY_LIT_RIM else COL_GREY_IDLE_RIM
	)

	-- Center-right: Yellow C-stick
	local cs_cx = x + 90 * s
	local cs_cy = y + 53 * s
	local cs_travel = 8.4 * s
	tgg.draw.polygon(layout.pts_cstick_oct, COL_WELL)
	tgg.draw.polygon_outline(layout.pts_cstick_oct, 1.0 * s, COL_C_RIM)
	local cdot_x = cs_cx + math.clamp(cx, -1, 1) * cs_travel
	local cdot_y = cs_cy - math.clamp(cy, -1, 1) * cs_travel
	if (cx * cx + cy * cy) > 0.02 then
		tgg.draw.line(cs_cx, cs_cy, cdot_x, cdot_y, 1.8 * s, COL_C_STEM)
	end
	local ccap_r = 4.2 * s
	tgg.draw.circle(cdot_x, cdot_y, ccap_r, COL_C_FILL)
	tgg.draw.circle_outline(cdot_x, cdot_y, ccap_r - 0.3 * s, 0.9 * s, COL_C_CAP_RIM)

	-- Z, Y, X pills (cached polygons)
	local z_lit = bit32.btest(btns, tgg.button.Z)
	tgg.draw.polygon(layout.pts_z, if z_lit then COL_Z_LIT else COL_Z_IDLE)
	tgg.draw.polygon_outline(layout.pts_z, 1.0 * s, if z_lit then COL_Z_LIT_RIM else COL_Z_IDLE_RIM)

	local y_lit = bit32.btest(btns, tgg.button.Y)
	tgg.draw.polygon(layout.pts_y, if y_lit then COL_GREY_LIT else COL_GREY_IDLE)
	tgg.draw.polygon_outline(layout.pts_y, 1.0 * s, if y_lit then COL_GREY_LIT_RIM else COL_GREY_IDLE_RIM)

	local x_lit = bit32.btest(btns, tgg.button.X)
	tgg.draw.polygon(layout.pts_x, if x_lit then COL_GREY_LIT else COL_GREY_IDLE)
	tgg.draw.polygon_outline(layout.pts_x, 1.0 * s, if x_lit then COL_GREY_LIT_RIM else COL_GREY_IDLE_RIM)

	-- A and B circles
	local a_lit = bit32.btest(btns, tgg.button.A)
	local a_cx = x + 127 * s
	local a_cy = y + 52 * s
	local a_r = 9.8 * s
	tgg.draw.circle(a_cx, a_cy, a_r, if a_lit then COL_A_LIT else COL_A_IDLE)
	tgg.draw.circle_outline(a_cx, a_cy, a_r - 0.4 * s, 1.1 * s, if a_lit then COL_A_LIT_RIM else COL_A_IDLE_RIM)

	local b_lit = bit32.btest(btns, tgg.button.B)
	local b_cx = x + 112 * s
	local b_cy = y + 61 * s
	local b_r = 6.6 * s
	tgg.draw.circle(b_cx, b_cy, b_r, if b_lit then COL_B_LIT else COL_B_IDLE)
	tgg.draw.circle_outline(b_cx, b_cy, b_r - 0.4 * s, 1.0 * s, if b_lit then COL_B_LIT_RIM else COL_B_IDLE_RIM)

	-- Pass 2: ALL text labels consecutively in MODE_GLYPH (single flush from shapes -> glyphs)
	local fsz = 8.5 * s
	local trig_ty = trig_y + (trig_h - fsz) * 0.5 - 0.3 * s
	tgg.draw.text(l_x + trig_w * 0.5, trig_ty, fsz, if l_dig then COL_TEXT_INK else COL_TEXT_PRIMARY, tgg.align.CENTER, "L")
	tgg.draw.text(r_x + trig_w * 0.5, trig_ty, fsz, if r_dig then COL_TEXT_INK else COL_TEXT_PRIMARY, tgg.align.CENTER, "R")

	local p_sz = 9 * s
	tgg.draw.text(x + w * 0.5, trig_y + (trig_h - p_sz) * 0.5 - 0.3 * s, p_sz, COL_TEXT_MUTED, tgg.align.CENTER, `P{p}`)

	local z_sz = 7.5 * s
	tgg.draw.text(
		x + 141 * s,
		y + 22 * s + (9.5 * s - z_sz) * 0.5 - 0.3 * s,
		z_sz,
		if z_lit then COL_TEXT_PRIMARY else COL_TEXT_MUTED,
		tgg.align.CENTER,
		"Z"
	)

	local btn_sz = 7.8 * s
	tgg.draw.text(
		x + 124 * s,
		y + 32 * s + (9.5 * s - btn_sz) * 0.5 - 0.2 * s,
		btn_sz,
		if y_lit then COL_TEXT_INK else COL_TEXT_MUTED,
		tgg.align.CENTER,
		"Y"
	)
	tgg.draw.text(
		x + 146 * s,
		y + 39 * s + (18 * s - btn_sz) * 0.5 - 0.2 * s,
		btn_sz,
		if x_lit then COL_TEXT_INK else COL_TEXT_MUTED,
		tgg.align.CENTER,
		"X"
	)

	local a_sz = 11 * s
	tgg.draw.text(a_cx, a_cy - a_sz * 0.53, a_sz, if a_lit then COL_A_INK else COL_TEXT_MUTED, tgg.align.CENTER, "A")

	local b_sz = 8.4 * s
	tgg.draw.text(b_cx, b_cy - b_sz * 0.53, b_sz, if b_lit then COL_TEXT_PRIMARY else COL_TEXT_MUTED, tgg.align.CENTER, "B")
end)
