--!strict
-- Camera-free Stadium monitor: Pokémon Stadium's big screen leaves its close-up of a fighter when
-- the fighter's position leaves fixed bounds (x -120 to 120, y -20 to 80), instead of when the
-- close-up would leave the camera's view. Leaving the close-up draws from the RNG, so with the
-- game's own rule the camera, which each player's game draws differently, can change the
-- simulation. Matches Slippi's code (slippi-ssbm-asm Common/PSCameraIndependentMonitor), which
-- runs in every Slippi game.
--
-- The close-up still goes where the game puts it; whether it stays comes from the bounds
-- (fighter_check's tgg.check.STADIUM_CLOSE_UP).
local tgg = require("@tgg")

tgg.change.fighter_check(function(fighter, check)
	if check.kind ~= tgg.check.STADIUM_CLOSE_UP then
		return
	end
	local x, y = fighter.x, fighter.y
	check.pass = not (x < -120 or x > 120 or y > 80 or y < -20)
end)
