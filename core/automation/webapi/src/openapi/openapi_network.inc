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
            "firmware programmed it), frame_bits, rts, dtr, bytes_in, bytes_out, lost (frames the MCU had no room for). "
            "atm2ioesp: the ATM2IOESP card (machine.internal_io says the machine has the connector): fitted, address (its bus "
            "address), the peer fields and the 16550 registers as in com_port. "
            "slots (machines with expansion slots, the Sprinter's ISA slots): id (isa1 / isa2), bus, label, configured, card, "
            "not_fitted (why a configured card is missing) and the card's report - an NE2000: chip (RTL8019AS / UM9003 / NE1000), "
            "base, irq, mac, port_key, link (ethernet-gateway | none), running, irq_level, stalled, registers (cr, isr, imr, rcr, "
            "tcr, dcr, pstart, pstop, bnry, curr, tpsr, tbcr, rsar, rbcr, crda, par, config1-3), counters (tx_frames, rx_frames, "
            "rx_filtered, rx_missed, rx_ring_full_waits, tx_no_link, reset_port_reads); a SprinterESP (card sprinteresp): board, "
            "chip TL16C550C, uart_clock_hz (14745600), base #3E8, irq 3, irq_gating, port_key (isa1.uart0), decode, peer_spec (the "
            "configured line), uart (ier, iir, fcr, lcr, mcr, lsr, msr, scr, divisor, baud, afe, rts, cts, loopback, intr, rx_fifo, "
            "tx_fifo, bytes_in, bytes_out, overruns), pins (out1_esp_reset, out2_esp_gpio0_low), peer (kind, target, connected, "
            "pending), esp (module, firmware, state running | reset held | ROM download mode, hardware_resets, wifi, ssid, ip, mac, "
            "baud, factory_baud, flow_control, line_mismatch, requests, at_session (echo, mux, passive_receive, sysstore, dns, "
            "links: link, proto, remote, rx_pending, peer_closed), exchanges: the last 32 AT requests and replies); a 3Com "
            "EtherLink III (card el3c509b, network phase SN5): chip (3C509B-TPO / 3C509B-TP), product_id, base (the active I/O "
            "base), eeprom_base, id_port (the ID port the last zero picked, #100-#1F0), ids (AUTOINIT / ID_WAIT with the sequence "
            "position / ID_CMD, tag), activated, irq, irq_level, irq_driven, mac (the EEPROM's), station_address (window 2), "
            "port_key, link, link_state (no cable / link beat disabled / link test running / link pass), window, powered_down, "
            "registers (status, configuration_control, address / resource / internal configuration, interrupt and read zero "
            "masks, rx_filter, the three thresholds, rx / tx / statistics enabled, media, net and FIFO diagnostic, EEPROM command "
            "and data), fifo (tx_capacity, tx_free, tx_packets, tx_bytes, tx_on_wire, rx_capacity, rx_free, rx_packets, rx_bytes, "
            "rx_status, tx_status_stack), eeprom (product and manufacturer IDs, configuration words, checksums, busy, all 64 "
            "words), statistics (window 6), counters (tx_frames, rx_frames, rx_filtered, rx_fifo_full_waits, tx_no_link, "
            "tx_underruns, tx_overruns, rx_underruns, id_sequences, id_eeprom_reads, activations, power_on_resets, commands), "
            "events (the last 64: isolation, activation, resets, underruns) and summary (one line). "
            "ethernet_gateway (with a frame-level "
            "card): mode nat, router_mac, router_ip, dns_ip, ports (port, mac, queued_frames, dhcp_lease), arp (mac, ip), tcp "
            "(guest, remote, state, socket, queued_bytes, unacked, guest_window, guest_mss, guest_fin, host_fin, retries), udp, "
            "forwarded_guest_ports, counters (frames both ways, switched, arp_replies, dhcp, dns, udp, icmp, tcp_connections, "
            "tcp_refused, retransmits, resets, unsupported, fragments, bad_checksum, runts, queue_drops, ring_full_waits), "
            "captured_frames. "
            "zifi: the TS AVR firmware's ZiFi (machine.zifi: TS-Conf, a ZX-Evo with avr_firmware ts2016-02 / ts2016-04): "
            "fitted, avr_firmware, api (0 off, 1 on), data_register (zifi | rs232: what #00EF..#BFEF reaches, the last "
            "ZIFR / ZOFR or RIFR / ROFR read decides), er, imr, isr, zibtr, zitor_ms, ribtr, ritor_ms, zifi_rx / zifi_tx "
            "and rs232_rx / rs232_tx (ring fill), the peer fields of its line to the ESP, bytes_in, bytes_out, dropped "
            "(bytes the full ring lost). card.kind is none when "
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
            "\"zxnetusb\" | \"zxwifi\" | \"atm2ioesp\" | a list with ','; ATM2IOESP sits on the ATM Turbo 2+ INTERNAL I/O connector, not "
            "the ZX-Bus), atm2ioesp (what the ATM2IOESP card's 16550 is wired to, the com_port values, default \"at\"), "
            "atm2ioesp_address (its bus address on #FB: \"0xF0\" Rev 1.5 / 2.0, \"0xF8\" Rev 1.0), "
            "host_access (true / false), dns_mode (\"host\" | \"pass\"), hosts (\"name=a.b.c.d,...\"), forwards "
            "(\"tcp:<hostport>:<guestport>,...\"), connect_timeout_ms (500..120000), com_port (\"none\" | \"loopback\" | \"espnet[,<baud>]\" | \"at[,<firmware>][,<baud>]\" (an ESP module's firmware rate; without it the port's: 38400 on the ATM Turbo 2+ keyboard controller, 115200 elsewhere; <firmware> esp32 | esp8266 | esp8266-at221 | esp8266-at222 for this module alone, else esp_chip) | \"zifi-native[,<s3|esp01s>][,<baud>]\" (the 2026 ZiFi firmware: binary frames 5A CMD LEN DATA XOR; s3 = ESP32-S3-Zero s3-native-0.6.94, esp01s = ESP-01S native-0.2.2) | \"modem[,<guest port>]\" (a Hayes modem: ATDT dials host:port or a phone book number; with a guest port it also answers host clients of that port) | "
            "\"tcp:<a.b.c.d|localhost>:<port>\" | \"serial:<device>[,<baud>]\"; the machine's own serial port: the ZX-Evo "
            "AVR's, the ATM Turbo 2+ keyboard controller's RS-232), zx_wifi (the same values: what the ZX-WiFi card's 16550 is wired to, default \"at\"), com_modem_lines (true / false), esp_chip (\"esp32\" | \"esp8266\" (NonOS AT 1.7.4) | \"esp8266-at221\" | \"esp8266-at222\" (ESP-AT 2.2.1 / 2.2.2; the Sprinter's SprinterESP takes an ESP8266 build from here, else esp8266-at222)), isa1_peer / isa2_peer (Sprinter: what the UART card in that ISA slot is wired to, com_port values; default \"at\" = the SprinterESP's ESP-12F, \"modem\" = the ISA modem card's own Hayes modem, \"none\" = SprinterSerial's COM1), isa1_peer_b / isa2_peer_b (SprinterSerial's COM2 at #2F8), modem_phonebook (\"<number>=<host>[:<port>],...\": the numbers a Hayes modem peer dials with ATDT; a dialed number with letters, '.' or ':' is a host itself), avr_firmware (ZX-Evo, the AVR firmware the COM port behaves like: "
            "\"baseconf\" | \"base2010\" | \"base2011-04\" | \"base2011-05\" | \"base2011-09\" | \"base2013\" | \"base2023\" | \"ts\" | "
            "\"ts2013\" | \"ts2016-02\" | \"ts2016-04\"; a change restarts the AVR's UART), kbc_firmware (ATM Turbo 2+, the keyboard "
            "controller's firmware: \"none\" | \"v22-7\" | \"v22-11\" | \"v22-12\" | \"v31-7\" | \"v31-11\" | \"v32-7\" | "
            "\"v32-11\" | \"v40\" | \"v41\"; RS-232 from v31 on; a change fits a new controller, which boots afresh), zifi (TS-Conf, "
            "a ZX-Evo with a TS-Labs AVR firmware: what the AVR's ZiFi UART is wired to, the com_port values, default \"none\" = "
            "no ZiFi board; \"at\" = the original board's ESP-01 (ESP8266, 1 MB) with Espressif's AT firmware, NonOS 1.7.4 unless esp_chip names an ESP8266 build or "
            "\"at,esp8266-at222\" picks ESP-AT 2.2.2; \"zifi-native[,s3|esp01s]\" = the 2026 ZiFi firmwares). The ESP's firmware, state and session are "
            "\"esp\" in each port of GET /state/network. The settings in force are \"settings\" in "
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
    // Ethernet frames of the frame-level cards (core EthernetAccess): the capture and a frame towards a card
    {
        const char* kFrames = "/api/v1/emulator/{id}/network/frames";
        paths[kFrames]["get"]["summary"] = "Get the Ethernet frame capture";
        paths[kFrames]["get"]["tags"].append("Network");
        paths[kFrames]["get"]["description"] =
            "The Ethernet gateway keeps the last 256 frames of the frame-level cards (the Sprinter's NE2000 or 3C509B), both ways. "
            "format=json (default): frames[] with index, frame (the machine frame), direction (to_card / from_card), port, "
            "length, summary (\"ARP who-has 10.0.2.2 tell 10.0.2.15\", \"IPv4 10.0.2.15:1025 > 192.0.2.10:80 TCP S ...\"), "
            "hex; link=isa2.eth picks one card, last=N the newest N (default 64). format=pcap: a pcap file "
            "(LINKTYPE_ETHERNET, timestamps = machine frame x 20 ms) for Wireshark / tcpdump. 404 without a frame-level card.";
        paths[kFrames]["get"]["parameters"][0]["name"] = "id";
        paths[kFrames]["get"]["parameters"][0]["in"] = "path";
        paths[kFrames]["get"]["parameters"][0]["required"] = true;
        paths[kFrames]["get"]["parameters"][0]["schema"]["type"] = "string";
        for (const char* q : {"link", "last", "format"})
        {
            Json::Value p;
            p["name"] = q;
            p["in"] = "query";
            p["schema"]["type"] = "string";
            paths[kFrames]["get"]["parameters"].append(p);
        }
        paths[kFrames]["get"]["responses"]["200"]["description"] = "The capture (JSON, or a pcap file)";
        paths[kFrames]["get"]["responses"]["404"]["description"] = "Emulator not found, or no frame-level card";

        const char* kFrame = "/api/v1/emulator/{id}/network/frame";
        paths[kFrame]["post"]["summary"] = "Inject an Ethernet frame towards a card";
        paths[kFrame]["post"]["tags"].append("Network");
        paths[kFrame]["post"]["description"] =
            "Body {\"link\": \"isa2.eth\", \"hex\": \"FFFFFFFFFFFF525...\"}: a whole frame (14-1514 bytes, no CRC) offered to "
            "the card at the next frame boundary, as if from the wire (the card's address filter and ring apply). A tool "
            "edit while TTD records.";
        paths[kFrame]["post"]["parameters"][0]["name"] = "id";
        paths[kFrame]["post"]["parameters"][0]["in"] = "path";
        paths[kFrame]["post"]["parameters"][0]["required"] = true;
        paths[kFrame]["post"]["parameters"][0]["schema"]["type"] = "string";
        paths[kFrame]["post"]["responses"]["200"]["description"] = "Queued";
        paths[kFrame]["post"]["responses"]["400"]["description"] = "Bad body, hex or link";
        paths[kFrame]["post"]["responses"]["404"]["description"] = "Emulator not found";
    }

