-- Co-emulation autoboot script for MAME (see README.md). run.sh passes the settings in the environment:
--   COEMU_MACHINE  the coemu machine name (48k, 128k, plus2, plus2a, plus3, pentagon, scorpion, profscorp, atm710)
--   COEMU_MEDIA    tape or disk (what run.sh mounted)
--   COEMU_DONE     address of the program's DONE byte
--   COEMU_START, COEMU_END   the dump range, START .. END-1
--   COEMU_MAX_FRAMES         give up after this many frames
--   COEMU_BIN      where the dump goes; COEMU_WORK   prefix for the screen (.scr) and ROM (.rom) files
-- It types what a user would type, presses play on the tape, polls DONE every frame and exits.

local machine_name = os.getenv("COEMU_MACHINE")
local media = os.getenv("COEMU_MEDIA") or "tape"
local done_addr = tonumber(os.getenv("COEMU_DONE"))
local start_addr = tonumber(os.getenv("COEMU_START"))
local end_addr = tonumber(os.getenv("COEMU_END"))
local max_frames = tonumber(os.getenv("COEMU_MAX_FRAMES") or "60000")
local bin_path = os.getenv("COEMU_BIN")
local work = os.getenv("COEMU_WORK")

local function log(fmt, ...)
	print(string.format("coemu.lua: " .. fmt, ...))
end

-- What the user does once the machine has booted: a list of steps, the first one a number of frames after
-- power-on, each later one that many frames after the previous one is done. A step types text through MAME's
-- natural keyboard (keys), or holds keys down for 5 frames (press: the keys' names as MAME's keyboard lists
-- them; the natural keyboard has no cursor keys)
--   48k: LOAD "" (J is the LOAD keyword in K mode, SYMBOL SHIFT + P is the quote)
--   128k / plus2: ENTER on the boot menu's first entry, "Tape Loader"
--   plus2a / plus3: ENTER on the menu's first entry, "Loader" (tries the disk drive, then the tape)
--   from disk, into TR-DOS:
--     pentagon: the 128 menu's fifth entry, "TR-DOS" (four times cursor down, CAPS SHIFT + 6), then RUN at the
--       A> prompt (R is the RUN keyword in K mode)
--     scorpion, profscorp: the menu's first entry, "128 TR-DOS"; TR-DOS runs the disk's "boot" by itself
--     atm710: the ATM BIOS menu's second entry, "TR-DOS 48"; TR-DOS runs "boot" by itself
--   pentagon / scorpion / profscorp from tape: ENTER on the menu's first entry
local DOWN = { "CAPS SHIFT", "6 " }
local boot = {
	["48k"]      = { { frame = 150, keys = 'j""\r' } },
	["128k"]     = { { frame = 150, keys = "\r" } },
	["plus2"]    = { { frame = 150, keys = "\r" } },
	["plus2a"]   = { { frame = 200, keys = "\r" } },
	["plus3"]    = { { frame = 200, keys = "\r" } },
	["pentagon"] = { { frame = 200, keys = "\r" } },
	["scorpion"] = { { frame = 200, keys = "\r" } },
	["profscorp"] = { { frame = 200, keys = "\r" } },
}
local boot_disk = {
	["pentagon"] = {
		{ frame = 200, press = DOWN }, { frame = 10, press = DOWN }, { frame = 10, press = DOWN },
		{ frame = 10, press = DOWN }, { frame = 10, keys = "\r" }, { frame = 100, keys = "r\r" },
	},
	["scorpion"] = { { frame = 200, keys = "\r" } },
	["profscorp"] = { { frame = 200, keys = "\r" } },
	["atm710"]   = { { frame = 200, press = DOWN }, { frame = 10, keys = "\r" } },
}
local plan = boot[machine_name]
if media == "disk" then
	plan = boot_disk[machine_name]
end
if plan == nil then
	log("no key plan for machine %s, media %s", tostring(machine_name), media)
	manager.machine:exit()
	return
end

local space = manager.machine.devices[":maincpu"].spaces["program"]
local natkbd = manager.machine.natkeyboard
local cassette = nil
for _, c in pairs(manager.machine.cassettes) do
	cassette = c
	break
end

local frame = 0
local typed = false
local step_no = 1
local ready_at = 0       -- the frame the previous step was done
local held = nil         -- the key fields a press step holds down
local release_at = 0
local waiting = false    -- a keys step is being typed
local playing = false
local seen_zero = false
local finished = false

local function dump(path, from, to)
	local f = assert(io.open(path, "wb"))
	local t = {}
	for a = from, to - 1 do
		t[#t + 1] = string.char(space:read_u8(a))
	end
	f:write(table.concat(t))
	f:close()
end

local function dump_rom()
	-- The whole ROM region, so the screen text can be read with the machine's own font
	local region = manager.machine.memory.regions[":maincpu"]
	if region == nil then return end
	local f = io.open(work .. ".rom", "wb")
	if f == nil then return end
	local t = {}
	for a = 0, region.size - 1 do
		t[#t + 1] = string.char(region:read_u8(a))
	end
	f:write(table.concat(t))
	f:close()
end

-- The key whose name (as MAME's keyboard lists it) starts with prefix
local function key_field(prefix)
	for _, port in pairs(manager.machine.ioport.ports) do
		for name, f in pairs(port.fields) do
			if name:sub(1, #prefix) == prefix then return f end
		end
	end
	return nil
end

local function finish(reason)
	if finished then return end
	finished = true
	log("%s at frame %d", reason, frame)
	dump(work .. ".scr", 0x4000, 0x5B00)
	dump_rom()
	manager.machine:exit()
end

-- Keep the subscription referenced, or it is collected
coemu_frame_sub = emu.add_machine_frame_notifier(function()
	if finished then return end
	frame = frame + 1

	if not typed then
		if held ~= nil and frame >= release_at then
			for _, f in ipairs(held) do f:clear_value() end
			held = nil
			ready_at = frame
		end
		if waiting and natkbd.empty then
			waiting = false
			ready_at = frame
		end
		if held == nil and not waiting and step_no <= #plan and frame >= ready_at + plan[step_no].frame then
			local step = plan[step_no]
			if step.press ~= nil then
				log("frame %d: pressing %s", frame, table.concat(step.press, " + "))
				held = {}
				for _, name in ipairs(step.press) do
					local f = key_field(name)
					if f == nil then
						log("no key %q on this machine", name)
					else
						f:set_value(1)
						held[#held + 1] = f
					end
				end
				release_at = frame + 5
			else
				log("frame %d: typing %q", frame, step.keys)
				natkbd:post(step.keys)
				waiting = true
			end
			step_no = step_no + 1
		end
		if step_no > #plan and held == nil and not waiting then
			typed = true
		end
	end
	if typed and not playing and natkbd.empty then
		playing = true
		if media == "tape" and cassette ~= nil then
			log("frame %d: tape play", frame)
			cassette:play()
		end
	end

	if playing then
		local v = space:read_u8(done_addr)
		if v == 0 then
			seen_zero = true
		elseif v == 1 and seen_zero then
			dump(bin_path, start_addr, end_addr)
			finish("DONE = 1")
			return
		elseif v ~= 1 and seen_zero then
			-- The program only ever writes 0 and 1 there: the memory was cleared, the machine was reset
			finish(string.format("reset: DONE = %d, the program's memory was overwritten", v))
			return
		end
	end

	if frame >= max_frames then
		finish(string.format("timeout: DONE not 1 after %d frames", max_frames))
	end
end)

log("machine %s, media %s, DONE at %d, dump %d..%d", machine_name, media, done_addr, start_addr, end_addr - 1)
