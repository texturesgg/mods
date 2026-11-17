--!strict
-- Legal stages: random stage select picks only the competitive legal stages (Battlefield, Final
-- Destination, Dream Land, Yoshi's Story, Fountain of Dreams, Pokémon Stadium).
--
-- The game already has a list for this: the Random Stage Switch in the options, kept in the save.
-- Every random pick, the stage select's random button and the Random stage selection rule, asks
-- it. A new save, or one whose list was never changed, has every stage on; this sets the list to
-- the legal stages there. A list the player changed is theirs, and stays as it is.
--
-- The list counts only once the Random Stage Switch is unlocked, which tgg.unlocks does.
local tgg = require("@tgg")

local LEGAL = {
	["melee/battlefield"] = true,
	["melee/final-destination"] = true,
	["melee/dream-land"] = true,
	["melee/yoshis-story"] = true,
	["melee/fountain-of-dreams"] = true,
	["melee/pokemon-stadium"] = true,
}

-- The save is ready, loaded or made new: an untouched list becomes the legal stages
tgg.on.save_loaded(function()
	if not tgg.save.random_stages_untouched() then
		return
	end
	for _, stage in tgg.content.stages() do
		tgg.save.set_random_stage(stage, LEGAL[stage] == true)
	end
end)

-- The stage select shows only the legal stages
tgg.change.stage_offered(function(offered)
	if not LEGAL[offered.stage] then
		offered.state = tgg.offer.OFF
	end
end)
