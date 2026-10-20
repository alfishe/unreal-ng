// Baseline JPEG encoder for the decks: YCbCr 4:4:4 (the + / + XL key decoder garbles 4:2:0), the standard Annex K
// quantisation and Huffman tables, libjpeg's quality scale, colour conversion and integer ("islow") DCT.
// Started from TooJpeg (Stephan Brumme, zlib licence, notice in jpeg.cpp); measured in POC 032
// (tools/poc/032-streamdeck-jpeg-encoder).
#pragma once
#include <cstdint>
#include <vector>

namespace sdb::jpeg
{

// Packed RGB, 3 bytes per pixel, rows without padding; 1 <= width, height <= 65535; quality 1..100 (clamped).
// Returns the JPEG file, empty on bad arguments. Thread-safe: no shared state.
[[nodiscard]] std::vector<std::uint8_t> Encode(const std::uint8_t* rgb, int width, int height, int quality);

}  // namespace sdb::jpeg
