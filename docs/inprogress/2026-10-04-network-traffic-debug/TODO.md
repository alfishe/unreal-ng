# TODO - network traffic for the debuggers

- [ ] Research: what the existing capture, the virtual network log and the serial peers offer; how other emulators
  and Wireshark extcap / pcapng show socket-level traffic; pick the capture format (pcapng with one interface per
  adapter is the first candidate)
- [ ] Design (`design.md`): the shared tap, time stamps and the TTD link, filters, live stream, the Qt window, the
  automation reports; owner questions one at a time
- [ ] Build: the tap in the network core, all adapters feeding it, the five automation surfaces + OpenAPI, Qt window,
  recipes, tests (incl. a TTD replay capturing the same packets) and a cost check with the tap off
