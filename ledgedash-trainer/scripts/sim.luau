--!strict
-- Ledge dash trainer: after each ledge dash (let go of the ledge, jump, air dodge onto the stage),
-- a breakdown above the player's percent: the frames on the ledge, from the drop to the jump and
-- from the jump to the air dodge, and how much of the ledge's intangibility was left once the
-- landing lag ended. That last number is the one that matters: above zero, nobody could hit you
-- the moment you could act.
--
-- It follows each fighter's action state in fighter_update: hanging (CLIFF_CATCH, CLIFF_WAIT),
-- off the ledge, a jump (jumps used goes up), ESCAPE_AIR, then LANDING_FALL_SPECIAL, the
-- waveland's landing. The intangibility is the game's own timer (fighter.intangible), which the
-- ledge grab sets and the fighter's update counts down.
--
-- This file only reads, so the mod is display. The attempt in progress and each result are in
-- state, so they're right through a rollback; view.luau shows a result once its frame is final.
local tgg = require("@tgg")
local ledges = require("./ledges")

local state = tgg.state(ledges.defaults())

-- No air dodge this long after the drop: not a ledge dash
local GIVE_UP = 60

tgg.on.match_start(function()
	local fresh = ledges.defaults()
	state.tick = fresh.tick
	state.at = fresh.at
	state.res = fresh.res
end)

tgg.on.tick(function()
	if tgg.in_match() then
		state.tick += 1
	end
end)

local function decide(port: number, r: ledges.Result)
	r.seq = state.res[port + 1].seq + 1
	r.frame = tgg.frame()
	state.res[port + 1] = r
	state.at[port + 1].phase = ledges.NONE
	if ledges.showing() then
		local a, b = ledges.describe(r)
		tgg.log(`P{port + 1}: {a}{if b ~= "" then ", " else ""}{b}`) -- dropped during a re-run
	end
end

local function on_ledge(action: number): boolean
	return action == tgg.action.CLIFF_CATCH or action == tgg.action.CLIFF_WAIT
end

-- The ledge's own options: getting up, rolling, attacking or jumping from it
local function ledge_option(action: number): boolean
	return action >= tgg.action.CLIFF_CLIMB_SLOW and action <= tgg.action.CLIFF_JUMP_QUICK2
end

tgg.on.fighter_update(function(fighter)
	local port = fighter.port
	if port < 0 or port >= 4 or fighter.follower then
		return
	end
	local a = state.at[port + 1]
	local action = fighter.action
	if on_ledge(action) then
		if a.phase ~= ledges.LEDGE then
			local fresh = ledges.attempt()
			fresh.phase = ledges.LEDGE
			fresh.grab = state.tick
			state.at[port + 1] = fresh
		end
		return
	end
	if a.phase == ledges.NONE then
		return
	end
	if fighter.intangible == 0 and a.gone == 0 then
		a.gone = state.tick
	end
	if (a.phase == ledges.DROPPED or a.phase == ledges.JUMPED) and state.tick - a.drop > GIVE_UP then
		a.phase = ledges.NONE
		return
	end
	if a.phase == ledges.LEDGE then
		if ledge_option(action) then
			a.phase = ledges.NONE
		else
			a.phase = ledges.DROPPED
			a.drop = state.tick
			a.jumps = fighter.jumps_used
		end
	elseif a.phase == ledges.DROPPED or a.phase == ledges.JUMPED then
		if a.phase == ledges.DROPPED and fighter.jumps_used > a.jumps then
			a.phase = ledges.JUMPED
			a.jump = state.tick
		end
		if action == tgg.action.ESCAPE_AIR then
			a.phase = ledges.DODGED
			a.dodge = state.tick
		elseif fighter.grounded then
			a.phase = ledges.NONE -- landed some other way
		end
	elseif a.phase == ledges.DODGED then
		if action == tgg.action.LANDING_FALL_SPECIAL then
			a.phase = ledges.LANDED
		elseif action ~= tgg.action.ESCAPE_AIR then
			local r = ledges.result()
			r.kind = ledges.NO_STAGE
			decide(port, r)
		end
	elseif a.phase == ledges.LANDED then
		if action ~= tgg.action.LANDING_FALL_SPECIAL then
			-- The first frame the fighter can act
			local r = ledges.result()
			r.kind = ledges.DASH
			r.ledge = a.drop - a.grab
			r.to_jump = if a.jump ~= 0 then a.jump - a.drop else -1
			r.to_dodge = a.dodge - (if a.jump ~= 0 then a.jump else a.drop)
			local left = fighter.intangible
			r.left = if left > 0 then left else -(state.tick - a.gone)
			decide(port, r)
		end
	end
end)
