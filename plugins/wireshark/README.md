# Wireshark integration

`unreal-ng-extcap.py` puts every running unreal-ng emulator into Wireshark's capture interface list and captures its
network traffic live: what the machine's network adapters send and receive (frame cards' Ethernet frames, socket
adapters' operations as packets). Design: [network-traffic-debug](../../docs/inprogress/2026-10-04-network-traffic-debug/design.md).

## Without installing anything

Start the emulator's stream and point Wireshark at its port:

```bash
curl -s -X POST http://127.0.0.1:8090/api/v1/emulator/<id>/network/traffic -H 'Content-Type: application/json' -d '{"action":"stream","port":0}' | jq .stream
#  {"running":true,"port":53817,"clients":0,"wireshark":"wireshark -k -i TCP@127.0.0.1:53817"}
wireshark -k -i TCP@127.0.0.1:53817
```

`[NETWORK] TrafficStream=auto` (or a port) starts the stream with the machine.

## The extcap script

| OS | Copy into Wireshark's personal extcap folder (Help > About Wireshark > Folders) | Needs |
|---|---|---|
| macOS | `unreal-ng-extcap.py`, then `chmod +x` | Python 3 (`/usr/bin/env python3`) |
| Linux | `unreal-ng-extcap.py`, then `chmod +x` | Python 3 |
| Windows | `unreal-ng-extcap.py` **and** `unreal-ng-extcap.bat` (Wireshark runs only executables there) | Python 3 with the `py` launcher |

Restart Wireshark: the interfaces "unreal-ng <name> (<model>)" appear. Capturing one starts its stream when needed.
The WebAPI address is `http://127.0.0.1:8090` unless the interface's options or `UNREAL_WEBAPI_URL` say otherwise.
