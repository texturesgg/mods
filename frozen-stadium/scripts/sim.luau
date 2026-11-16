--!strict
-- Frozen Stadium: Pokémon Stadium never transforms, and stays the neutral stage for the whole
-- match. Matches Slippi's frozen Stadium toggle
-- (slippi-ssbm-asm Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm).
--
-- Each frame the stage asks whether it may go on with its transformations (stage_check's
-- tgg.check.STADIUM_TRANSFORM). The game says no only in Training Mode. This says no always.
local tgg = require("@tgg")

tgg.change.stage_check(function(check)
	if check.kind == tgg.check.STADIUM_TRANSFORM then
		check.pass = false
	end
end)
