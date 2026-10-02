# Vendored decoders

Built only when selected by the CMake options `EVE_DECODER_INFLATE`, `EVE_DECODER_PNG`
and `EVE_DECODER_JPEG` (`BUILTIN`, the default). Both are git submodules pinned to the
versions below.

| Directory | Project | Version | License | Used for |
|---|---|---|---|---|
| `miniz/` | [richgel999/miniz](https://github.com/richgel999/miniz) | tag `3.1.2` | MIT | `tinfl` streaming inflate (`CMD_INFLATE`) |
| `stb/` | [nothings/stb](https://github.com/nothings/stb) | commit `2c980bb` (2026-08-01) | MIT / public domain | `stb_image.h`, PNG and baseline JPEG (`CMD_LOADIMAGE`, M-JPEG frames) |

No vendored symbol leaves the library (arch §6.3), checked by the `eve-check-symbols`
target:

- `tinfl` is compiled in `src/eve-vendor-tinfl.c` with every function renamed into the
  `EveLib` prefix (`tinfl_decompress` → `EveLibTinflDecompress`, ...). `config/miniz_export.h`
  stands in for the header miniz's own CMake build would generate.
- `stb_image` is compiled in `src/eve-vendor-stb.cpp` with `STB_IMAGE_STATIC`: all of its
  functions have internal linkage.

stb_image expands indexed PNG images to RGBA; the built-in PNG decoder maps each pixel
back to the first palette entry of the same color (the picture is identical; an image with
duplicate palette entries gets the first index).
