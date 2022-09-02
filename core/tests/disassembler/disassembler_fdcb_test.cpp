#include "disassembler_fdcb_test.h"

#include <vector>
#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Disassembler_FDCB_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _disasm = new Z80DisassemblerCUT();
}

void Disassembler_FDCB_Test::TearDown()
{
    if (_disasm != nullptr)
    {
        delete _disasm;
        _disasm = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(Disassembler_FDCB_Test, TestBitOperations)
{
    static char message[256];
    static constexpr const char* ERROR_OPERANDS = "<FAIL>";

    std::vector<std::vector<uint8_t>> testData =
    {
        { 0xFD, 0xCB, 0x02, 0x5E },     // bit 3,(iy+2)
        { 0xFD, 0xCB, 0x01, 0x6E },     // bit 5,(iy+1)
        { 0xFD, 0xCB, 0x01, 0xAE },     // res 5,(iy+#01)
    };

    std::vector<std::string> referenceValues =
    {
        "bit 3,(iy+#02)",
        "bit 5,(iy+#01)",
        "res 5,(iy+#01)"
    };

    int i = 0;
    for (auto& cmd : testData)
    {
        uint8_t* command = cmd.data();
        std::string hexCommand = DumpHelper::HexDumpBuffer(command, cmd.size());
        std::string referenceResult = referenceValues[i];

        // Probe method under test and get result
        std::string result = _disasm->disassembleSingleCommand(command, cmd.size());

        if (result != referenceResult)
        {
            snprintf(message, sizeof message, "Iteration %d. Data '%s'. Expected '%s', found '%s'", i,
                     hexCommand.c_str(), referenceResult.c_str(), result.c_str());

            EXPECT_EQ(referenceResult, result) << message << std::endl;
        }
        else
        {
#ifdef _DEBUG
            std::cout << std::left << std::setw(16) << hexCommand << std::setw(0) << result << std::endl;
#endif // _DEBUG
        }

        i++;
    }
}