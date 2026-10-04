    // ISA slots (the Sprinter Sp2000's two ISA-8 slots; Sprinter ISA tdd §10) - DeviceState::Isa for the report
    // and IsaAccess for cycles: the same calls CLI `isa` / `state isa`, Lua/Python isa_state() / isa_io_read() /
    // isa_io_write() / isa_mem_read() / isa_mem_write() / isa_reset() and the MCP aspect `isa` use. Other
    // machines answer 404 ("no ISA slots on this machine").
    {
        const char* kReport = "/api/v1/emulator/{id}/state/isa";
        paths[kReport]["get"]["summary"] = "Get ISA slot state";
        paths[kReport]["get"]["tags"].append("ISA Slots");
        paths[kReport]["get"]["description"] =
            "The Sprinter's ISA-8 slots: latch (the #9FBD byte: value, a19_a14, address_base, aen, reset), window "
            "(whether memory window 3 shows a slot now: page, slot, space io / memory), and slots[] with slot (1 = J6, "
            "page_io #D4, page_mem #D0; 2 = J7, #D6 / #D2), configured (the [ISA] SlotN kind), card (fitted: none, "
            "ne2000, ...), not_fitted (why a configured card is missing), the card's own fields (an NE2000: chip, "
            "base, mac, registers, counters) and counters (io_reads, io_writes, mem_reads, mem_writes, reset_pulses, "
            "irq_rises, irq_falls, irq_pio_requests, irq_acknowledged, irq_service_ends). "
            "Interrupts (ISA phase I4): each slot's irq_line object - card_irq (the IRQ the card selects), pio_bit (PB0 slot "
            "1, PB1 slot 2: every IRQ pin of a slot is one net with a 3.9 kOhm pull-up, wired to the Z84C15 PIO port "
            "B), driven (who drives the line; an undriven line reads high), line (high / low; ISA IRQs are active "
            "high), card_request, cause (the card's words: an NE2000's ISR & IMR and IRQEN, a 16550's IIR / IER), pio "
            "(input, monitored, active_level, logic, interrupt_enabled, vector, condition, pending, under_service) and "
            "reaches_cpu (yes with the IM 2 vector and the daisy-chain order, or why not). pio_port_b: the PIO port's "
            "mode, lines, inputs_latched, read, output, direction, mask, interrupt_control, vector, pending, "
            "under_service; irq_summary: the lines in one sentence. "
            "The ZX-bus adapter (card zxbus, ISA phase I2; slot 1 by default) adds zx_bus: adapter (I/O cycles become "
            "Spectrum IN / OUT at port A15-A0; no memory cycles, IRQ or WAIT), reset (ISA RESET DRV resets the card), "
            "reset_held, reset_pulses, memory_cycles, cards[] (the General Sound / NeoGS behind it: card gs, personality "
            "z80 / lw / ngs, device, firmware, ports #B3 / #BB / #33, cpu_addresses #C0B3 / #C0BB / #C033 with window 3 = "
            "#D4, status, data_flag, command_flag, data_to_host, ready_for_commands, sound, zx_dma (NeoGS: unavailable), "
            "machine_reset (GSReset), details: the GS's own report at /state/audio/gs) or empty (why nothing is behind "
            "it). Every slot carries summary_line: its piece of summary. "
            "The ISA address of a window access is (#9FBD bits 5-0) << 14 | CPU A13-A0.";
        paths[kReport]["get"]["parameters"][0]["name"] = "id";
        paths[kReport]["get"]["parameters"][0]["in"] = "path";
        paths[kReport]["get"]["parameters"][0]["required"] = true;
        paths[kReport]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kReport]["get"]["responses"]["200"]["description"] = "ISA slot state";
        paths[kReport]["get"]["responses"]["404"]["description"] = "No ISA slots on this machine (the reason is in message)";

        const char* kActive = "/api/v1/emulator/state/isa";
        paths[kActive]["get"]["summary"] = "Get ISA slot state (active emulator)";
        paths[kActive]["get"]["tags"].append("ISA Slots");
        paths[kActive]["get"]["description"] = "Same as /api/v1/emulator/{id}/state/isa for the only or selected emulator.";
        paths[kActive]["get"]["responses"]["200"]["description"] = "ISA slot state";
        paths[kActive]["get"]["responses"]["400"]["description"] = "Several emulators running: use the {id} form";
        paths[kActive]["get"]["responses"]["404"]["description"] = "No emulator, or no ISA slots";

        const char* kControl = "/api/v1/emulator/{id}/control/isa";
        paths[kControl]["post"]["summary"] = "Run an ISA cycle";
        paths[kControl]["post"]["tags"].append("ISA Slots");
        paths[kControl]["post"]["description"] =
            "One ISA cycle from outside, as the CPU would run it through window 3 but at a full 20-bit ISA address: "
            "io_read / mem_read (side effects as from the CPU: an NE2000 data-port read advances its remote DMA), "
            "io_write / mem_write, io_peek / mem_peek (what the card shows, no side effect), reset (one RESET DRV "
            "pulse to both slots) and latch (write the #9FBD byte). An empty slot reads #FF. While a TTD session "
            "records, a cycle with an effect parks the CPU and is marked as a debugger edit. Answer: action, slot, "
            "address, value (read or written), latch. Example: {\"action\": \"io_read\", \"slot\": 2, \"address\": "
            "\"#30A\"} reads the RTL8019AS ID byte #50.";
        paths[kControl]["post"]["parameters"][0]["name"] = "id";
        paths[kControl]["post"]["parameters"][0]["in"] = "path";
        paths[kControl]["post"]["parameters"][0]["required"] = true;
        paths[kControl]["post"]["parameters"][0]["schema"]["type"] = "string";
        paths[kControl]["post"]["requestBody"]["required"] = true;
        Json::Value& body = paths[kControl]["post"]["requestBody"]["content"]["application/json"]["schema"];
        body["type"] = "object";
        body["required"].append("action");
        body["properties"]["action"]["type"] = "string";
        for (const char* a : {"io_read", "io_write", "io_peek", "mem_read", "mem_write", "mem_peek", "reset", "latch"})
            body["properties"]["action"]["enum"].append(a);
        body["properties"]["slot"]["type"] = "integer";
        body["properties"]["slot"]["description"] = "1 (J6) or 2 (J7); not used by reset / latch";
        body["properties"]["address"]["description"] = "20-bit ISA address: integer or \"#..\" / \"0x..\" string";
        body["properties"]["value"]["description"] = "0-255 for writes and latch: integer or \"#..\" / \"0x..\" string";
        paths[kControl]["post"]["responses"]["200"]["description"] = "Cycle done";
        paths[kControl]["post"]["responses"]["400"]["description"] = "Bad body, slot, address or value";
        paths[kControl]["post"]["responses"]["404"]["description"] = "Emulator not found, or no ISA slots";
        body["properties"]["action"]["enum"].append("journal_clear");
        body["properties"]["action"]["enum"].append("journal_on");
        body["properties"]["action"]["enum"].append("journal_off");

        const char* kJournal = "/api/v1/emulator/{id}/state/isa/journal";
        paths[kJournal]["get"]["summary"] = "Get the ISA access journal";
        paths[kJournal]["get"]["tags"].append("ISA Slots");
        paths[kJournal]["get"]["description"] =
            "Who touched which card register: the last `last` ISA cycles and bus events (default 64, 0 = all, up to "
            "512 kept), oldest first - frame, t (base T-state in the frame), pc, slot, space (io / memory), access "
            "(read / write), address (20-bit ISA), cpu_address (window 3), value, what (the card's register: CR, ISR, "
            "data port, ...; or RESET DRV, a stall). Interrupt events carry event = irq (with slot for a line edge): "
            "IRQ line high / low with the card's cause, PIO port B requests (vector), INT acknowledged (IM, vector, "
            "table), RETI; irq_events repeats the last `last` of them from their own ring (128), which polling does not "
            "flush. Recorded live and while a TTD recording replays; control/isa "
            "journal_clear / journal_on / journal_off.";
        paths[kJournal]["get"]["parameters"][0]["name"] = "id";
        paths[kJournal]["get"]["parameters"][0]["in"] = "path";
        paths[kJournal]["get"]["parameters"][0]["required"] = true;
        paths[kJournal]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kJournal]["get"]["parameters"][1]["name"] = "last";
        paths[kJournal]["get"]["parameters"][1]["in"] = "query";
        paths[kJournal]["get"]["parameters"][1]["schema"]["type"] = "integer";
        paths[kJournal]["get"]["responses"]["200"]["description"] = "The journal";
        paths[kJournal]["get"]["responses"]["404"]["description"] = "Emulator not found, or no ISA slots";
    }
