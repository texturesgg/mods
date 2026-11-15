--!strict
-- Unlock everything: every character, stage, unlockable and trophy flag is unlocked.
--
-- It runs whenever the save is ready, loaded from the memory card or made new (no save on the
-- card, or erasing it), with the game's own unlock-everything calls, the ones its debug build
-- runs. Unlocking is one way, so a save that already has some of it loses nothing, and the log
-- says when a loaded save gained something. It touches only save data. To unlock things by
-- playing, turn it off.
local tgg = require("@tgg")

tgg.on.save_loaded(function()
	if tgg.save.unlock_all() then
		tgg.log("unlocked what the save still had locked")
	end
end)
