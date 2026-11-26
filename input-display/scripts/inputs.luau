--!strict
-- Shared state layout between sim.luau (records each fighter's inputs) and view.luau (draws them).
local inputs = {}

export type PortInput = {
	active: boolean,
	cpu: boolean,
	controller: number,
	sx: number,
	sy: number,
	cx: number,
	cy: number,
	buttons: number,
	trigger: number,
}

export type State = {
	tick: number,
	ports: { PortInput },
}

function inputs.empty_port(): PortInput
	return {
		active = false,
		cpu = false,
		controller = -1,
		sx = 0,
		sy = 0,
		cx = 0,
		cy = 0,
		buttons = 0,
		trigger = 0,
	}
end

function inputs.defaults(): State
	return {
		tick = 0,
		ports = {
			inputs.empty_port(),
			inputs.empty_port(),
			inputs.empty_port(),
			inputs.empty_port(),
		},
	}
end

return inputs
