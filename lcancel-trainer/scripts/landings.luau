--!strict
-- What sim.luau decides for each landing, and how view.luau shows it: both files require it.
local tgg = require("@tgg")

local landings = {}

landings.CANCEL = 1
landings.EARLY = 2
landings.LATE = 3
landings.MISSED = 4

-- The simulation's facts for one port
export type Port = {
	waiting: boolean, -- landed without a press in time: watching for a late one
	landed: number, -- the tick of that landing
}

function landings.port(): Port
	return { waiting = false, landed = 0 }
end

-- A landing's result, which sim.luau posts (tgg.post) and view.luau counts
export type Result = {
	port: number,
	result: number,
	frames: number, -- frames early or late
}

-- sim.luau's state: the logic frames into the match, and each port's facts (ports 1 to 4)
function landings.defaults()
	return {
		tick = 0,
		port = { landings.port(), landings.port(), landings.port(), landings.port() },
	}
end

function landings.showing(): boolean
	return tgg.in_match() and (tgg.settings.where == "Every match" or tgg.mode.current() == tgg.mode.TRAINING)
end

function landings.label(result: number, frames: number): string
	local s = if frames == 1 then "" else "s"
	if result == landings.CANCEL then
		return "L-cancel"
	elseif result == landings.EARLY then
		return `{frames} frame{s} early`
	elseif result == landings.LATE then
		return `{frames} frame{s} late`
	end
	return "Missed"
end

return landings
