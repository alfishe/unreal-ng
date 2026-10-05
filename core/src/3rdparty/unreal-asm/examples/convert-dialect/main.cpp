// Example: convert an assembler source to another assembler's dialect. Open a tokenized source (a hobeta file, e.g.
// ALASM's NAME.$H), decode it with the codec that recognizes it, convert it through the neutral IR to sjasmplus and
// print the result and what the conversion reports (renamed labels, expanded macros, lines it could not convert).
// A whole project (main source + included files, macros shared) goes through ConvertProject instead.
//
//   unrealasmexampleconvertdialect <file.$X> [dialect]
//   e.g. unrealasmexampleconvertdialect testdata/dialects/thelink/gsports.$H sjasmplus

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "unrealasm/unrealasm.h"

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "usage: " << argv[0] << " <file.$X> [dialect]\n";
        return 2;
    }
    const std::string target = argc == 3 ? argv[2] : "sjasmplus";
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
    DecodeOptions options;
    options.catalog = file.Hints();
    const DecodeResult decoded = detected.chosen->Decode(file.data, options);
    std::cerr << "read as " << decoded.document.dialect << " (" << detected.chosen->Info().id << " " << decoded.document.subversion << ")\n";

    const ConvertResult converted = Convert(decoded.document, target);
    for (const Diagnostic& d : converted.diagnostics)
        std::cerr << (d.severity == Severity::Info ? "  info" : "  warning") << (d.line ? " line " + std::to_string(d.line) : std::string()) << ": "
                  << d.message << "\n";
    if (!converted.ok)
    {
        std::cerr << "no conversion from " << decoded.document.dialect << " to " << target << "\n";
        return 1;
    }
    // The document is Unicode text; the target's codec writes it in the code page that assembler expects
    if (const ISourceCodec* codec = CodecRegistry::Builtin().Find(target))
    {
        const std::vector<uint8_t> out = codec->Encode(converted.document, {}).bytes;
        std::cout.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    else
        std::cout << converted.document.Text() << "\n";
    return 0;
}
