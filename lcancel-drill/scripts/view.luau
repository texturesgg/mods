--!strict
-- L-cancel drill: Practice > L-cancel. Training Mode with nobody else on the stage and a clean
-- screen (tgg.practice-stage), starting on Final Destination, for SHFFLing on your own. The
-- feedback is tgg.lcancel-trainer's: with it installed, every aerial landing says how the
-- L-cancel went. Without it, the drill says so once.
local tgg = require("@tgg")

local ID = "tgg.lcancel-drill"
-- Displayed frames the missing-trainer line shows
local SHOW = 300

local practice = assert(tgg.mod("tgg.practice-stage"))

tgg.menu.add("practice/lcancel", {
	label = "L-cancel",
	description = "Just you on the stage, with your L-cancel timing after every aerial.",
	action = function()
		practice.start(ID, "melee/final-destination", false, false)
	end,
})

local shown = SHOW
local was_in_match = false

local function mine(): boolean
	return tgg.in_match() and practice.drill() == ID
end

tgg.on.frame(function()
	local in_match = mine()
	if in_match and not was_in_match then
		shown = if tgg.mod("tgg.lcancel-trainer") ~= nil then SHOW else 0
	end
	was_in_match = in_match
	if shown < SHOW then
		shown += 1
	end
end)

tgg.on.hud_draw(function()
	if shown >= SHOW or not mine() then
		return
	end
	local text = "Install the L-cancel trainer to see your timing"
	local w = tgg.draw.text_width(18, text) + 24
	tgg.draw.rect(320 - w / 2, 96, w, 28, { r = 0, g = 0, b = 0, a = 150 })
	tgg.draw.text(320, 100, 18, { r = 255, g = 255, b = 255 }, tgg.align.CENTER, text)
end)
