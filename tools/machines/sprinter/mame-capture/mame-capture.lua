-- Sprinter Sp2000 reference captures on MAME's `sprinter` driver
-- (see testdata/machines/sprinter/reference/README.md). Run by mame-capture.sh through -autoboot_script; the settings come from the environment:
--   SPC_MODE    boot    port trace from power-on, snapshots, page #40 dump (default)
--               loader  counts the CPU memory writes of the PLD configuration loader (runtime Q4 check)
--               sync    calls BIOS function #F2 (FN_SYNC) for each INT mode and records the INT positions
--               palette per-frame sum of the palette bytes in video RAM (the logo fade, no renderer needed)
--   SPC_OUT     output folder (must exist)
--   SPC_END     frame to exit at (default 600)
--   boot:   SPC_PORTS (accesses to record, default 10000), SPC_SNAP_EVERY (0 = off), SPC_SNAP_AT,
--           SPC_DUMP_AT (frame of the page #40 dump and the final snapshot),
--           SPC_CODES ("lo-hi" in hex: record only port-table accesses with a code in that range, e.g.
--           "20-29" for the IDE), SPC_PORTS_FILE (output name, default ports.csv),
--           SPC_WAIT_TEXT (text to look for in the text-mode screen every frame: the first frame that shows it
--           is logged as an event and saved as text-found.png, e.g. "Estex DSS" for the DSS banner)
--   sync:   SPC_SYNC_AT (frame of the first FN_SYNC call), SPC_SYNC_FRAMES (frames measured per mode)
--   loader: SPC_ROM (the 256 KB BIOS image, to check the reassembled bitstream)
--
-- Clocks: MAME's maincpu runs at 42 MHz / 12 = 3.5 MHz, with a clock scale of 6 in turbo. "t35" columns
-- are emulated time x 3 500 000 (T-states of a 3.5 MHz clock), whatever the turbo state.

-- MAME runs the autoboot script again after every soft reset, and the driver soft-resets itself when its
-- configuration shortcut ends (sprinter.cpp bootstrap_w). Only the first run (power-on, t = 0) captures.
if spc_started then
	return
end
spc_started = true

local mode = os.getenv("SPC_MODE") or "boot"
local out = os.getenv("SPC_OUT") or "."
local end_frame = tonumber(os.getenv("SPC_END") or "600")

local machine = manager.machine
local cpu = machine.devices[":maincpu"]
local prg = cpu.spaces["program"]
local iosp = cpu.spaces["io"]
local ops = cpu.spaces["opcodes"]
local screen = machine.screens[":screen"]

local function log(fmt, ...)
	print(string.format("mame-capture.lua: " .. fmt, ...))
end

-- Save-state items of the driver (MAME's names, "0/<member>")
local items = machine.devices[":"].items
local function item(name)
	local idx = items["0/" .. name]
	if idx == nil then
		error("no save item " .. name)
	end
	return emu.item(idx)
end
local it_conf_loading = item("m_conf_loading")
local it_starting = item("m_starting")
local it_bitcount = item("m_bitstream_count")
local it_bithash = item("m_bitstream_hash")
local it_cnf = item("m_cnf")
local it_pn = item("m_pn")
local it_dos = item("m_dos")
local it_rgmod = item("m_rgmod")
local it_turbo = item("m_turbo")
local ram = emu.item(machine.devices[":ram"].items["0/m_pointer"])
local vram = machine.memory.shares[":vram"]

local DCP_BASE = 0x40 * 0x4000

local function t35()
	return emu.time() * 3500000
end

local frame = 0
local finished = false
-- Global, so the taps and notifiers are never garbage-collected (a collected tap crashes MAME)
spc_subs = {}
local subs = spc_subs

local function finish(reason)
	if finished then return end
	finished = true
	log("%s at frame %d (%.6f s)", reason, frame, emu.time())
	machine:exit()
end

local function write_file(name, data)
	local f = assert(io.open(out .. "/" .. name, "wb"))
	f:write(data)
	f:close()
end

local function snapshot(name)
	screen:snapshot(out .. "/" .. name)
end

-- The port-table index MAME builds for an access (sprinter.cpp dcp_r / dcp_w)
local function dcp_index(port, is_read)
	local cnf = it_cnf:read(0)
	local pn = it_pn:read(0)
	local dos = it_dos:read(0)
	return ((cnf >> 3) & 3) << 12 | ((pn >> 5) & 1) << 11 | (dos & 1) << 10 | (is_read and 1 or 0) << 9
		| ((port >> 14) & 3) << 7 | ((port >> 13) & 1) << 4 | ((port >> 7) & 1) << 3 | (port & 0x67)
end

-- Z84C15 on-chip ports (8-bit decoded; MAME sprinter.cpp init_taps)
local Z84 = {}
for _, p in ipairs({ 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0xee, 0xef,
	0xf0, 0xf1, 0xf4 }) do
	Z84[p] = true
end

local events = {}
local function event(fmt, ...)
	local s = string.format("frame=%d time_s=%.9f t35=%.0f ", frame, emu.time(), t35()) .. string.format(fmt, ...)
	events[#events + 1] = s
	log("%s", s)
end

---------------------------------------------------------------------------------------------------------------
if mode == "boot" then
	local max_ports = tonumber(os.getenv("SPC_PORTS") or "10000")
	local snap_every = tonumber(os.getenv("SPC_SNAP_EVERY") or "0")
	-- SPC_SNAP_AT: "<frame>:<name>,..." snapshots saved as <name>.png
	local snap_at = {}
	for f, name in string.gmatch(os.getenv("SPC_SNAP_AT") or "", "(%d+):([%w%-_]+)") do
		snap_at[tonumber(f)] = name
	end
	local dump_at = tonumber(os.getenv("SPC_DUMP_AT") or "-1")
	local ports_file = os.getenv("SPC_PORTS_FILE") or "ports.csv"
	local code_lo, code_hi = string.match(os.getenv("SPC_CODES") or "", "^(%x+)-(%x+)$")
	code_lo = code_lo and tonumber(code_lo, 16)
	code_hi = code_hi and tonumber(code_hi, 16)
	local rows = { "n,frame,time_us,dir,port,value,pc,phase,index,code" }
	local nports = 0
	local opened = false
	local was_loading = true

	local wait_text = os.getenv("SPC_WAIT_TEXT")
	-- The text-mode screen as unreal-ng's tests read it: a text square's Mode1 byte is the character code,
	-- 40 squares x 2 characters per row, 32 rows, both mode pages (tdd-video §3)
	local function screen_text()
		local chars = {}
		for page = 0, 1 do
			for b = 0, 31 do
				for a = 0, 39 do
					for half = 0, 1 do
						local c = vram:read_u8((1 + 2 * a + half + 0x80 * page) * 1024 + 0x301 + 4 * b)
						chars[#chars + 1] = (c >= 0x20 and c < 0x7f) and string.char(c) or " "
					end
				end
				chars[#chars + 1] = "\n"
			end
		end
		return table.concat(chars)
	end

	local function record(dir, port, data)
		if nports >= max_ports then return end
		local loading = it_conf_loading:read(0) ~= 0
		local phase, index, code = "loader", "", ""
		if loading then
			phase = "loader"
		elseif Z84[port & 0xff] then
			phase = opened and "open" or "closed"
			code = "z84"
		else
			phase = opened and "open" or "closed"
			local idx = dcp_index(port, dir == "R")
			index = string.format("%04X", idx)
			local c = ram:read(DCP_BASE + idx)
			if code_lo ~= nil and (c < code_lo or c > code_hi) then return end
			code = string.format("%02X", c)
		end
		if code_lo ~= nil and code == "" then return end
		local pc = cpu.state["CURPC"].value
		nports = nports + 1
		rows[#rows + 1] = string.format("%d,%d,%.3f,%s,%04X,%02X,%04X,%s,%s,%s", nports, frame, emu.time() * 1e6,
			dir, port, data, pc, phase, index, code)
		if nports == max_ports then
			event("port trace complete (%d accesses)", nports)
			write_file(ports_file, table.concat(rows, "\n") .. "\n")
		end
	end

	subs.io_r = iosp:install_read_tap(0x0000, 0xffff, "spc_io_r", function(offset, data, mask)
		-- dcp_r has already run: the first read outside the loader opens the decoder
		record("R", offset, data)
		if not opened and it_conf_loading:read(0) == 0 and not Z84[offset & 0xff] then
			opened = true
			event("first port read: DCP open (port %04X, PC %04X); page #40 saved as page40-open.bin", offset,
				cpu.state["CURPC"].value)
			write_file("page40-open.bin", ram:read_block(DCP_BASE, 0x4000))
		end
	end)
	subs.io_w = iosp:install_write_tap(0x0000, 0xffff, "spc_io_w", function(offset, data, mask)
		record("W", offset, data)
	end)

	subs.frame = emu.add_machine_frame_notifier(function()
		if finished then return end
		frame = frame + 1
		local loading = it_conf_loading:read(0) ~= 0
		if was_loading and not loading then
			event("MAME configuration shortcut done (m_conf_loading 1 -> 0)")
		end
		was_loading = loading
		if snap_every > 0 and frame % snap_every == 0 then
			snapshot(string.format("snap-%05d.png", frame))
		end
		if wait_text ~= nil then
			local text = screen_text()
			if string.find(text, wait_text, 1, true) then
				event("text '%s' on screen at frame %d (%.3f s)", wait_text, frame, emu.time())
				write_file("text-found.txt", text)
				snapshot("text-found.png")
				wait_text = nil
			end
		end
		if snap_at[frame] ~= nil then
			snapshot(snap_at[frame] .. ".png")
			event("snapshot %s.png", snap_at[frame])
		end
		if frame == dump_at then
			write_file("page40.bin", ram:read_block(DCP_BASE, 0x4000))
			event("page #40 dumped (cnf=%02X pn=%02X dos=%d starting=%d)", it_cnf:read(0), it_pn:read(0),
				it_dos:read(0), it_starting:read(0))
		end
		if frame >= end_frame then
			if nports < max_ports then
				write_file(ports_file, table.concat(rows, "\n") .. "\n")
				event("port trace ended early (%d accesses)", nports)
			end
			write_file("events.txt", table.concat(events, "\n") .. "\n")
			finish("end")
		end
	end)

---------------------------------------------------------------------------------------------------------------
elseif mode == "loader" then
	-- MAME ends the load after 4 096 writes (bootstrap_w). Holding m_bitstream_count at 0 lets the ROM loader
	-- run to the end of the real bitstream; once the source pointer HL has passed the last byte by a margin,
	-- the count is released and MAME's own shortcut ends the load 4 096 writes later.
	local rom_path = os.getenv("SPC_ROM")
	local STREAM_FIRST, STREAM_LAST = 0x0100, 0xE84E
	local total, pre, stream, post = 0, 0, 0, 0
	local first_hl, first_de, first_pc, first_t = nil, nil, nil, nil
	local end_t = nil
	local released = false
	local bits = {}       -- the D0 of each stream write
	local nonzero_upper = 0
	local addr_lo, addr_hi = 0xffff, 0
	local outside_fe = 0

	-- The Z84C15's chip selects put the CPU's 16-bit address at #2xxxx while the loader runs
	subs.w = prg:install_write_tap(0x00000, 0x3ffff, "spc_loader_w", function(offset, data, mask)
		if it_conf_loading:read(0) == 0 then return end
		offset = offset & 0xffff
		total = total + 1
		local hl = cpu.state["HL"].value
		if first_hl == nil then
			first_hl, first_de, first_pc, first_t = hl, cpu.state["DE"].value, cpu.state["CURPC"].value, t35()
		end
		if offset < addr_lo then addr_lo = offset end
		if offset > addr_hi then addr_hi = offset end
		if (offset & 0xff00) ~= 0xfe00 then outside_fe = outside_fe + 1 end
		if hl < STREAM_FIRST then
			pre = pre + 1
		elseif hl <= STREAM_LAST then
			stream = stream + 1
			bits[#bits + 1] = data & 1
			if stream == (STREAM_LAST - STREAM_FIRST + 1) * 8 then
				end_t = t35()
				event("last bitstream write: write %d, stream write %d, HL=%04X DE=%04X", total, stream, hl,
					cpu.state["DE"].value)
			end
		else
			post = post + 1
		end
		if not released then
			it_bitcount:write(0, 0)
			it_bithash:write(0, 0)
			if hl > STREAM_LAST + 0x100 then
				released = true
				event("released MAME's write counter at write %d (HL=%04X)", total, hl)
			end
		end
	end)

	subs.frame = emu.add_machine_frame_notifier(function()
		if finished then return end
		frame = frame + 1
		if released and it_conf_loading:read(0) == 0 then
			-- Rebuild the bytes from the D0 of each write, bit 0 first, and compare with the ROM
			local bytes = {}
			for i = 1, #bits, 8 do
				local b = 0
				for k = 0, 7 do
					b = b | ((bits[i + k] or 0) << k)
				end
				bytes[#bytes + 1] = string.char(b)
			end
			local rebuilt = table.concat(bytes)
			write_file("loader-bitstream.bin", rebuilt)
			local match = "not checked (SPC_ROM not set)"
			if rom_path ~= nil then
				local f = assert(io.open(rom_path, "rb"))
				local rom = f:read("a")
				f:close()
				-- the loader runs with ROM pages #C-#F at #0000: ROM offset #30000 + address
				local want = rom:sub(0x30000 + STREAM_FIRST + 1, 0x30000 + STREAM_LAST + 1)
				if want == rebuilt then
					match = "identical"
				else
					local i = 1
					while i <= #want and want:byte(i) == rebuilt:byte(i) do i = i + 1 end
					match = string.format("identical up to #%04X, first difference at #%04X (MAME maps ROM page #C only"
						.. " at #0000-#3FFF while loading; real hardware has pages #C-#F)", STREAM_FIRST + i - 2,
						STREAM_FIRST + i - 1)
				end
			end
			local lines = {
				string.format("writes_total=%d (to the end of MAME's shortcut: 4 096 more after the release)", total),
				string.format("writes_before_stream=%d", pre),
				string.format("writes_stream=%d (HL in #%04X-#%04X)", stream, STREAM_FIRST, STREAM_LAST),
				string.format("writes_after_stream=%d (source past #E84E, until MAME ends the load)", post),
				string.format("expected_stream=%d (59 215 x 8)", (STREAM_LAST - STREAM_FIRST + 1) * 8),
				string.format("first_write: HL=%04X DE=%04X PC=%04X t35=%.0f", first_hl or 0, first_de or 0,
					first_pc or 0, first_t or 0),
				string.format("last_stream_write_t35=%.0f (%.6f s)", end_t or 0, (end_t or 0) / 3500000),
				string.format("write_address_range=%04X-%04X, writes_outside_FExx=%d", addr_lo, addr_hi, outside_fe),
				string.format("rebuilt_bitstream_vs_rom=%s", match),
			}
			for _, e in ipairs(events) do lines[#lines + 1] = e end
			write_file("loader.txt", table.concat(lines, "\n") .. "\n")
			finish("loader done")
			return
		end
		if frame >= end_frame then
			write_file("loader.txt", string.format("timeout: total=%d stream=%d\n", total, stream))
			finish("timeout")
		end
	end)

---------------------------------------------------------------------------------------------------------------
elseif mode == "sync" then
	local sync_at = tonumber(os.getenv("SPC_SYNC_AT") or "520")
	local per_mode = tonumber(os.getenv("SPC_SYNC_FRAMES") or "5")
	-- FN_SYNC (#F2), BIOS 3.04 (page 8 #0B64): A = 0 from the system variable, 1 Scorpion, 2 Pentagon, 3 Spectrum
	local plan = { { name = "booted" }, { name = "scorpion", a = 1 }, { name = "pentagon", a = 2 },
		{ name = "spectrum", a = 3 }, { name = "default", a = 0 } }
	local step = 0
	local state = "idle"      -- idle, calling, measuring
	local measure_from = 0
	local rows = { "mode,source,frame,line,x,t35_in_frame" }
	local acks = {}
	local STUB_RET = nil

	-- MAME's INT list (sprinter.cpp update_int): the 8th line of the square that follows a run of
	-- "blank + INT" squares (mode byte & #FD == #FD), read from the mode table in video RAM
	local function int_model()
		local list = {}
		local height = math.floor(screen.frame_period / screen.scan_period + 0.5) // 8
		local rg = it_rgmod:read(0) & 1
		for scr_b = 0, height - 1 do
			local pre_int = false
			local b = (scr_b + height - 2) % height
			for scr_a = 0, 55 do
				local a = (scr_a + 56 - 6) % 56
				local m = vram:read_u8((1 + a * 2 + 0x80 * rg) * 1024 + 0x300 + b * 4)
				if (m & 0xfd) == 0xfd then
					pre_int = true
				else
					if pre_int then
						list[#list + 1] = { scr_b * 8 + 7, scr_a * 16 }
					end
					pre_int = false
				end
			end
		end
		return list, height * 8
	end

	-- Position in MAME's frame (vpos 0, hpos 0 = 0) in 3.5 MHz T-states (896 pixels of 14 MHz = 224 T)
	local function frame_pos_t35()
		local pos = screen.frame_period - screen:time_until_pos(0, 0)
		if pos >= screen.frame_period - 1e-12 then pos = 0 end
		return pos * 3500000
	end

	-- The 16-bit address appears at #0xxxx-#3xxxx of the opcode space (Z84C15 chip selects)
	local function fetch_tap(key, addr, fn)
		subs[key] = subs[key] or {}
		for k = 0, 3 do
			if subs[key][k] ~= nil then subs[key][k]:remove() end
			local a = (k << 16) | addr
			subs[key][k] = ops:install_read_tap(a, a, "spc_" .. key .. k, fn)
		end
	end

	-- INT acknowledge: the first fetch of the interrupt routine
	local function arm_ack()
		local im = cpu.state["IM"].value
		local target = 0x0038
		if im == 2 then
			target = prg:read_u16((cpu.state["I"].value << 8) | 0xff)
		end
		fetch_tap("ack", target, function(offset, data, mask)
			if state == "measuring" then
				local t = frame_pos_t35()
				acks[#acks + 1] = string.format("%s,ack,%d,%d,%d,%.1f", plan[step].name, frame, math.floor(t / 224),
					math.floor((t % 224) * 4), t)
			end
		end)
		return im, target
	end

	local function record_model(name)
		local list, lines = int_model()
		for _, p in ipairs(list) do
			rows[#rows + 1] = string.format("%s,model,%d,%d,%d,%d", name, frame, p[1], p[2], p[1] * 224 + p[2] // 4)
		end
		event("%s: %d INT position(s) in a %d-line frame, frame period %.9f s, rgmod=%02X", name, #list, lines,
			screen.frame_period, it_rgmod:read(0))
		snapshot("sync-" .. name .. ".png")
	end

	-- A stub below the stack (in window 1 or 2, never window 3, which FN_SYNC pages) that calls FN_SYNC and
	-- returns to the interrupted code with every register kept:
	--   PUSH AF/BC/DE/HL/IX/IY, LD A,n, LD C,#F2, RST #18, POP IY/IX/HL/DE/BC/AF, RET
	-- RST #18 is the BIOS call from system mode (ROM page 8 in window 0); RST #08 is the call from RAM (DSS),
	-- which first switches the ROM out (OUT (#3C)), so it does not work at the boot screen
	local function call_sync(a)
		local sp = cpu.state["SP"].value
		local pc = cpu.state["PC"].value
		local stub = (sp - 0x400) & 0xffff
		local code = { 0xF5, 0xC5, 0xD5, 0xE5, 0xDD, 0xE5, 0xFD, 0xE5, 0x3E, a, 0x0E, 0xF2, 0xDF,
			0xFD, 0xE1, 0xDD, 0xE1, 0xE1, 0xD1, 0xC1, 0xF1, 0xC9 }
		for i, b in ipairs(code) do
			prg:write_u8(stub + i - 1, b)
		end
		STUB_RET = stub + 13
		fetch_tap("ret", STUB_RET, function(offset, data, mask)
			if state == "calling" then
				state = "returned"
				event("FN_SYNC returned, carry=%d", cpu.state["F"].value & 1)
			end
		end)
		sp = (sp - 2) & 0xffff
		prg:write_u16(sp, pc)
		cpu.state["SP"].value = sp
		cpu.state["PC"].value = stub
		event("FN_SYNC A=%d: stub at %04X, return to %04X, SP=%04X, IM=%d I=%02X", a, stub, pc, sp,
			cpu.state["IM"].value, cpu.state["I"].value)
	end

	local function finish_sync(reason)
		write_file("int.csv", table.concat(rows, "\n") .. "\n" .. table.concat(acks, "\n") .. "\n")
		write_file("events-sync.txt", table.concat(events, "\n") .. "\n")
		finish(reason)
	end

	subs.frame = emu.add_machine_frame_notifier(function()
		if finished then return end
		frame = frame + 1
		if frame >= end_frame then
			finish_sync("timeout")
			return
		end
		if frame == sync_at then
			step = 1
			local im, target = arm_ack()
			event("IM=%d, ISR entry %04X, turbo=%d", im, target, it_turbo:read(0))
			record_model(plan[step].name)
			state = "measuring"
			measure_from = frame
		elseif state == "measuring" and frame >= measure_from + per_mode then
			step = step + 1
			if step > #plan then
				finish_sync("sync done")
				return
			end
			state = "calling"
			call_sync(plan[step].a)
		elseif state == "returned" then
			-- start measuring on the next whole frame
			arm_ack()
			record_model(plan[step].name)
			state = "measuring"
			measure_from = frame
		end
	end)
elseif mode == "palette" then
	-- The palette lives in the last 32 bytes of every 1 KB video RAM line (sprinter.cpp vram_w: laddr >= #3E0).
	-- One row per frame whose sum changed: the BIOS logo's palette set-up and fade, comparable without a renderer
	local rows = { "frame,palette_sum" }
	local prev = -1
	subs.frame = emu.add_machine_frame_notifier(function()
		if finished then return end
		frame = frame + 1
		local sum = 0
		for line = 0, 255 do
			for o = 0x3e0, 0x3ff do
				sum = sum + vram:read_u8(line * 1024 + o)
			end
		end
		if sum ~= prev then
			rows[#rows + 1] = string.format("%d,%d", frame, sum)
		end
		prev = sum
		if frame >= end_frame then
			write_file("palette.csv", table.concat(rows, "\n") .. "\n")
			finish("palette done")
		end
	end)
else
	log("unknown SPC_MODE %s", mode)
	machine:exit()
end
