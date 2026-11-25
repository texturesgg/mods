--!strict
-- What sim.luau decides for each ledge dash, and how view.luau words it: both files require it.
local tgg = require("@tgg")

local ledges = {}

ledges.DASH = 1
ledges.NO_STAGE = 2

-- The attempt in progress: hanging, let go with no jump yet, jumped with no air dodge yet, air
-- dodging, in the waveland's landing lag
ledges.NONE = 0
ledges.LEDGE = 1
ledges.DROPPED = 2
ledges.JUMPED = 3
ledges.DODGED = 4
ledges.LANDED = 5

export type Attempt = {
	phase: number,
	grab: number, -- the ticks each happened on
	drop: number,
	jump: number,
	dodge: number,
	jumps: number, -- jumps used when the fighter let go
	gone: number, -- the tick the intangibility ran out, 0 while it lasts
	facing: number, -- facing direction on the ledge (+1 left ledge, -1 right ledge)
}

-- An attempt's result, which sim.luau posts (tgg.post) and view.luau shows
export type Result = {
	port: number,
	kind: number, -- DASH or NO_STAGE
	ledge: number, -- frames on the ledge
	to_jump: number, -- drop to jump, -1 with no jump
	to_dodge: number, -- jump (or drop) to air dodge
	left: number, -- intangible frames left once actionable; below 0, frames short
}

function ledges.attempt(): Attempt
	return { phase = ledges.NONE, grab = 0, drop = 0, jump = 0, dodge = 0, jumps = 0, gone = 0, facing = 0 }
end

function ledges.result(): Result
	return { port = 0, kind = 0, ledge = 0, to_jump = 0, to_dodge = 0, left = 0 }
end

-- sim.luau's state: the logic frames into the match, and each port's attempt (ports 1 to 4)
function ledges.defaults()
	return {
		tick = 0,
		at = { ledges.attempt(), ledges.attempt(), ledges.attempt(), ledges.attempt() },
	}
end

function ledges.showing(): boolean
	return tgg.in_match() and (tgg.settings.where == "Every match" or tgg.mode.current() == tgg.mode.TRAINING)
end

-- The result's two lines; the second is empty for a missed stage
function ledges.describe(r: Result): (string, string)
	if r.kind == ledges.NO_STAGE then
		return "Air dodge missed the stage", ""
	end
	local line1
	if r.left > 0 then
		line1 = `Intangible: {r.left} frame{if r.left == 1 then "" else "s"} left`
	elseif r.left == 0 then
		line1 = "No intangibility left"
	else
		line1 = `Too slow by {-r.left} frame{if r.left == -1 then "" else "s"}`
	end
	if r.to_jump >= 0 then
		return line1, `Ledge {r.ledge}   Jump {r.to_jump}   Air dodge {r.to_dodge}`
	end
	return line1, `Ledge {r.ledge}   Air dodge {r.to_dodge}`
end

return ledges
