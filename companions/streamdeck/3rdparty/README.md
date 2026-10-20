# Third-party sources of the Stream Deck companion

Embedded source, built by `companions/streamdeck/CMakeLists.txt`; nothing is downloaded at configure time. Licences:
[`THIRD_PARTY_NOTICES.md`](../../../THIRD_PARTY_NOTICES.md).

| Path | Library | Version | Used for |
|------|---------|---------|----------|
| `hidapi/` | [hidapi](https://github.com/libusb/hidapi): `hidapi/hidapi.h`, the mac, windows and linux (hidraw) backends, licences | 0.15.0 | USB HID access to the decks |
| `nlohmann/` | [nlohmann/json](https://github.com/nlohmann/json) headers | 3.11.3 | JSON everywhere |
| `httplib.h` | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | 0.61.0 | the automation server (WebAPI + MCP) |
| `fkYAML.hpp` | [fkYAML](https://github.com/fktn-k/fkYAML) single header | 0.5.0 | scenario YAML |
| `rapidcsv.h` | [rapidcsv](https://github.com/d99kris/rapidcsv) | 9.07 | scenario CSV / TSV |
| `nanosvg.h`, `nanosvgrast.h` | [nanosvg](https://github.com/memononen/nanosvg) | | SVG icons |
| `stb_image.h` | [stb](https://github.com/nothings/stb) | | PNG / JPEG / GIF loading |
