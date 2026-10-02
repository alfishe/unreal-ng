    // Network adapters (network adapters TDD, docs/inprogress/2026-09-30-nedoos-integration/tdd-network.md) -
    // DeviceState::Network: the same report CLI `network` / `state network`, Lua / Python network_state() and
    // the MCP aspect `network` use. Fitted by the machine config ([NETWORK] Card=ZXNETUSB) and the runtime
    // feature `network`. Without an adapter the answer says why.
    {
        const char* kReport = "/api/v1/emulator/{id}/state/network";
        paths[kReport]["get"]["summary"] = "Get network adapter state";
        paths[kReport]["get"]["tags"].append("Network");
        paths[kReport]["get"]["description"] =
            "Network adapters and the machine's virtual network, as of the last frame boundary. card: kind "
            "(ZXNETUSB), the card ports port_83AB / port_82AB / port_81AB, w5300_running (false while the chip is "
            "held in reset by #83AB bit 4), w5300_int (the chip's INTn: IR & IMR), int_to_z80 (the card holds the "
            "Z80's /INT low: w5300_int with #83AB bits 2 and 6), w5300_in_io_space, the chip's mac / ip / gateway / mask registers as the "
            "guest programmed them, and per W5300 socket n: mode (TCP / UDP / IPRAW / CLOSED), state (Sn_SSR by "
            "name) and ssr, ir, source_port, destination, tx_free, rx_received (Sn_RX_RSR), tcp_backlog (bytes "
            "waiting for receive memory), network_socket. virtual_network: host_access (false = only the virtual "
            "network's own services answer), network, gateway, dns, dns_mode (host = queries answered by the host "
            "resolver, pass = sent as UDP), hosts (the name table), dhcp_leases (mac, ip), sockets (id, proto, "
            "connected, remote, listen_port, bytes_in, bytes_out), guest_servers (guest_port, host_port on "
            "127.0.0.1, waiting_sockets, pending_clients), counters (dhcp_replies, dns_local_answers, "
            "dns_host_queries, echo_replies, host_events, link_resets) and recent_activity (the last 64 socket "
            "actions: frame, socket, proto, action, remote, status, bytes). com_port: fitted, flavor (evo = the "
            "ZX-Evo AVR's UART, zxwifi = a 16550 card), peer (loopback | tcp | serial), target, phase (idle | resolving | connecting | connected), error, modem_lines, connected, baud, "
            "frame_bits, divisor, the registers lcr / mcr / lsr / msr / ier / iir / scr, rts, cts, rx_fifo, tx_fifo, "
            "peer_pending (bytes the peer holds for the ZX), peer_baud (an ESP module's own rate; a mismatch with baud garbles both sides), bytes_in, bytes_out, overruns; an ESP module adds requests and recent_exchanges (request / reply: ESPNET frames by name, AT lines as text). "
            "machine_serial: the machine's own serial port when it is no 16550 (ATM Turbo 2+: the keyboard controller's RS-232; "
            "machine.serial_port atm2-kbc): fitted, flavor atm2kbc, kbc_firmware, the peer fields as in com_port, baud (as the "
            "firmware programmed it), frame_bits, rts, dtr, bytes_in, bytes_out, lost (frames the MCU had no room for). card.kind is none when "
            "only the COM port is fitted. 404 with the reason when no adapter is fitted.";
        paths[kReport]["get"]["parameters"][0]["name"] = "id";
        paths[kReport]["get"]["parameters"][0]["in"] = "path";
        paths[kReport]["get"]["parameters"][0]["required"] = true;
        paths[kReport]["get"]["parameters"][0]["schema"]["type"] = "string";
        paths[kReport]["get"]["responses"]["200"]["description"] = "Network adapter state";
        paths[kReport]["get"]["responses"]["404"]["description"] = "Emulator not found, or no network adapter fitted (the reason is in message)";

        const char* kActive = "/api/v1/emulator/state/network";
        paths[kActive]["get"]["summary"] = "Get network adapter state (active emulator)";
        paths[kActive]["get"]["tags"].append("Network");
        paths[kActive]["get"]["description"] = "Same as /api/v1/emulator/{id}/state/network for the only or selected emulator.";
        paths[kActive]["get"]["responses"]["200"]["description"] = "Network adapter state";
        paths[kActive]["get"]["responses"]["400"]["description"] = "Several emulators running: use the {id} form";
        paths[kActive]["get"]["responses"]["404"]["description"] = "No emulator, or no network adapter";

        const char* kConfig = "/api/v1/emulator/{id}/network/config";
        paths[kConfig]["post"]["summary"] = "Change network settings";
        paths[kConfig]["post"]["tags"].append("Network");
        paths[kConfig]["post"]["description"] =
            "Change the machine's [NETWORK] settings at runtime. Body: any of card (ZX-Bus cards: \"none\" | "
            "\"zxnetusb\" | \"zxwifi\" | \"zxnetusb,zxwifi\"), "
            "host_access (true / false), dns_mode (\"host\" | \"pass\"), hosts (\"name=a.b.c.d,...\"), forwards "
            "(\"tcp:<hostport>:<guestport>,...\"), connect_timeout_ms (500..120000), com_port (\"none\" | \"loopback\" | \"espnet[,<baud>]\" | \"at[,<baud>]\" (an ESP module's firmware rate; without it the port's: 38400 on the ATM Turbo 2+ keyboard controller, 115200 elsewhere) | "
            "\"tcp:<a.b.c.d|localhost>:<port>\" | \"serial:<device>[,<baud>]\"; the machine's own serial port: the ZX-Evo "
            "AVR's, the ATM Turbo 2+ keyboard controller's RS-232), zx_wifi (the same values: what the ZX-WiFi card's 16550 is wired to, default \"at\"), com_modem_lines (true / false), esp_chip (\"esp32\" | \"esp8266\"), avr_firmware (ZX-Evo, the AVR firmware the COM port behaves like: "
            "\"baseconf\" | \"base2010\" | \"base2011-04\" | \"base2011-05\" | \"base2011-09\" | \"base2013\" | \"base2023\" | \"ts\" | "
            "\"ts2013\" | \"ts2016-02\" | \"ts2016-04\"; a change restarts the AVR's UART), kbc_firmware (ATM Turbo 2+, the keyboard "
            "controller's firmware: \"none\" | \"v22-7\" | \"v22-11\" | \"v22-12\" | \"v31-7\" | \"v31-11\" | \"v32-7\" | "
            "\"v32-11\" | \"v40\" | \"v41\"; RS-232 from v31 on; a change fits a new controller, which boots afresh). The settings in force are \"settings\" in "
            "GET /state/network, with the host's serial devices in \"host_serial_devices\". Unset keys keep their value. Applied "
            "at the next frame boundary (at once while paused): the devices are fitted again, so every connection closes. A "
            "device the machine cannot take (a ZX-WiFi card on a ZX-Evo: #xxEF is the AVR's; a com_port with a keyboard controller "
            "firmware without RS-232) is listed in the state's "
            "not_fitted with the reason. "
            "The same keys as CLI `network set` and Lua / Python network_configure(). Refused (409) while a TTD "
            "recording runs.";
        paths[kConfig]["post"]["parameters"][0]["name"] = "id";
        paths[kConfig]["post"]["parameters"][0]["in"] = "path";
        paths[kConfig]["post"]["parameters"][0]["required"] = true;
        paths[kConfig]["post"]["parameters"][0]["schema"]["type"] = "string";
        paths[kConfig]["post"]["requestBody"]["content"]["application/json"]["schema"]["type"] = "object";
        paths[kConfig]["post"]["responses"]["200"]["description"] = "Accepted";
        paths[kConfig]["post"]["responses"]["400"]["description"] = "Unknown key or bad value (the reason is in message)";
        paths[kConfig]["post"]["responses"]["404"]["description"] = "Emulator not found";
        paths[kConfig]["post"]["responses"]["409"]["description"] = "A TTD recording is running";
    }
