--!strict
-- Shared types and formatting between wavedash-trainer's sim.luau and view.luau.
local tgg = require("@tgg")

local wavedashes = {}

-- Result kinds posted from sim.luau to view.luau
wavedashes.LAND = 1 -- completed wavedash into LANDING_FALL_SPECIAL
wavedashes.EARLY = 2 -- pressed L/R during jumpsquat and jumped without airdodging
wavedashes.AIR = 3 -- airdodged after jump but never hit the ground

-- Attempt phases tracked per port in sim.luau state
wavedashes.NONE = 0
wavedashes.SQUAT = 1
wavedashes.AIRBORNE = 2
wavedashes.DODGING = 3
wavedashes.SLIDING = 4

-- Quality tiers for color and 20XX tint flash
wavedashes.GRADE_PERFECT = 1
wavedashes.GRADE_GOOD = 2
wavedashes.GRADE_WARN = 3
wavedashes.GRADE_BAD = 4

export type Attempt = {
	phase: number,
	start_x: number,
	squat_end: number,
	early_tick: number,
	dodge_frames: number,
	angle: number,
	has_angle: boolean,
	airborne_ticks: number,
}

export type Result = {
	port: number,
	kind: number,
	frames: number, -- frames after jumpsquat (LAND/AIR), or frames early during jumpsquat (EARLY)
	angle: number, -- degrees below horizontal (17-22 ideal), negative if above horizontal
	has_angle: boolean,
	distance: number, -- horizontal slide distance in stage units (rounded to 0.1)
}

function wavedashes.attempt(): Attempt
	return {
		phase = wavedashes.NONE,
		start_x = 0,
		squat_end = 0,
		early_tick = 0,
		dodge_frames = 0,
		angle = 0,
		has_angle = false,
		airborne_ticks = 0,
	}
end

function wavedashes.result(): Result
	return {
		port = 0,
		kind = 0,
		frames = 0,
		angle = 0,
		has_angle = false,
		distance = 0,
	}
end

function wavedashes.defaults()
	return {
		tick = 0,
		at = {
			wavedashes.attempt(),
			wavedashes.attempt(),
			wavedashes.attempt(),
			wavedashes.attempt(),
		},
	}
end

function wavedashes.showing(): boolean
	return tgg.in_match() and (tgg.settings.where == "Every match" or tgg.mode.current() == tgg.mode.TRAINING)
end

function wavedashes.grade(r: Result): number
	if r.kind == wavedashes.LAND then
		if r.frames <= 1 then
			return wavedashes.GRADE_PERFECT
		elseif r.frames == 2 then
			return wavedashes.GRADE_GOOD
		elseif r.frames == 3 then
			return wavedashes.GRADE_WARN
		end
		return wavedashes.GRADE_BAD
	end
	return wavedashes.GRADE_BAD
end

function wavedashes.headline(r: Result): string
	if r.kind == wavedashes.LAND then
		if r.frames <= 1 then
			return "Perfect 1f"
		elseif r.frames == 2 then
			return "2f"
		elseif r.frames == 3 then
			return "3f"
		end
		return `{r.frames}f  Late`
	elseif r.kind == wavedashes.EARLY then
		return `Early {r.frames}f`
	end
	if r.frames <= 2 and r.has_angle and r.angle < 5 then
		return "Angle too high"
	end
	return `Too late ({r.frames}f)`
end

function wavedashes.detail(r: Result): string
	if r.kind == wavedashes.LAND then
		local ang = if r.has_angle then `{r.angle} deg` else "Neutral"
		local dist = string.format("%.1fu", r.distance)
		return `{ang}   {dist}`
	elseif r.kind == wavedashes.EARLY then
		return "Pressed L/R in jumpsquat"
	end
	if r.has_angle then
		return `{r.angle} deg   Aim 17-22 deg`
	end
	return "Aim down-forward (17-22 deg)"
end

function wavedashes.chip(r: Result): string
	if r.kind == wavedashes.LAND then
		return `{r.frames}f`
	elseif r.kind == wavedashes.EARLY then
		return `E{r.frames}`
	end
	return "Air"
end

return wavedashes
