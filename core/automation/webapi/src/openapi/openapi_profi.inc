    // ZX Profi board chips - DeviceState::ProfiPeripherals: the same report CLI `state profi`, Lua/Python profi_state()
    // and the MCP aspect `profi` serve. Machines: PROFI, PROFI3 (no COM port), PROFI-PLUS. Anything else answers 404
    // with the reason.
    {
        const char* kReport = "/api/v1/emulator/{id}/state/profi";
        paths[kReport]["get"]["summary"] = "Get ZX Profi board chips";
        paths[kReport]["get"]["tags"].append("Profi State");
        paths[kReport]["get"]["description"] =
            "The ZX Profi's board peripherals: board (v3 or v5), port_map (ext_ports cpm / sys / v003 - the [PROFI] "
            "ExtPorts rule, dos_latch, cpm, rom14, hires, extended_map = the long port map answers now, "
            "long_ports_beside_vg93 = the V0.03 PROM's long ports beside the short VG93), ppi8255 (control word, mode, "
            "port_a / port_b / port_c_upper / port_c_lower with direction in / out and output_latch), pit8253 "
            "(clock_hz and counters[]: mode, rw, bcd, count_register, count, out, gate, counting, output_period_clk; "
            "v5 only) and usart8251 (mode_word, command_word, baud, data_bits, stop_half_bits, rts, dtr, status flags, "
            "bytes_in / bytes_out / overruns, com_interrupt_enable = the #B3 latch D0; v5 only). Peeked: reading changes "
            "nothing. 404 with the reason on machines that are not a Profi.";
        paths[kReport]["get"]["parameters"][0]["name"] = "id";
        paths[kReport]["get"]["parameters"][0]["in"] = "path";
        paths[kReport]["get"]["parameters"][0]["required"] = true;
        paths[kReport]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kReport]["get"]["responses"]["200"]["description"] = "ZX Profi board chips";
        paths[kReport]["get"]["responses"]["404"]["description"] = "Not a ZX Profi machine (the reason is in message)";

        const char* kActive = "/api/v1/emulator/state/profi";
        paths[kActive]["get"]["summary"] = "Get ZX Profi board chips (active emulator)";
        paths[kActive]["get"]["tags"].append("Profi State");
        paths[kActive]["get"]["description"] = "Same as /api/v1/emulator/{id}/state/profi for the only or selected emulator.";
        paths[kActive]["get"]["responses"]["200"]["description"] = "ZX Profi board chips";
        paths[kActive]["get"]["responses"]["400"]["description"] = "Several emulators running: use the {id} form";
        paths[kActive]["get"]["responses"]["404"]["description"] = "No emulator, or not a ZX Profi machine";
    }
