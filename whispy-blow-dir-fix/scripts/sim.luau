--!strict
-- Whispy blow direction fix: Whispy Woods blows toward the side most opponents are on, and a
-- KO'd fighter counts for neither side. The stock game counts it where its model is, and a
-- screen KO's model follows the camera, which each player's game draws differently. Matches
-- Slippi's online code (slippi-ssbm-asm Online/Core/WhispyBlowDirFix), in Slippi 3.15 and later.
--
-- The game asks about each fighter it counts (fighter_check's tgg.check.COUNTS_FOR_SIDE). The
-- rest is the game's own: who's an opponent, and the coin flip on a tie.
local tgg = require("@tgg")

tgg.change.fighter_check(function(fighter, check)
	-- The Dead actions, and Sleep right after them, as Slippi's code has it
	if check.kind == tgg.check.COUNTS_FOR_SIDE and fighter.action <= tgg.action.SLEEP then
		check.pass = false
	end
end)
