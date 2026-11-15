--!strict
-- Neutral spawns: on the legal stages, singles players start at the stage's neutral spawn points
-- instead of the game's, and 2v2 teams start side by side. Other stages use the stage's own
-- points in player order. Matches Slippi's NeutralSpawn code (slippi-ssbm-asm
-- External/NeutralSpawn).
--
-- As a match is set up, each player's starting point comes through the player_spawn event: this
-- moves it, and turns the player to face the middle of the stage.
local tgg = require("@tgg")

type Spawn = { x: number, y: number }
type StageSpawns = { singles: { Spawn }, teams: { Spawn } } -- teams: team 1's two players, then team 2's

local function at(x: number, y: number): Spawn
	return { x = x, y = y }
end

local STAGES: { [string]: StageSpawns } = {
	["melee/final-destination"] = {
		singles = { at(-60, 10), at(60, 10), at(-20, 10), at(20, 10) },
		teams = { at(-60, 10), at(-20, 10), at(60, 10), at(20, 10) },
	},
	["melee/battlefield"] = {
		singles = { at(-38.8, 35.2), at(38.8, 35.2), at(0, 8), at(0, 62.4) },
		teams = { at(-38.8, 35.2), at(-38.8, 5), at(38.8, 35.2), at(38.8, 5) },
	},
	["melee/yoshis-story"] = {
		singles = { at(-42, 26.6), at(42, 28), at(0, 46.9), at(0, 4.9) },
		teams = { at(-42, 26.6), at(-42, 5), at(42, 28), at(42, 5) },
	},
	["melee/dream-land"] = {
		singles = { at(-46.6, 37.2), at(47.4, 37.3), at(0, 7), at(0, 58.5) },
		teams = { at(-46.6, 37.2), at(-46.6, 5), at(47.4, 37.3), at(47.4, 5) },
	},
	["melee/fountain-of-dreams"] = {
		singles = { at(-41.25, 21), at(41.25, 27), at(0, 5.25), at(0, 48) },
		teams = { at(-41.25, 21), at(-41.25, 5), at(41.25, 27), at(41.25, 5) },
	},
	["melee/pokemon-stadium"] = {
		singles = { at(-40, 32), at(40, 32), at(70, 7), at(-70, 7) },
		teams = { at(-40, 32), at(-40, 5), at(40, 32), at(40, 5) },
	},
}

-- On another stage, which of the stage's own points each 2v2 player takes
local OTHER_STAGE_TEAMS = { 0, 3, 1, 2 }

local function present(slot: number): boolean
	local player = tgg.player.at(slot)
	return player ~= nil and player.kind ~= tgg.player.NONE
end

local function team_of(slot: number): number
	local player = tgg.player.at(slot)
	return if player ~= nil then player.team else -1
end

-- Where this player goes among the players (0 to 3), or nil to leave the game's spawn alone
local function spawn_order(slot: number, teams: boolean): number?
	if not teams then
		local order = 0
		for i = 0, slot - 1 do
			if present(i) then
				order += 1
			end
		end
		return order
	end
	-- Only 2v2: every team has none or two players
	for team = 0, 2 do
		local members = 0
		for i = 0, 3 do
			if present(i) and team_of(i) == team then
				members += 1
			end
		end
		if members == 1 or members > 2 then
			return nil
		end
	end
	local order = 0
	for team = 0, 2 do
		for i = 0, 3 do
			if present(i) and team_of(i) == team then
				if i == slot then
					return order
				end
				order += 1
			end
		end
	end
	return nil
end

tgg.change.player_spawn(function(spawn)
	if spawn.port >= 5 or tgg.mode.single_player() then
		return
	end
	local mode = tgg.mode.current()
	if mode == tgg.mode.TRAINING or mode == tgg.mode.TARGET_TEST then
		return
	end
	local order = spawn_order(spawn.port, spawn.teams)
	if order == nil or order > 3 then
		return
	end
	local id = tgg.stage.current()
	local stage = if id ~= nil then STAGES[id] else nil
	if stage ~= nil then
		local point = if spawn.teams then stage.teams[order + 1] else stage.singles[order + 1]
		spawn.x = point.x
		spawn.y = point.y
	else
		local index = if spawn.teams then OTHER_STAGE_TEAMS[order + 1] else order
		spawn.x = tgg.stage.spawn_x(index)
		spawn.y = tgg.stage.spawn_y(index)
	end
	spawn.facing = if spawn.x > 0 then -1 else 1
end)
