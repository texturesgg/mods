--!strict
-- UCF 0.84: the Universal Controller Fix as Slippi 3.17 and later run it.
--
-- UCF makes the game read what a player meant from a stick that different controllers report
-- differently. This follows the eight codes of Slippi's build of UCF 0.84 (slippi-ssbm-asm,
-- External/UCF 0.84/UCF/), whose logic and constants it reproduces at the same points in the
-- game's own code: fighter_input, fighter_turn and fighter_check.
--
-- The game's floats are 32-bit and Luau's numbers 64-bit. Stick values and the game's thresholds
-- arrive exactly; constants compared against them are the 32-bit ones (tgg.f32), and each float
-- the codes compute is rounded the way the game would hold it.
--
-- Not here: the CSS indicator text (Indicator/), which has no counterpart in the port's menus yet.
local tgg = require("@tgg")

-- The last four raw stick values of each controller port, newest at `index`, and a count of how
-- long a stick has been flicked down and held (the extended shield drop's)
local BUFFER = 4

local function new_port()
	return { x = { 0, 0, 0, 0 }, y = { 0, 0, 0, 0 }, index = 0, down = 0 }
end

local state = tgg.state({ pad = { new_port(), new_port(), new_port(), new_port() } })

-- Zelda's Farore's Wind start, the eighth of her own action states
local ZELDA_SPECIAL_HI_START = tgg.action.FIGHTER_FIRST + 8

local K_0001 = tgg.f32(0.0001)
local K_59 = tgg.f32(0.59)
local K_8 = tgg.f32(0.8)

-- The raw stick value `back` entries before the newest
local function raw_x(port: number, back: number): number
	local p = state.pad[port + 1]
	return p.x[(p.index - back) % BUFFER + 1]
end

local function raw_y(port: number, back: number): number
	local p = state.pad[port + 1]
	return p.y[(p.index - back) % BUFFER + 1]
end

-- How far each raw axis moved in two frames, squared: the flick tests
local function flick_x(port: number): number
	local d = raw_x(port, 0) - raw_x(port, 2)
	return d * d
end

local function flick_y(port: number): number
	local d = raw_y(port, 0) - raw_y(port, 2)
	return d * d
end

-- C's (int) cast: toward zero
local function trunc(v: number): number
	return if v < 0 then math.ceil(v) else math.floor(v)
end

-- A stick reading as raw units, rounded down the way the codes do it: the square of
-- (|v| * 80 - 0.0001) + 2 of each axis summed, which is above 80 * 80 on the rim. |v| * 80 and
-- the subtraction are exact in 64 bits, so one rounding to 32 is the codes' fused multiply-add.
local function rim_squared(x: number, y: number): number
	local rx = trunc(tgg.f32(math.abs(x) * 80 - K_0001)) + 2
	local ry = trunc(tgg.f32(math.abs(y) * 80 - K_0001)) + 2
	return rx * rx + ry * ry
end

-- A cardinal: one axis at the rim and the other within 6 of center. Returns the snapped x and y,
-- or nil when it isn't one.
local function snap_cardinal(rx: number, ry: number): (number?, number?)
	local snap = 6
	if (rx <= -80 or rx >= 80) and ry >= -snap and ry <= snap then
		return if rx < 0 then -1 else 1, 0
	elseif (ry <= -80 or ry >= 80) and rx >= -snap and rx <= snap then
		return 0, if ry < 0 then -1 else 1
	end
	return nil, nil
end

-- UCF Pad Buffer + 1.0 Cardinals.asm (Fighter_procInput, after the stick timers): records the raw
-- stick the fighter's input was read from, snaps the main stick's and C-stick's cardinals to
-- exactly 1.0 (Zelda's Farore's Wind start excepted), and counts the frames a stick flicked down
-- stays down. A computer-controlled fighter (Nana) has no controller and none of it.
tgg.change.fighter_input(function(fighter, input)
	local port = fighter.controller
	if fighter.cpu or port < 0 then
		return
	end
	local p = state.pad[port + 1]
	p.index = (p.index + 1) % BUFFER
	p.x[p.index + 1] = tgg.pad.stick_x(port)
	p.y[p.index + 1] = tgg.pad.stick_y(port)

	-- Farore's Wind aims with the stick's exact value
	if not (fighter.behaves_as == "melee/zelda" and fighter.action == ZELDA_SPECIAL_HI_START) then
		local x, y = snap_cardinal(tgg.pad.stick_x(port), tgg.pad.stick_y(port))
		if x and y then
			input.stick_x = x
			input.stick_y = y
		end
		x, y = snap_cardinal(tgg.pad.cstick_x(port), tgg.pad.cstick_y(port))
		if x and y then
			input.cstick_x = x
			input.cstick_y = y
		end
	end

	-- The count starts on a flick down (the stick moved more than 44 raw units in two frames,
	-- within a frame of crossing the smash threshold), grows while the stick stays down on the rim
	-- past -0.609375, and goes back to zero when it doesn't. It wraps with its byte.
	local down = 0
	if input.stick_y <= -0.609375 and rim_squared(input.stick_x, input.stick_y) > 80 * 80 then
		down = p.down
		if down ~= 0 then
			down = (down + 1) % 256
		elseif fighter.stick_y_age <= 1 and flick_y(port) > 44 * 44 then
			down = 1
		end
	end
	p.down = down
end)

-- UCF Dashback.asm (ftCo_Turn_IASA, where a tilt turn flips its facing): on the second frame of a
-- tilt turn of a controlled fighter, a stick that crossed the smash threshold in the last two
-- frames (the raw x moved more than 75 units in two frames) while meeting the game's own smash
-- conditions makes it a smash turn, and Nana dashes back as well.
tgg.change.fighter_turn(function(fighter, turn)
	if fighter.follower or fighter.action_frame ~= 2 then
		return
	end
	-- The game's smash conditions, as its own check reads them
	local port = fighter.controller
	if
		fighter.stick_x * fighter.facing < tgg.pad.dash_threshold()
		or fighter.stick_x_age > 1
		or port < 0
		or flick_x(port) <= 75 * 75
	then
		return
	end
	turn.smash = true
	turn.follower_too = true
end)

-- UCF Shield Drop.asm (ftCo_80099894, the spot dodge): the spot dodge is held off while a stick
-- being rolled down the rim to straight down (the Axe method) passes through the spot dodge zone
-- first: standing on a platform, the stick held sideways for the roll window, no C-stick spot
-- dodge, the stick on the rim and not yet at -0.8.
local function blocks_spot_dodge(fighter: tgg.Fighter): boolean
	if fighter.cstick_y <= tgg.pad.spot_dodge_threshold() or fighter.stick_x_age < tgg.pad.roll_frames() then
		return false
	end
	if fighter.stick_y <= -K_8 or not fighter.on_platform then
		return false
	end
	return rim_squared(fighter.stick_x, fighter.stick_y) > 80 * 80
end

-- The game's input checks the codes change, each at the point of its own injection:
--
-- - UCF Shield Drop.asm: the two checks that start a spot dodge (from a standing state with the
--   shield button down, and from the shield) fail while blocks_spot_dodge holds.
-- - UCF SDI.asm (ftCo_Damage_OnEveryHitlag) and UCF Shield SDI.asm (ftCo_80093240): smash DI that
--   the window test refuses (the stick was already past the threshold) is allowed for a stick
--   that was just flicked.
-- - UCF Tumble.asm (ftCo_DamageFall_IASA): the window test is the frame the stick crossed or the
--   one after, and on the one after the stick must have been below the threshold before and
--   flicked.
-- - UCF DBOOC SquatRv Fix.asm (ftCo_SquatRv_CheckInput): the crouch releases at -0.59 instead when
--   the stick has just crossed the smash threshold and sits on the rim.
-- - UCF Shield Drop Extended.asm (ftCo_8009A080): the stick counts as down for dropping through a
--   platform with the shield held when it was flicked down and held for more than a frame.
tgg.change.fighter_check(function(fighter, check)
	local kind = check.kind
	local port = fighter.controller
	local have_pad = port >= 0

	if kind == tgg.check.SPOT_DODGE or kind == tgg.check.SPOT_DODGE_SHIELD then
		if check.pass and blocks_spot_dodge(fighter) then
			check.pass = false
		end
	elseif kind == tgg.check.SDI then
		-- An axis crossed in the last frame, the stick before it inside the threshold, and it
		-- moved at least 62 raw units in two frames
		if check.pass or (fighter.stick_x_age_kept > 1 and fighter.stick_y_age_kept > 1) then
			return
		end
		local prev_x, prev_y = fighter.stick_prev_x, fighter.stick_prev_y
		-- fmaf(prev_x, prev_x, prev_y * prev_y) in 32 bits
		local prev_squared = tgg.f32(prev_x * prev_x + tgg.f32(prev_y * prev_y))
		local min = tgg.pad.sdi_threshold()
		if tgg.f32(min * min) <= prev_squared or not have_pad then
			return
		end
		check.pass = flick_x(port) + flick_y(port) > 62 * 62
	elseif kind == tgg.check.SHIELD_SDI then
		-- The same from the x axis alone, which isn't compared by magnitude
		if
			check.pass
			or fighter.stick_x_age_kept > 1
			or fighter.stick_prev_x >= tgg.pad.sdi_threshold()
			or not have_pad
		then
			return
		end
		check.pass = flick_x(port) > 62 * 62
	elseif kind == tgg.check.TUMBLE_FALL then
		local age = fighter.stick_x_age
		if age ~= 1 then
			check.pass = age < 1
		else
			check.pass = have_pad
				and math.abs(fighter.stick_prev_x) < tgg.pad.tumble_threshold()
				and flick_x(port) > 75 * 75
		end
	elseif kind == tgg.check.SQUAT_RELEASE then
		if fighter.stick_x_age < 1 and rim_squared(fighter.stick_x, fighter.stick_y) > 80 * 80 then
			check.pass = fighter.stick_y > -K_59
		end
	elseif kind == tgg.check.PLATFORM_DROP then
		if not check.pass and have_pad then
			check.pass = state.pad[port + 1].down > 1
		end
	end
end)
