#include "loaders/rzx/rzxsnapshot.h"

#include "loaders/snapshot/szx/szxformat.h"

namespace rzx
{
    namespace
    {
        constexpr size_t kZ80V1HeaderSize = 30;
        constexpr size_t kSna48Size = 49179;
        constexpr size_t kSna128Size = 131103;
        constexpr size_t kSna128BigSize = 147487;  ///< 128K SNA with the duplicated paged bank

        bool FromZ80Code(uint8_t code, bool version3, bool modified, SnapshotMachine& machine, std::string& error)
        {
            // Hardware byte 34 (Z80 format, v2 / v3 tables differ for 3..6)
            switch (code)
            {
                case 0:
                case 1:
                    machine = {MM_SPECTRUM48, 48, false, {}};
                    return true;
                case 3:
                    if (version3)
                    {
                        machine = {MM_SPECTRUM48, 48, false, {}};  // 48K + MGT
                        return true;
                    }
                    [[fallthrough]];
                case 4:
                case 5:
                case 6:
                    if (!version3 && code > 4)
                        break;
                    machine = modified ? SnapshotMachine{MM_PLUS2, 128, false, {}}
                                       : SnapshotMachine{MM_SPECTRUM128, 128, false, {}};
                    return true;
                case 7:
                case 8:
                    machine = modified ? SnapshotMachine{MM_PLUS2A, 128, false, {}}
                                       : SnapshotMachine{MM_PLUS3, 128, false, {}};
                    return true;
                case 9:
                    machine = {MM_PENTAGON, 128, false, {}};
                    return true;
                case 10:
                    machine = {MM_SCORP, 256, false, {}};
                    return true;
                case 12:
                    machine = {MM_PLUS2, 128, false, {}};
                    return true;
                case 13:
                    machine = {MM_PLUS2A, 128, false, {}};
                    return true;
                default:
                    break;
            }
            error = "Z80 snapshot hardware type " + std::to_string(code) + " is not supported for RZX playback";
            return false;
        }
    }  // namespace

    bool DetectSnapshotMachine(const std::string& extension, const std::vector<uint8_t>& data,
                               SnapshotMachine& machine, std::string& error)
    {
        machine = SnapshotMachine{};
        if (extension == "z80")
        {
            if (data.size() < kZ80V1HeaderSize)
            {
                error = "Z80 snapshot shorter than its header";
                return false;
            }
            const uint16_t pc = static_cast<uint16_t>(data[6] | (data[7] << 8));
            if (pc != 0)
            {
                machine = {MM_SPECTRUM48, 48, false, "Z80 v1, 48K"};
                return true;
            }
            if (data.size() < kZ80V1HeaderSize + 2 + 8)
            {
                error = "Z80 snapshot: truncated extended header";
                return false;
            }
            const uint16_t extra = static_cast<uint16_t>(data[30] | (data[31] << 8));
            const bool version3 = extra != 23;
            if (extra != 23 && extra != 54 && extra != 55)
            {
                error = "Z80 snapshot: unknown extended header length " + std::to_string(extra);
                return false;
            }
            const bool modified = (data[37] & 0x80) != 0;
            if (!FromZ80Code(data[34], version3, modified, machine, error))
                return false;
            machine.description = std::string("Z80 ") + (version3 ? "v3" : "v2") + ", hardware " +
                                  std::to_string(data[34]) + (modified ? " (modified)" : "");
            return true;
        }

        if (extension == "sna")
        {
            if (data.size() == kSna48Size)
            {
                machine = {MM_SPECTRUM48, 48, false, "SNA 48K"};
                return true;
            }
            if (data.size() == kSna128Size || data.size() == kSna128BigSize)
            {
                machine = {MM_SPECTRUM128, 128, true, "SNA 128K"};
                return true;
            }
            error = "SNA snapshot of " + std::to_string(data.size()) + " bytes (48K or 128K sizes expected)";
            return false;
        }

        if (extension == "szx" || extension == "zxs")
        {
            // Header: "ZXST", major, minor, machine id, flags
            if (data.size() < 8 || data[0] != 'Z' || data[1] != 'X' || data[2] != 'S' || data[3] != 'T')
            {
                error = "SZX snapshot without its 'ZXST' header";
                return false;
            }
            szx::Machine szxMachine;
            if (!szx::MachineFor(data[6], szxMachine, error))
                return false;
            machine = {szxMachine.model, szxMachine.ramKb, false, "SZX, machine id " + std::to_string(data[6])};
            return true;
        }

        error = "snapshot type '" + extension + "' is not supported (sna, z80, szx)";
        return false;
    }

    bool MachineMatches(const SnapshotMachine& machine, MEM_MODEL model, uint32_t ramKb)
    {
        if (machine.family128)
        {
            switch (model)
            {
                case MM_SPECTRUM128:
                case MM_PLUS2:
                case MM_PLUS2A:
                case MM_PLUS3:
                case MM_PENTAGON:
                    return true;
                default:
                    return false;
            }
        }
        if (machine.model != model)
            return false;
        // RAM matters where the model has variants (Pentagon 128 / 512 / 1024)
        return machine.model != MM_PENTAGON || machine.ramKb == ramKb;
    }
}  // namespace rzx
