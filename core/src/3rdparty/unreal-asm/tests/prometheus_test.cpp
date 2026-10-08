// PROMETHEUS: the codec (source records + symbol table, byte-exact and canonical) on real saves of Proxima's D80
// source tape, the frontend (PROMETHEUS -> sjasmplus) construct by construct, labels laid out from the conversion equal
// to the values PROMETHEUS stored in each save's table, and with UNREAL_ASM_SJASMPLUS a probe assembled to the bytes
// PROMETHEUS (48K tape edition, in unreal-ng) built from it. research-prometheus.md.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

#include "codecs/prometheus/prometheuscodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
const char* const kSaves[] = {"d80-pfill1", "d80-pgraphics", "d80-minput", "d80-mkeys2", "d80-pplots"};

std::vector<std::string> ToSjasmplus(const std::string& prometheus)
{
    const ConvertResult r = Convert(SourceDocument::FromText(prometheus, "prometheus"), "sjasmplus");
    EXPECT_TRUE(r.ok);
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t first = l.text.find_first_not_of(' ');
        if (first == std::string::npos || l.text[first] == ';')
            continue;
        const size_t comment = l.text.find(" ;", first);
        out.push_back(l.text.substr(first, comment == std::string::npos ? std::string::npos : comment - first));
        while (!out.back().empty() && out.back().back() == ' ')
            out.back().pop_back();
    }
    return out;
}

std::filesystem::path ScratchDirectory()
{
    std::random_device random;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("unreal-asm-tests-" + std::to_string(stamp) + "-" + std::to_string(random()));
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

TEST(PrometheusCodec_Test, RealSavesDecodeByteExactAndCanonical)
{
    // Five saves of Proxima's D80 source tape: the text is PROMETHEUS' own view (the same as an independent decoder's,
    // research-prometheus.md §6), written back byte for byte, and every record rebuilt from its text alone
    const codecs::PrometheusCodec codec;
    for (const char* name : kSaves)
    {
        const std::vector<uint8_t> bytes = ReadTestData(std::string("prometheus/") + name + ".bin");
        EXPECT_EQ(codec.Detect(bytes, {}), 92) << name;
        const DecodeResult decoded = codec.Decode(bytes, {});
        ASSERT_TRUE(decoded.ok) << name;
        EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText(std::string("prometheus/") + name + ".txt")) << name;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, bytes) << name << ": byte-exact";
        std::vector<codecs::PrometheusCodec::Symbol> symbols;
        size_t used = 0;
        ASSERT_TRUE(codecs::PrometheusCodec::ReadSymbols(std::span<const uint8_t>(decoded.document.attrs.bytes).subspan(2), symbols, used));
        size_t canonical = 0;
        for (const SourceLine& line : decoded.document.lines)
        {
            std::vector<uint8_t> record;
            std::string error;
            ASSERT_TRUE(codecs::PrometheusCodec::EncodeLine(line.text, symbols, record, error)) << name << ": " << line.text << ": " << error;
            canonical += record == line.attrs.bytes;
        }
        EXPECT_EQ(canonical, decoded.document.lines.size()) << name;
    }
}

TEST(PrometheusCodec_Test, TheTwoMiddleBytesAreTheRecordsChecksum)
{
    // SAVE writes the records and the table as one chained block: between them the first part's checksum (#FF and the
    // records, XOR) and #FF; LOAD reads the whole block, so a file needs them right
    const std::vector<uint8_t> bytes = ReadTestData("prometheus/d80-pfill1.bin");
    size_t length = 0;
    ASSERT_TRUE(codecs::PrometheusCodec::FindSourceLength(bytes, length));
    EXPECT_EQ(length, 659u);
    uint8_t checksum = 0xFF;
    for (size_t k = 0; k < length; ++k)
        checksum ^= bytes[k];
    EXPECT_EQ(bytes[length], checksum);
    EXPECT_EQ(bytes[length + 1], 0xFF);
}

TEST(PrometheusCodec_Test, AFileFromText)
{
    // PROBE.txt encoded from text alone: PROBE.tap holds this save (header Param2 = the records' length), which
    // PROMETHEUS loaded with LOAD :probe and showed line for line
    const codecs::PrometheusCodec codec;
    SourceDocument document = SourceDocument::FromText(ReadTestText("dialects/prometheus/PROBE.txt"), "prometheus");
    while (!document.lines.empty() && document.lines.back().text.empty())
        document.lines.pop_back();
    const EncodeResult encoded = codec.Encode(document, {});
    ASSERT_TRUE(encoded.ok);
    std::vector<containers::TapeBlock> blocks;
    std::string error;
    ASSERT_TRUE(containers::ReadTapeBlocks(ReadTestData("dialects/prometheus/PROBE.tap"), blocks, error)) << error;
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[1].data, encoded.bytes);
    EXPECT_EQ(blocks[0].data[15] | (blocks[0].data[16] << 8), 167) << "Param2: the records' length";
    const DecodeResult decoded = codec.Decode(encoded.bytes, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.Text(), document.Text());
}

TEST(PrometheusFrontend_Test, Constructs)
{
    // Left to right on 16-bit words, / and ? unsigned (the backend masks to 16 bits); HX / LY the index halves; SLIA is SLI
    EXPECT_EQ(ToSjasmplus("         ld   a,7*3+1/2\n         ld   bc,100?7\n         ld   hx,5\n         ld   a,ly\n         slia b"),
              (std::vector<std::string>{"LD A,(7*3+1&#FFFF)/(2&#FFFF)", "LD BC,100%7", "LD IXH,5", "LD A,IYL", "SLI B"}));
    // $ in an item of DEFB / DEFW is that item's address; DEFS is a hole (the bytes keep what they held)
    EXPECT_EQ(ToSjasmplus("         defw 1,$,$\n         defs 3"), (std::vector<std::string>{"DW 1,$+2,$+4", "ORG $+3"}));
    // PUT: the bytes go elsewhere, the address goes on; ORG ends it
    const std::vector<std::string> put = ToSjasmplus("         org  60000\n         put  62000\nHERE     ld   hl,HERE\n         org  60200");
    EXPECT_EQ(put, (std::vector<std::string>{"ORG 60000", "__PROMETHEUS_PUT1=$", "ORG 62000", "DISP __PROMETHEUS_PUT1", "HERE    LD HL,HERE", "ENT", "ORG 60200"}));
}

TEST(PrometheusFrontend_Test, LabelsEqualTheValuesPrometheusStored)
{
    // A save keeps each label's value from the last assembly: the conversion laid out from where that assembly put the
    // code (no ORG: PROMETHEUS puts it after the source) gives the same values; +fill1 / +plots / +graphics align an
    // ORG to 256 / 512 from a label, which only the right start reproduces
    const codecs::PrometheusCodec codec;
    const std::map<std::string, int> starts = {{"d80-pfill1", 41892}, {"d80-pplots", 42143}, {"d80-pgraphics", 43997}};
    for (const auto& [name, start] : starts)
    {
        const DecodeResult decoded = codec.Decode(ReadTestData("prometheus/" + name + ".bin"), {});
        ASSERT_TRUE(decoded.ok);
        std::vector<codecs::PrometheusCodec::Symbol> stored;
        size_t used = 0;
        ASSERT_TRUE(codecs::PrometheusCodec::ReadSymbols(std::span<const uint8_t>(decoded.document.attrs.bytes).subspan(2), stored, used));
        SourceDocument document = decoded.document;
        document.lines.insert(document.lines.begin(), SourceLine{"         org  " + std::to_string(start), {}, -1});
        const symbols::SourceSymbolsResult r = symbols::SymbolsFromSource(document);
        EXPECT_TRUE(r.ok) << name;
        std::map<std::string, uint32_t> laid;
        for (const symbols::Symbol& s : r.set.symbols)
        {
            const auto address = symbols::CpuAddress(s);
            laid[s.name] = address ? *address : static_cast<uint32_t>(s.location.offset);
        }
        size_t compared = 0;
        for (const auto& s : stored)
            if (s.defined && laid.count(s.name))
            {
                EXPECT_EQ(laid.at(s.name) & 0xFFFF, s.value) << name << ": " << s.name;
                ++compared;
            }
        EXPECT_GT(compared, 20u) << name;
    }
}

// --- Assembled by sjasmplus (opt-in: UNREAL_ASM_SJASMPLUS) ----------------------------------------------------------

TEST(PrometheusFrontend_Test, ProbeAssemblesToWhatPrometheusBuilt)
{
    // PROMETHEUS 48K (installed at 24000) assembled PROBE over memory filled with #AA: 48 bytes from 60000, 4 from
    // 60200, 8 from 62000 (the PUT)
    const char* sjasmplus = std::getenv("UNREAL_ASM_SJASMPLUS");
    if (!sjasmplus)
        GTEST_SKIP() << "set UNREAL_ASM_SJASMPLUS to the sjasmplus binary";
    const codecs::PrometheusCodec codec;
    std::vector<containers::TapeBlock> blocks;
    std::string error;
    ASSERT_TRUE(containers::ReadTapeBlocks(ReadTestData("dialects/prometheus/PROBE.tap"), blocks, error)) << error;
    const DecodeResult decoded = codec.Decode(blocks[1].data, {});
    ASSERT_TRUE(decoded.ok);
    const ConvertResult r = Convert(decoded.document, "sjasmplus");
    ASSERT_TRUE(r.ok);
    const std::filesystem::path dir = ScratchDirectory();
    WriteBytes(dir / "probe.asm", codecs::SjasmplusCodec().Encode(r.document, {}).bytes);
    const std::string harness = "        DEVICE ZXSPECTRUM48\n        ORG 60000\n        DS 256,#AA\n        ORG 62000\n        DS 16,#AA\n        ORG 0\n"
                                "        INCLUDE \"probe.asm\"\n        SAVEBIN \"a.bin\",60000,48\n        SAVEBIN \"b.bin\",60200,4\n        SAVEBIN \"c.bin\",62000,8\n";
    WriteBytes(dir / "harness.asm", std::vector<uint8_t>(harness.begin(), harness.end()));
#ifdef _WIN32
    const std::string command = "cd /d \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#else
    const std::string command = "cd \"" + dir.string() + "\" && \"" + sjasmplus + "\" --nologo harness.asm > out.txt 2>&1";
#endif
    EXPECT_EQ(std::system(command.c_str()), 0);
    EXPECT_EQ(ReadBytes(dir / "a.bin"), ReadTestData("dialects/prometheus/PROBE-60000.bin"));
    EXPECT_EQ(ReadBytes(dir / "b.bin"), ReadTestData("dialects/prometheus/PROBE-60200.bin"));
    EXPECT_EQ(ReadBytes(dir / "c.bin"), ReadTestData("dialects/prometheus/PROBE-62000.bin"));
    std::filesystem::remove_all(dir);
}
