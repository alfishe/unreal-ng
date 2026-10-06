// Example: detect the code page, line ends and text-ness of a file with the encoding detectors alone (decision
// D-13: they work without any codec), and print the file as UTF-8.
//
//   unrealasmexampledetectencoding <file>

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "unrealasm/encoding.h"

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: " << argv[0] << " <file>\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    using namespace unrealasm::encoding;
    const CodePageGuess best = CodePageDetector().Best(bytes);
    std::cout << "code page:  " << CodePageName(best.codePage) << " (confidence " << best.confidence << ")\n";
    std::cout << "line end:   " << LineEndName(LineEndDetector().Detect(bytes)) << "\n";
    std::cout << "text score: " << TextBinaryDetector().TextScore(bytes) << "\n\n";
    std::cout << ToUtf8(bytes, best.codePage);
    return 0;
}
