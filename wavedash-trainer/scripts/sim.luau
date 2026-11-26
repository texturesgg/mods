--!strict
-- Wavedash trainer: tracks each grounded jump (KNEE_BEND) through airdodge (ESCAPE_AIR) and
-- waveland slide (LANDING_FALL_SPECIAL), measuring exact frames after jumpsquat, stick angle in
-- degrees below horizontal, and slide distance once actionable.
--
-- On a frame-1 wavedash in Melee, KneeBend -> Jump -> EscapeAir -> LandingFallSpecial all happen
-- inside one logic frame, so fighter_update sees KNEE_BEND directly followed by
-- LANDING_FALL_SPECIAL on the very next tick. Both immediate and delayed landings are handled.
--
-- Read-only: never modifies the simulation, so the port classifies this mod as display (safe for
-- online play). Results are posted with tgg.post so rollbacks never double-count.
local tgg = require("@tgg")
local wavedashes = require("./wavedashes")

local state = tgg.state(wavedashes.defaults())

-- Ignore airdodges this many frames after jumpsquat (normal jumps / high platform wavelands)
local MAX_DODGE_WINDOW = 12
-- Frames of uninterrupted jump after an early L/R press in jumpsquat before reporting Early
local EARLY_CONFIRM = 6

local LR_MASK = bit32.bor(tgg.button.L, tgg.button.R)

-- Clear per-match state in match_setup (the first fighter_spawn fires before match_start)
tgg.on.match_setup(function()
	local fresh = wavedashes.defaults()
	state.tick = fresh.tick
	state.at = fresh.at
end)

tgg.on.tick(function()
	if tgg.in_match() then
		state.tick += 1
	end
end)

local function capture_angle(sx: number, sy: number): (number, boolean)
	local ax = math.abs(sx)
	local ay = -sy -- positive is downward (standard wavedash direction)
	if ax < 0.01 and math.abs(ay) < 0.01 then
		return 0, false
	end
	return math.floor(math.deg(math.atan2(ay, ax)) + 0.5), true
end

local function is_jump_or_fall(action: number): boolean
	return action == tgg.action.JUMP_F
		or action == tgg.action.JUMP_B
		or action == tgg.action.FALL
		or action == tgg.action.FALL_F
		or action == tgg.action.FALL_B
end

local function decide(port: number, r: wavedashes.Result)
	r.port = port
	state.at[port + 1].phase = wavedashes.NONE
	tgg.post(r)
end

tgg.on.fighter_update(function(fighter)
	local port = fighter.port
	if port < 0 or port >= 4 or fighter.follower then
		return
	end
	local a = state.at[port + 1]
	local action = fighter.action

	if action == tgg.action.KNEE_BEND then
		if a.phase ~= wavedashes.SQUAT then
			local fresh = wavedashes.attempt()
			fresh.phase = wavedashes.SQUAT
			fresh.start_x = fighter.x
			fresh.squat_end = state.tick
			state.at[port + 1] = fresh
			a = fresh
		else
			a.squat_end = state.tick
		end
		if bit32.btest(fighter.buttons_pressed, LR_MASK) then
			a.early_tick = state.tick
		end
		return
	end

	if a.phase == wavedashes.NONE then
		return
	end

	if a.phase == wavedashes.SQUAT then
		if action == tgg.action.LANDING_FALL_SPECIAL then
			-- Frame-1 wavedash: KneeBend -> Jump -> EscapeAir -> LandingFallSpecial in one tick
			a.phase = wavedashes.SLIDING
			a.dodge_frames = math.max(1, state.tick - a.squat_end)
			a.angle, a.has_angle = capture_angle(fighter.stick_x, fighter.stick_y)
		elseif action == tgg.action.ESCAPE_AIR then
			-- Frame-1 airdodge still airborne at end of frame
			a.phase = wavedashes.DODGING
			a.dodge_frames = math.max(1, state.tick - a.squat_end)
			a.angle, a.has_angle = capture_angle(fighter.stick_x, fighter.stick_y)
		elseif is_jump_or_fall(action) then
			a.phase = wavedashes.AIRBORNE
			a.airborne_ticks = 1
		else
			-- Jump-cancel grab, up-smash, or hitstun: not a wavedash attempt
			a.phase = wavedashes.NONE
		end
	elseif a.phase == wavedashes.AIRBORNE then
		a.airborne_ticks += 1
		local since = state.tick - a.squat_end
		if action == tgg.action.LANDING_FALL_SPECIAL then
			if since <= MAX_DODGE_WINDOW then
				a.phase = wavedashes.SLIDING
				a.dodge_frames = math.max(1, since)
				a.angle, a.has_angle = capture_angle(fighter.stick_x, fighter.stick_y)
			else
				a.phase = wavedashes.NONE
			end
		elseif action == tgg.action.ESCAPE_AIR then
			if since <= MAX_DODGE_WINDOW then
				a.phase = wavedashes.DODGING
				a.dodge_frames = math.max(1, since)
				a.angle, a.has_angle = capture_angle(fighter.stick_x, fighter.stick_y)
			else
				a.phase = wavedashes.NONE
			end
		elseif action == tgg.action.JUMP_F or action == tgg.action.JUMP_B then
			if a.early_tick > 0 and a.airborne_ticks >= EARLY_CONFIRM then
				local r = wavedashes.result()
				r.kind = wavedashes.EARLY
				r.frames = math.max(1, (a.squat_end + 1) - a.early_tick)
				decide(port, r)
			elseif since > MAX_DODGE_WINDOW then
				a.phase = wavedashes.NONE
			end
		elseif fighter.grounded and a.early_tick > 0 then
			local r = wavedashes.result()
			r.kind = wavedashes.EARLY
			r.frames = math.max(1, (a.squat_end + 1) - a.early_tick)
			decide(port, r)
		else
			-- Aerial attack, double jump, special, hitstun, or ledge grab: not a wavedash
			a.phase = wavedashes.NONE
		end
	elseif a.phase == wavedashes.DODGING then
		if action == tgg.action.LANDING_FALL_SPECIAL then
			a.phase = wavedashes.SLIDING
		elseif action ~= tgg.action.ESCAPE_AIR then
			-- Airdodged without reaching the ground
			local r = wavedashes.result()
			r.kind = wavedashes.AIR
			r.frames = a.dodge_frames
			r.angle = a.angle
			r.has_angle = a.has_angle
			decide(port, r)
		end
	elseif a.phase == wavedashes.SLIDING then
		if action ~= tgg.action.LANDING_FALL_SPECIAL then
			-- First actionable frame after the 10-frame waveland slide
			local r = wavedashes.result()
			r.kind = wavedashes.LAND
			r.frames = a.dodge_frames
			r.angle = a.angle
			r.has_angle = a.has_angle
			r.distance = math.floor(math.abs(fighter.x - a.start_x) * 10 + 0.5) / 10
			decide(port, r)
		end
	end
end)
