--!strict
-- Save states: D-pad left saves the match as it is right now, D-pad right puts it back: both
-- fighters, percents, the CPU mid-action, items, the stage, the RNG. One state, kept until the
-- match ends.
--
-- A save and a load are asked for from the frame event and happen at the next boundary between
-- logic frames. The buttons are read from the viewer's controllers (tgg.input.pad), so they
-- never reach the simulation. Off while a replay plays: the replay player has its own controls
-- on the D-pad.
--
-- With tgg.practice-stage installed it's also Practice > Free practice, and it stays off in
-- another drill's match, which has its own resets on the D-pad.
local tgg = require("@tgg")

local ID = "tgg.save-states"
-- Displayed frames a message stays up, the last FADE of them fading
local SHOW = 60
local FADE = 15

local saved: tgg.Snapshot? = nil
local was_in_match = false
local message = ""
local message_age = SHOW

-- The mods it uses when they're there, looked up once every mod has loaded
local practice: { [string]: (...any) -> any }? = nil
local replays: { [string]: (...any) -> any }? = nil
tgg.on.init(function()
	practice = tgg.mod("tgg.practice-stage")
	replays = tgg.mod("tgg.replay-player")
end)

local function say(text: string)
	message = text
	message_age = 0
end

tgg.menu.add("practice/free", {
	label = "Free practice",
	description = "Just you on the stage. D-pad left saves, D-pad right loads.",
	action = function()
		if practice then
			practice.start(ID, "", false, false)
		end
	end,
})

local function usable(): boolean
	local drill = if practice then practice.drill() else nil
	if drill ~= nil then
		return tgg.in_match() and drill == ID
	end
	return tgg.in_match()
		and not (replays ~= nil and replays.playing())
		and (tgg.settings.where == "Every match" or tgg.mode.current() == tgg.mode.TRAINING)
end

-- What went down on any controller since the last displayed frame
local function pressed(): number
	local any = 0
	for port = 0, 3 do
		local pad = tgg.input.pad(port)
		if pad then
			any = bit32.bor(any, pad.pressed)
		end
	end
	return any
end

tgg.on.frame(function()
	if message_age < SHOW then
		message_age += 1
	end
	local in_match = tgg.in_match()
	if not in_match and was_in_match and saved then
		-- a snapshot only loads into its own match
		tgg.snapshot.free(saved)
		saved = nil
	end
	was_in_match = in_match
	if not usable() then
		return
	end
	local input = pressed()
	if bit32.btest(input, tgg.button.LEFT) then
		if saved then
			tgg.snapshot.free(saved)
		end
		saved = tgg.snapshot.save()
		say(if saved then "Saved" else "Couldn't save")
		if saved then
			tgg.log("saved")
		end
	elseif bit32.btest(input, tgg.button.RIGHT) then
		if saved == nil then
			say("Nothing saved yet")
		elseif tgg.snapshot.load(saved) ~= tgg.snapshot.OK then
			say("Couldn't load")
		end
	end
end)

tgg.on.state_loaded(function()
	if usable() then
		say("Loaded")
		tgg.log(`loaded (states take {tgg.snapshot.bytes() // 1048576} MB)`)
	end
end)

tgg.on.hud_draw(function()
	if message_age >= SHOW or not tgg.in_match() then
		return
	end
	local left = SHOW - message_age
	local a = if left < FADE then left / FADE else 1
	local w = tgg.draw.text_width(22, message) + 28
	tgg.draw.rect(320 - w / 2, 96, w, 32, { r = 0, g = 0, b = 0, a = 150 * a })
	tgg.draw.text(320, 100, 22, { r = 255, g = 255, b = 255, a = 255 * a }, tgg.align.CENTER, message)
end)
