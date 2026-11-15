--!strict
-- Competitive rules: VS mode starts with the competitive standard (4 stocks, 8:00, items off,
-- team attack on) instead of the game's 2-minute time match with items. The game's own rules
-- screens are where they change.
--
-- The game sets the VS rules to its defaults every start (rules_defaults); the mod sets the
-- competitive ones then. The item switch is in the save, so a change there stays: the mod only
-- turns items off on a save whose item switch the player never touched.
local tgg = require("@tgg")

-- At start, and when the player erases their data
tgg.on.rules_defaults(function()
	tgg.save.set_rule(tgg.rule.MODE, tgg.rule_mode.STOCK)
	tgg.save.set_rule(tgg.rule.STOCKS, 4)
	tgg.save.set_rule(tgg.rule.STOCK_TIME_LIMIT, 8)
	tgg.save.set_rule(tgg.rule.HANDICAP, 0)
	tgg.save.set_rule(tgg.rule.DAMAGE_RATIO, 10) -- 1.0
	tgg.save.set_rule(tgg.rule.RANDOM_STAGE, 0) -- on
	tgg.save.set_rule(tgg.rule.FRIENDLY_FIRE, 1) -- doubles is played with it on
	tgg.save.set_rule(tgg.rule.PAUSE, 1)
	tgg.save.set_rule(tgg.rule.SCORE_DISPLAY, 0)
	tgg.save.set_rule(tgg.rule.SELF_DESTRUCTS, 0) -- -1
	tgg.save.set_rule(tgg.rule.ITEM_FREQUENCY, -1)
end)

-- The save loads, or a new one is made: its item switch replaces ours unless it's the game's
-- default, which nobody chose
tgg.on.save_loaded(function()
	if tgg.save.rule(tgg.rule.ITEM_FREQUENCY) == tgg.save.rule_default(tgg.rule.ITEM_FREQUENCY) then
		tgg.save.set_rule(tgg.rule.ITEM_FREQUENCY, -1)
	end
end)
