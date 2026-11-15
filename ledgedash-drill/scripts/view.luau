--!strict
-- Ledge dash drill: Practice > Ledge dash. Training Mode with nobody else and a clean screen
-- (tgg.practice-stage); the first time you hang on a ledge that's the reset point, and every try
-- goes back to it, so a rep is just the ledge dash itself.
--
-- A try starts when the fighter leaves the ledge and is over when they're standing on the stage
-- and can act, when they're KO'd, or after TRY_MAX displayed frames. A beat later (BEAT frames,
-- to read the result) it resets. D-pad left while hanging moves the reset point there; D-pad
-- right resets now.
--
-- It tells for itself when a try is over, so it works without tgg.ledgedash-trainer; with it
-- installed, the trainer's breakdown shows for each try.
local tgg = require("@tgg")

local ID = "tgg.ledgedash-drill"
-- Displayed frames a try may take, and the beat before the reset
local TRY_MAX = 150
local BEAT = 40

local practice = assert(tgg.mod("tgg.practice-stage"))

tgg.menu.add("practice/ledgedash", {
	label = "Ledge dash",
	description = "Grab the ledge once. After each try you're put back on it.",
	action = function()
		practice.start(ID, "melee/final-destination", false, false)
	end,
})

local WAITING, HANGING, TRYING, DONE = 0, 1, 2, 3
local phase = WAITING
local frames = 0 -- displayed frames in this phase

local function on_ledge(action: number): boolean
	return action == tgg.action.CLIFF_CATCH or action == tgg.action.CLIFF_WAIT
end

local function ko(action: number): boolean
	return action >= tgg.action.DEAD_DOWN and action <= tgg.action.REBIRTH
end

-- Standing on the stage, past any landing lag
local function standing(fighter: tgg.Fighter): boolean
	local action = fighter.action
	return fighter.grounded
		and action ~= tgg.action.LANDING_FALL_SPECIAL
		and action ~= tgg.action.LANDING
		and not on_ledge(action)
end

-- What went down on any controller since the last displayed frame
local function pressed(): number
	local any = 0
	for port = 0, 3 do
		local pad = tgg.input.pad(port)
		if pad then
			any = bit32.bor(any, pad.pressed)
		end
	end
	return any
end

local function enter(next: number)
	phase = next
	frames = 0
end

tgg.on.frame(function()
	local fighter = if tgg.in_match() then tgg.fighter.at(0, false) else nil
	if practice.drill() ~= ID or fighter == nil then
		enter(WAITING)
		return
	end
	local action = fighter.action
	local input = pressed()
	frames += 1
	if phase == WAITING then
		-- the first hang: from the catch, which lasts a few frames
		if on_ledge(action) then
			practice.reset_here()
			enter(HANGING)
		end
	elseif phase == HANGING then
		if not on_ledge(action) then
			enter(TRYING)
		elseif bit32.btest(input, tgg.button.LEFT) and action == tgg.action.CLIFF_WAIT then
			practice.reset_here()
		end
	elseif phase == TRYING then
		if standing(fighter) or ko(action) or frames >= TRY_MAX then
			enter(DONE)
		end
	elseif phase == DONE then
		if frames >= BEAT and practice.reset(false) then
			enter(HANGING)
		end
	end
	if bit32.btest(input, tgg.button.RIGHT) and phase ~= WAITING and practice.reset(true) then
		enter(HANGING)
	end
end)
