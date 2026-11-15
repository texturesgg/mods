--!strict
-- UCF (Slippi 3.16 and earlier): the Universal Controller Fix as Slippi's builds up to 3.16 ran
-- it, the three codes of slippi-ssbm-asm's External/UCF 0.8/Logic/: dashback (UCF DB.asm), shield
-- drop (UCF SD.asm) and tumble wiggle (UCF Tumble.asm). It reproduces their logic and constants at
-- the same points in the game's own code: fighter_turn and fighter_check.
--
-- Slippi 3.17 moved to UCF 0.84 (tgg.ucf). Play with one or the other: both change the same
-- checks.
--
-- The game's floats are 32-bit and Luau's numbers 64-bit. Stick values and the game's thresholds
-- arrive exactly; constants compared against them are the 32-bit ones (tgg.f32), and each float
-- the codes compute is rounded the way the game would hold it.
local tgg = require("@tgg")

-- The last four raw stick x values of each controller port, newest at `index`. The codes read
-- the game's pad queue: the newest entry and the one two before it, this frame's and the one two
-- frames ago. This keeps them itself, recorded once per logic frame.
local HISTORY = 4

local state = tgg.state({
	x = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
	index = 0,
})

local K_8 = tgg.f32(0.8)
-- UCF SD.asm's rounding: 0.00001 as the code holds it
local K_00001 = tgg.f32(9.953975677490234e-06)

-- Each logic frame, after the game read its controllers: this frame's raw input
tgg.on.tick(function()
	state.index = (state.index + 1) % HISTORY
	for port = 0, 3 do
		state.x[port + 1][state.index + 1] = tgg.pad.stick_x(port)
	end
end)

-- How far a port's raw stick x moved in two frames, squared
local function flick_x(port: number): number
	if port < 0 then
		return 0
	end
	local x = state.x[port + 1]
	local d = x[state.index + 1] - x[(state.index - 2) % HISTORY + 1]
	return d * d
end

-- C's (int) cast: toward zero
local function trunc(v: number): number
	return if v < 0 then math.ceil(v) else math.floor(v)
end

-- A stick axis in raw units the way UCF SD.asm rounds it: |v| * 80 less 0.00001, truncated, plus
-- 2, each step in 32 bits. |v| * 80 and the subtraction are exact in 64 bits before rounding.
local function rim_units(v: number): number
	local scaled = tgg.f32(math.abs(v) * 80)
	return trunc(tgg.f32(scaled - K_00001)) + 2
end

-- UCF DB.asm (ftCo_Turn_IASA, where a tilt turn flips its facing): on the second frame of the
-- turn, a stick past the dash threshold either way that hasn't been held there for two frames,
-- and whose raw x moved more than 75 units in two frames, makes it a smash turn. Popo's Nana
-- dashes back too.
tgg.change.fighter_turn(function(fighter, turn)
	if fighter.action_frame ~= 2 then
		return
	end
	if
		math.abs(fighter.stick_x) < tgg.pad.dash_threshold()
		or fighter.stick_x_age >= 2
		or fighter.follower
		or flick_x(fighter.controller) <= 75 * 75
	then
		return
	end
	turn.smash = true
	turn.follower_too = true
end)

-- UCF SD.asm (ftCo_80099894, the spot dodge): the spot dodge is held off while the stick, on the
-- rim, has been sideways for more than 3 frames and isn't yet down past -0.8 (rolling it down to
-- shield drop), unless the C-stick asks for the spot dodge. The code compares (units / 80)
-- squared, summed, with 1; for every unit count that's the same as comparing the units squared
-- with 80 squared.
local function blocks_spot_dodge(fighter: tgg.Fighter): boolean
	if fighter.cstick_y <= tgg.pad.spot_dodge_threshold() then
		return false
	end
	local x = rim_units(fighter.stick_x)
	local y = rim_units(fighter.stick_y)
	if x * x + y * y < 80 * 80 then
		return false
	end
	if fighter.stick_x_age <= 3 then
		return false
	end
	return fighter.stick_y > -K_8
end

-- UCF Tumble.asm (ftCo_DamageFall_IASA): a sideways stick ends tumble on the frame it crossed the
-- threshold, or on the frame after if it was inside the threshold the frame before and its raw x
-- moved more than 75 units in two frames.
--
-- UCF SD.asm: the two checks that start a spot dodge, from a standing state with the shield
-- button down and from the shield, fail while blocks_spot_dodge holds. The code returns from
-- whichever one reached the spot dodge as if its own test had failed.
tgg.change.fighter_check(function(fighter, check)
	local kind = check.kind
	if kind == tgg.check.TUMBLE_FALL then
		local age = fighter.stick_x_age
		if age ~= 1 then
			check.pass = age < 1
		else
			check.pass = math.abs(fighter.stick_prev_x) < tgg.pad.tumble_threshold()
				and flick_x(fighter.controller) > 75 * 75
		end
	elseif kind == tgg.check.SPOT_DODGE or kind == tgg.check.SPOT_DODGE_SHIELD then
		if check.pass and blocks_spot_dodge(fighter) then
			check.pass = false
		end
	end
end)
