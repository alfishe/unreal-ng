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
            "actions: frame, socket, proto, action, remote, status, bytes). 404 with the reason when no adapter is "
            "fitted.";
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
            "Change the machine's [NETWORK] settings at runtime. Body: any of card (\"zxnetusb\" | \"none\"), "
            "host_access (true / false), dns_mode (\"host\" | \"pass\"), hosts (\"name=a.b.c.d,...\"), forwards "
            "(\"tcp:<hostport>:<guestport>,...\"), connect_timeout_ms (500..120000). Unset keys keep their value. Applied "
            "at the next frame boundary (at once while paused): the card is fitted again, so every connection closes. "
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
