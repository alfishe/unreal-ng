// The XAS codec: every XAS source found (demos, magazine disks, ZX Navigator, the XAS disks) and a file typed into
// XAS 7.447 in unreal-ng decode to the editor's layout, round-trip byte-exact with their sector slack, and XAS's line
// packer reproduces every line the editor wrote (research-xas.md).

#include <gtest/gtest.h>

#include "codecs/xas/xascodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
struct Sample
{
    const char* stem;      ///< testdata/xas/<stem>.$X and the expected text <stem>.txt
    const char* version;
    size_t lines;
    size_t kept;           ///< lines whose stored bytes the packer would not write (kept in the attributes)
};

const Sample kSamples[] = {
    {"CCINTROS__KISHKOID", "4.18", 321, 0},
    {"CCINTROS__matrix", "4.18", 333, 0},
    {"OBERON#5__mandelbr", "4.18", 313, 0},
    {"P#14VRZZ__KERNEL", "7.43c", 296, 0},        // lost the title's first byte: a 35-byte header
    {"P#14VRZZ__OOPSfix", "7.43c", 434, 0},
    {"SNG_2APP__DAINGY", "4.18", 359, 0},
    {"XAS505SE__XMACROS", "4.18", 87, 0},         // the macro file (start "aS")
    {"XAS7_43C__Read_Me", "7.43", 182, 0},        // red and green line marks
    {"XAS7_447__Read_Me", "7.43", 182, 0},
    {"XASCII__XASCII", "7.43", 1291, 119},        // written by the XASCII converter: lower-case labels and operands
    {"ZXNAV1_3__KERNEL", "7.43c", 127, 0},
    {"ZXNAV1_3__PT3daem", "7.43c", 60, 0},
    {"ZXNAV1_3__SCT.src", "7.43c", 377, 0},
    {"ZXNAV1_3__SET.SRC", "7.43c", 275, 0},
    {"ZXN__KERNEL", "7.43c", 114, 0},
    {"Zxf_04Ap__XMACROS", "4.18", 52, 0},
    {"Zxf_04Ap__Xas_help", "4.18", 79, 0},
    {"typed-in-xas7447__noname", "7.43", 31, 0},   // typed into XAS 7.447 in unreal-ng
};

containers::TrdosFile Unwrap(const Sample& sample)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(std::string("xas/") + sample.stem + ".$X"), out, error)) << sample.stem << ": " << error;
    return out;
}

std::vector<uint8_t> Sectors(const containers::TrdosFile& file)
{
    std::vector<uint8_t> all = file.data;
    all.insert(all.end(), file.tail.begin(), file.tail.end());
    return all;
}

std::vector<uint8_t> Body(const std::string& text, const std::string& version)
{
    std::vector<uint8_t> body;
    std::string error;
    EXPECT_TRUE(codecs::XasCodec::EncodeBody(text, version, body, error)) << text << ": " << error;
    return body;
}

std::vector<uint8_t> Bytes(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}
}  // namespace

TEST(XasCodec_Test, SourcesDecodeAndRoundTrip)
{
    const codecs::XasCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << sample.stem;
        EXPECT_EQ(decoded.document.subversion, sample.version) << sample.stem;
        EXPECT_EQ(decoded.document.lines.size(), sample.lines) << sample.stem;
        EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText(std::string("xas/") + sample.stem + ".txt")) << sample.stem;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, Sectors(file)) << sample.stem << ": byte-exact";
        size_t kept = 0;
        for (const SourceLine& line : decoded.document.lines)
            kept += line.attrs.bytes.size() > 1;
        EXPECT_EQ(kept, sample.kept) << sample.stem;
    }
}

TEST(XasCodec_Test, LineRules)
{
    using codecs::XasCodec;
    // no blanks stored; keywords and registers are tokens; text in capitals; the editor lays out the fields
    const std::vector<uint8_t> loop{'L', 'O', 'O', 'P', 0xAC, 0xDF, '(', 0xD3, '+', '5', ')'};
    EXPECT_EQ(Body("loop ld a,(ix+5)", "7.43"), loop);
    EXPECT_EQ(XasCodec::DecodeBody(loop, "7.43"), "loop    LD    A,(IX+5)");
    EXPECT_EQ(XasCodec::DecodeBody(loop, "9.10"), "LOOP          LD      A,(IX+5)");   // 64 columns, capitals
    EXPECT_EQ(Body("LD (IX-2),5", "7.43"), (std::vector<uint8_t>{0xAC, '(', 0xD3, '-', '2', ')', '5'}));
    // commas only in DB / DW / DS (and PUSH / POP from 5.05) lists; '.' is '#'
    EXPECT_EQ(Body("DB 1,2,3", "7.43"), Bytes("\xC8" "1,2,3"));
    EXPECT_EQ(Body("PUSH BC,DE,HL", "7.43"), (std::vector<uint8_t>{0x95, 0xD0, 0xD1, 0xD2}));
    EXPECT_EQ(Body("LD A,.FF", "7.43"), Bytes("\xAC\xDF#FF"));
    EXPECT_EQ(Body("NOP:NOP", "7.43"), Bytes("NOP:NOP"));
    // a comment follows the last item directly
    const std::vector<uint8_t> equ = Bytes("\xC6" "1; Define");
    EXPECT_EQ(Body("EQU 1; Define", "7.43"), equ);
    EXPECT_EQ(XasCodec::DecodeBody(equ, "7.43"), "        EQU   1; Define");
    EXPECT_EQ(XasCodec::DecodeBody(Bytes("\xC8" "1,2"), "4.18"), "        DEFB  1,2");
    // what XAS refuses: two text items in a row; 7.x drops the comma of ", " (so the items run together), 5.05 keeps it
    std::vector<uint8_t> body;
    std::string error;
    EXPECT_FALSE(XasCodec::EncodeBody("ld (lab),5", "7.43", body, error));
    EXPECT_FALSE(XasCodec::EncodeBody("db 1, 2", "7.43", body, error));
    EXPECT_EQ(Body("defb 1, 2", "5.05"), Bytes("\xC8" "1,2"));
}

TEST(XasCodec_Test, NewText)
{
    const codecs::XasCodec codec;
    const EncodeResult encoded = codec.Encode(SourceDocument::FromText("start ld hl,#4000\n        RET"), {});
    ASSERT_TRUE(encoded.ok);
    std::vector<uint8_t> expected = Bytes("by Max Petrov & Creator v7.44");   // XAS 7.447's template
    expected.insert(expected.end(), {0x23, 0xC0, 0, 0, 0, 0, 0x01});
    for (const uint8_t b : Bytes("START\xAC\xD2#4000\r\xB6\r"))
        expected.push_back(b);
    expected.push_back(0);
    EXPECT_EQ(encoded.bytes, expected);
    const DecodeResult decoded = codec.Decode(encoded.bytes, {});
    EXPECT_EQ(decoded.document.subversion, "7.43");
    EXPECT_EQ(decoded.document.Text(), "start   LD    HL,#4000\n        RET");
}

TEST(XasCodec_Test, Detection)
{
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample);
        const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
        ASSERT_NE(detected.chosen, nullptr) << sample.stem << ": " << detected.reason;
        EXPECT_EQ(detected.chosen->Info().id, "xas") << sample.stem;
    }
}
