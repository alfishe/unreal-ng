// Example: open a hobeta file (a TR-DOS file saved on a PC, "NAME.$A"), let the registry pick the codec from the
// catalog fields and the bytes, print the source as UTF-8 text, then edit one line and write a new hobeta file:
// unchanged lines keep their original bytes, the edited one is tokenized again.
//
//   unrealasmexamplereadhobeta <file.$A> [<out.$A>]
//   e.g. unrealasmexamplereadhobeta testdata/tasm3/CALLLOAD.$A scratch/CALLLOAD.$A

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "unrealasm/unrealasm.h"

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        std::cerr << "usage: " << argv[0] << " <file.$A> [<out.$A>]\n";
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
    std::cout << "catalog: " << file.TrimmedName() << "." << file.type << " start " << file.start << " length " << file.length << "\n";

    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    if (!detected.chosen)
    {
        std::cerr << "no codec: " << detected.reason << "\n";
        return 1;
    }
    const ISourceCodec& codec = *detected.chosen;
    DecodeResult decoded = codec.Decode(file.data, {});
    std::cout << "codec: " << codec.Info().id << ", " << decoded.document.lines.size() << " lines\n\n" << decoded.document.Text() << "\n";
    if (argc == 2)
        return decoded.ok ? 0 : 1;

    // Edit: add a comment to the first line; the codec tokenizes that line and keeps every other line as it was
    decoded.document.lines[0].text += " ; edited by unreal-asm";
    const EncodeResult encoded = codec.Encode(decoded.document, {});
    if (!encoded.ok)
    {
        for (const Diagnostic& d : encoded.diagnostics)
            std::cerr << "line " << d.line << ": " << d.message << "\n";
        return 1;
    }
    file.data = encoded.bytes;
    file.length = static_cast<uint16_t>(encoded.bytes.size());
    file.tail.clear();
    file.sectors = 0;   // recomputed from the size
    const std::vector<uint8_t> out = containers::WriteHobeta(file);
    std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    std::cout << "\nwritten " << argv[2] << " (" << out.size() << " bytes)\n";
    return 0;
}
