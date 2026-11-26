--!strict
-- Live controller input display: captures each fighter's sticks, buttons, and trigger from the
-- simulation so the overlay works in matches, training, practice drills, and .slp replays alike.
--
-- Read-only: never modifies the simulation, so the port classifies this mod as display (safe for
-- online play).
local tgg = require("@tgg")
local inputs = require("./inputs")

local state = tgg.state(inputs.defaults())

-- Clear per-match state in match_setup (the first fighter_spawn fires before match_start)
tgg.on.match_setup(function()
	local fresh = inputs.defaults()
	state.tick = fresh.tick
	state.ports = fresh.ports
end)

tgg.on.tick(function()
	if tgg.in_match() then
		state.tick += 1
	end
end)

tgg.on.fighter_update(function(fighter)
	local port = fighter.port
	if port < 0 or port >= 4 or fighter.follower then
		return
	end
	local p = state.ports[port + 1]
	p.active = true
	p.cpu = fighter.cpu
	p.controller = fighter.controller
	p.sx = fighter.stick_x
	p.sy = fighter.stick_y
	p.cx = fighter.cstick_x
	p.cy = fighter.cstick_y
	p.buttons = fighter.buttons
	p.trigger = fighter.trigger
end)
