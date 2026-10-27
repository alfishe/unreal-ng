// Example: let the registry detect the format of a source, decode it, show the first lines, encode it back with the
// same codec and check that the bytes are identical (the byte-exact round trip every codec guarantees, D-1).
//
//   unrealasmexampledecodeencode <file>

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "unrealasm/unrealasm.h"

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: " << argv[0] << " <file>\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    using namespace unrealasm;
    const DetectResult detected = CodecRegistry::Builtin().Detect(bytes);
    for (const DetectCandidate& c : detected.candidates)
        std::cout << "candidate " << c.codec->Info().id << " " << c.score << "\n";
    const ISourceCodec* codec = detected.chosen ? detected.chosen : CodecRegistry::Builtin().Find("text");

    const DecodeResult decoded = codec->Decode(bytes, {});
    const SourceDocument& document = decoded.document;
    std::cout << "codec " << codec->Info().id << ", dialect '" << document.dialect << "', "
              << encoding::CodePageName(document.codePage) << ", " << document.lines.size() << " line(s)\n";
    for (size_t i = 0; i < document.lines.size() && i < 10; ++i)
        std::cout << "  " << i + 1 << ": " << document.lines[i].text << "\n";

    const EncodeResult encoded = codec->Encode(document, {});
    const bool identical = encoded.bytes == bytes;
    std::cout << "round trip: " << (identical ? "identical" : "DIFFERENT") << "\n";
    return identical ? 0 : 1;
}
