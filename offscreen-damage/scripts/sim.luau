--!strict
-- Offscreen damage: a fighter takes offscreen damage whenever it's outside the stage's camera
-- bounds, the way Brawl does it, instead of while the game shows it in a magnifying bubble. The
-- damage, the delay between ticks and the percent where it stops are the game's own. Matches
-- Slippi's online code (slippi-ssbm-asm Online/Core/BrawlOffscreenDamage.asm).
--
-- The game asks each frame whether the fighter counts as off screen (fighter_check's
-- tgg.check.OFFSCREEN_DAMAGE); this answers from the camera bounds. The bubbles on screen still
-- come from the game's own answer.
local tgg = require("@tgg")

local function outside_camera(fighter: tgg.Fighter): boolean
	-- The Home-Run Contest's Sandbag never takes offscreen damage
	if tgg.mode.current() == tgg.mode.HOME_RUN_CONTEST then
		return false
	end
	-- Nor does a fighter already KO'd, or flying into the screen or the distance on a star or
	-- screen KO
	local action = fighter.action
	if fighter.out_of_play or action == tgg.action.DEAD_UP_STAR or action == tgg.action.DEAD_UP_FALL then
		return false
	end
	local x, y = fighter.x, fighter.y
	return x < tgg.stage.camera_left()
		or x > tgg.stage.camera_right()
		or y > tgg.stage.camera_top()
		or y < tgg.stage.camera_bottom()
end

tgg.change.fighter_check(function(fighter, check)
	if check.kind == tgg.check.OFFSCREEN_DAMAGE then
		check.pass = outside_camera(fighter)
	end
end)
