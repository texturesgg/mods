--!strict
-- L-cancel trainer: after every aerial landing that could be L-canceled, a line by the player's
-- percent says whether it was, and by how many frames it was missed, with the running rate.
--
-- It reads the game's own decision. When a fighter lands from an aerial, the game halves the
-- landing lag if L, R or Z was pressed fewer than the window (7) frames before; fighter_land
-- passes both, so "early" is exact and analog presses count as the game counts them. A press
-- during the landing lag after a miss counts as late, by the frames since the landing.
--
-- This file only reads, so the mod is display. Each landing's result is in state, so it's right
-- through a rollback; view.luau counts the rate once a landing's frame is final.
local tgg = require("@tgg")
local landings = require("./landings")

local state = tgg.state(landings.defaults())

-- A press further back than this before landing isn't "early", just missed
local EARLY_MAX = 20

local function decide(port: number, result: number, frames: number)
	local p = state.port[port + 1]
	p.seq += 1
	p.result = result
	p.frames = frames
	p.frame = tgg.frame()
	p.waiting = false
	if landings.showing() then
		tgg.log(`P{port + 1}: {landings.label(result, frames)}`) -- dropped during a re-run
	end
end

local function is_aerial(action: number): boolean
	return action >= tgg.action.ATTACK_AIR_N and action <= tgg.action.ATTACK_AIR_LW
end

local function is_aerial_landing(action: number): boolean
	return action >= tgg.action.LANDING_AIR_N and action <= tgg.action.LANDING_AIR_LW
end

tgg.on.match_start(function()
	local fresh = landings.defaults()
	state.tick = fresh.tick
	state.port = fresh.port
end)

tgg.on.tick(function()
	if tgg.in_match() then
		state.tick += 1
	end
end)

-- A fighter touches down: the game is about to pick the landing lag. Without landing lag it
-- auto-cancels and there's nothing to L-cancel. Nana follows Popo.
tgg.on.fighter_land(function(fighter, land)
	local port = fighter.port
	if port < 0 or port >= 4 or fighter.follower or not land.lag_applies or not is_aerial(land.action) then
		return
	end
	local since, window = land.press_age, land.window
	if since < window then
		decide(port, landings.CANCEL, 0)
	elseif since - window < EARLY_MAX then
		decide(port, landings.EARLY, since - window + 1)
	else
		local p = state.port[port + 1]
		p.waiting = true
		p.landed = state.tick
	end
end)

-- While a missed landing's lag lasts, a press is late
tgg.on.fighter_update(function(fighter)
	local port = fighter.port
	if port < 0 or port >= 4 or fighter.follower or not state.port[port + 1].waiting then
		return
	end
	if not is_aerial_landing(fighter.action) then
		decide(port, landings.MISSED, 0)
	elseif fighter.cancel_press_age == 0 then
		decide(port, landings.LATE, state.tick - state.port[port + 1].landed)
	end
end)
