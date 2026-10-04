-- Scripted user session on MAME's sprinter driver: ZXK_STEPS = "frame|action|arg;frame|action|arg;..."
-- actions: keys (natkeyboard:post_coded, e.g. "dir{ENTER}"), snap (PNG name), play (start the cassette),
--          stop (stop the cassette), load (snapshot image path), end, kbd (list keyboards), state (log PLD state),
--          reset / hardreset, dbg (a debugger command, e.g. bpset with a printf action; needs SPC_DEBUG=1),
--          vram (the video RAM to <name>.bin: the mode table that places the INT),
--          fields (input fields held for 4 frames: "<port tag>/<field name>+..."),
--          listfields (print the field names of the ports whose tag holds the argument, e.g. "ms_naturl")
if zxk_started then return end
zxk_started = true

local machine = manager.machine
local out = os.getenv("ZXK_OUT") or "."
local screen = machine.screens[":screen"]
local steps = {}
-- ZXK_SEP: the step separator (default ";"; set another, e.g. "~", when a dbg command holds ";")
local sep = os.getenv("ZXK_SEP") or ";"
for item in string.gmatch(os.getenv("ZXK_STEPS") or "", "[^" .. sep .. "]+") do
	local f, a, arg = string.match(item, "^(%d+)|([^|]+)|?(.*)$")
	if f then
		steps[#steps + 1] = { frame = tonumber(f), action = a, arg = arg }
	else
		print("zxsteps: bad step '" .. item .. "'")
	end
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
local held = {}  -- input fields pressed by "fields", released 4 frames later
zxk_sub = emu.add_machine_frame_notifier(function()
	frame = frame + 1
	for i = #held, 1, -1 do
		if held[i].until_frame <= frame then
			held[i].field:clear_value()
			table.remove(held, i)
		end
	end
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
		elseif s.action == "hardreset" then
			machine:hard_reset()
		elseif s.action == "dbg" then
			-- a debugger console command (needs SPC_DEBUG=1: -debug -debugger none -debuglog; output in debug.log)
			if machine.debugger then
				machine.debugger:command(s.arg)
				machine.debugger:command("g")
			else
				print("zxsteps: dbg needs SPC_DEBUG=1")
			end
		elseif s.action == "fields" then
			-- input fields held together for 4 frames: "<port tag>/<field name>+..." (a chord the natural keyboard
			-- cannot type, e.g. the Spectrum's cursor down: ":IO_LINE4/6   Down       &    YELLOW   MOVE+:IO_LINE4/CS Line4")
			for spec in string.gmatch(s.arg, "[^+]+") do
				local tag, name = string.match(spec, "^([^/]+)/(.+)$")
				local port = tag and machine.ioport.ports[tag]
				local field = port and port.fields[name]
				if field then
					field:set_value(1)
					held[#held + 1] = { field = field, until_frame = frame + 4 }
				else
					print("zxsteps: no input field '" .. spec .. "'")
				end
			end
		elseif s.action == "listfields" then
			-- the input fields of every port whose tag holds the argument (names for "fields")
			for tag, port in pairs(machine.ioport.ports) do
				if string.find(tag, s.arg, 1, true) then
					for name, _ in pairs(port.fields) do
						print("zxsteps: field " .. tag .. "/" .. name)
					end
				end
			end
		elseif s.action == "vram" then
			-- the 256 KB video RAM (mode tables, palettes, pictures) to <out>/<arg>.bin, with the PLD's RGMOD
			local share = machine.memory.shares[":vram"]
			local f = io.open(out .. "/" .. s.arg .. ".bin", "wb")
			if share and f then
				local chunk = {}
				for i = 0, share.size - 1 do
					chunk[#chunk + 1] = string.char(share:read_u8(i))
					if #chunk == 4096 then f:write(table.concat(chunk)); chunk = {} end
				end
				f:write(table.concat(chunk))
				f:close()
				local rg = item("m_rgmod")
				print(string.format("zxsteps: vram %s.bin %d bytes rgmod=%s", s.arg, share.size, rg and string.format("%02X", rg:read(0)) or "?"))
			else
				print("zxsteps: vram share not found")
			end
		elseif s.action == "end" then
			machine:exit()
		end
	end
end)
