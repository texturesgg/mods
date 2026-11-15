--!strict
-- Quick start: the game skips the opening movie and the title screen and starts at the main
-- menu, or at VS mode's or Training mode's character select, as the Slippi and UnclePunch setups
-- do.
--
-- tgg.control.boot_mode does the work: the game goes to the mode the way Start on the title
-- would take it to the main menu, so the unlock checks still run and a waiting challenger still
-- shows up. The memory card scene still runs: a save loads as usual, and a new one is made
-- without a button press. From there the menus are the game's own, so B on a character select
-- goes back to the main menu.
local tgg = require("@tgg")

-- Where the game starts, by the setting's choice
local MODES = {
	["Main menu"] = tgg.mode.MENU,
	["VS mode"] = tgg.mode.VS,
	["Training mode"] = tgg.mode.TRAINING,
}

-- At startup, once the settings are read
tgg.on.init(function()
	tgg.control.boot_mode(MODES[tgg.settings.start] or tgg.mode.MENU)
end)
