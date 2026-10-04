# TODO: Estex DSS aware debugging

Index: [README.md](README.md).

## Remaining

- [ ] Research: the DSS memory model, call mechanics (`RST #10` / `RST #08`), structures and addresses in a running
  system (DSS 1.71.57 and 1.71.66), with a short reference like the NedoOS kernel reference
- [ ] Requirements for the DSS layer (calls, programs, files and drives, memory, symbols, actions), each on every
  automation surface and in Qt, numbered like NK-n
- [ ] A Python prototype over the WebAPI (as POC 020 for NedoOS) checking the requirements on a live DSS
- [ ] First case: explain the "Unexpected application termination" of `TYPE` after leaving Flex Navigator
