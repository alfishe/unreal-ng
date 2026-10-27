// Example: every codec reads and writes every version of its format. Open a hobeta file, show which versions it is
// consistent with and which one was chosen, then write it as another version and print what the conversion reports
// (keywords the target version does not have stay text, with a warning).
//
//   unrealasmexampleconvertversion <file.$X> <version> <out.$X>
//   e.g. unrealasmexampleconvertversion testdata/alasm/AL442nfo.$H 5.07 scratch/AL442nfo.$H

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "unrealasm/unrealasm.h"

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: " << argv[0] << " <file.$X> <version> <out.$X>\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    using namespace unrealasm;
    containers::TrdosFile file;
    std::string error;
    if (!containers::ReadHobeta(bytes, file, error))
    {
        std::cerr << error << "\n";
        return 1;
    }
    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    if (!detected.chosen)
    {
        std::cerr << "no codec: " << detected.reason << "\n";
        return 1;
    }
    const ISourceCodec& codec = *detected.chosen;
    std::cout << "codec " << codec.Info().id << ", versions it knows:";
    for (const Subversion& v : codec.Info().subversions)
        std::cout << " " << v.id;
    std::cout << "\n";

    DecodeOptions options;
    options.catalog = file.Hints();
    const DecodeResult decoded = codec.Decode(file.data, options);
    std::cout << "the file is consistent with:";
    for (const std::string& v : decoded.subversions)
        std::cout << " " << v;
    std::cout << "\nread as " << decoded.document.subversion << "\n";
    for (const Diagnostic& d : decoded.diagnostics)
        std::cout << "  decode: " << d.message << "\n";

    EncodeOptions to;
    to.subversion = argv[2];
    const EncodeResult encoded = codec.Encode(decoded.document, to);
    for (const Diagnostic& d : encoded.diagnostics)
        std::cout << "  encode line " << d.line << ": " << d.message << "\n";
    if (!encoded.ok)
        return 1;
    file.data = encoded.bytes;
    file.length = static_cast<uint16_t>(encoded.bytes.size());
    file.tail.clear();
    file.sectors = 0;
    const std::vector<uint8_t> out = containers::WriteHobeta(file);
    std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    std::cout << "written " << argv[3] << " as version " << argv[2] << "\n";
    return 0;
}
