-- Scripted user session on MAME's sprinter driver: ZXK_STEPS = "frame|action|arg;frame|action|arg;..."
-- actions: keys (natkeyboard:post_coded, e.g. "dir{ENTER}"), snap (PNG name), play (start the cassette),
--          stop (stop the cassette), load (snapshot image path), end, kbd (list keyboards), state (log PLD state)
if zxk_started then return end
zxk_started = true

local machine = manager.machine
local out = os.getenv("ZXK_OUT") or "."
local screen = machine.screens[":screen"]
local steps = {}
for item in string.gmatch(os.getenv("ZXK_STEPS") or "", "[^;]+") do
	local f, a, arg = string.match(item, "^(%d+)|([^|]+)|?(.*)$")
	steps[#steps + 1] = { frame = tonumber(f), action = a, arg = arg }
end
local every = tonumber(os.getenv("ZXK_SNAP_EVERY") or "0")

local items = machine.devices[":"].items
local function item(name)
	local idx = items["0/" .. name]
	if idx == nil then return nil end
	return emu.item(idx)
end
local it = {}
for _, n in ipairs({ "m_cnf", "m_pn", "m_sc", "m_dos", "m_turbo", "m_rom_rg", "m_ram_sys", "m_all_mode", "m_pg3" }) do
	it[n] = item(n)
end
local function st()
	local s = ""
	for n, v in pairs(it) do
		if v then s = s .. string.format(" %s=%02X", n, v:read(0)) end
	end
	local cpu = machine.devices[":maincpu"]
	s = s .. string.format(" PC=%04X SP=%04X", cpu.state["PC"].value, cpu.state["SP"].value)
	return s
end

local frame = 0
local idx = 1
zxk_sub = emu.add_machine_frame_notifier(function()
	frame = frame + 1
	if every > 0 and frame % every == 0 then
		screen:snapshot(string.format("%s/f%05d.png", out, frame))
	end
	while idx <= #steps and steps[idx].frame <= frame do
		local s = steps[idx]
		idx = idx + 1
		print(string.format("zxsteps: frame %d %s %s |%s", frame, s.action, s.arg, st()))
		if s.action == "keys" then
			machine.natkeyboard:post_coded(s.arg)
		elseif s.action == "snap" then
			screen:snapshot(out .. "/" .. s.arg .. ".png")
		elseif s.action == "play" then
			for tag, c in pairs(machine.cassettes) do c:play(); print("zxsteps: play " .. tag) end
		elseif s.action == "stop" then
			for tag, c in pairs(machine.cassettes) do c:stop() end
		elseif s.action == "load" then
			for tag, img in pairs(machine.images) do
				if string.find(tag, "snapshot") then
					local err = img:load(s.arg)
					print("zxsteps: load " .. tag .. " -> " .. tostring(err))
				end
			end
		elseif s.action == "kbd" then
			for tag, k in pairs(machine.natkeyboard.keyboards) do
				print("zxsteps: keyboard " .. tag .. " enabled=" .. tostring(k.enabled))
			end
		elseif s.action == "kbdonly" then
			for tag, k in pairs(machine.natkeyboard.keyboards) do
				if string.sub(s.arg,1,1) == "=" then k.enabled = (tag == string.sub(s.arg,2)) else k.enabled = (string.find(tag, s.arg, 1, true) ~= nil) end
				print("zxsteps: keyboard " .. tag .. " enabled=" .. tostring(k.enabled))
			end
		elseif s.action == "state" then
			-- logged above
		elseif s.action == "reset" then
			machine:soft_reset()
		elseif s.action == "end" then
			machine:exit()
		end
	end
end)
