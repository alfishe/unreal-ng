#include "ttdfileinfo.h"

#include <cstring>
#include <fstream>

#include "common/filehelper.h"
#include "emulator/config.h"
#include "ttddumpformat.h"
#include "ttdserializable.h"   // PeripheralId

namespace ttd
{

namespace
{

template <typename T>
bool Read(std::istream& in, T& value, const char* what, std::string& err)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in)
    {
        err = std::string("truncated file (") + what + ")";
        return false;
    }
    return true;
}

bool Skip(std::istream& in, uint64_t bytes, const char* what, std::string& err)
{
    in.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
    if (!in)
    {
        err = std::string("truncated file (") + what + ")";
        return false;
    }
    return true;
}

/// The device set of a file written before the header mask: walk the page
/// store's slot headers (payloads skipped) to the first checkpoint and read
/// its peripheral blob ids. The stream is positioned right after the header.
bool ScanPeripherals(std::istream& in, const TTDFileInfo& info, uint64_t& mask, std::string& err)
{
    mask = 0;
    if (info.checkpointCount == 0)
        return true;   // an empty session records no device
    for (uint32_t i = 0; i < info.pageStoreCount; ++i)
    {
        uint8_t encoding = 0;
        uint32_t refcount = 0, prevSlot = 0, crc = 0, payloadSize = 0;
        if (!Read(in, encoding, "page slot", err) || !Read(in, refcount, "page slot", err) ||
            !Read(in, prevSlot, "page slot", err) || !Read(in, crc, "page slot", err) ||
            !Read(in, payloadSize, "page slot", err))
            return false;
        if (payloadSize > dump::kSubPageSize * 2)
        {
            err = "page slot " + std::to_string(i) + ": implausible payload size " + std::to_string(payloadSize);
            return false;
        }
        if (!Skip(in, payloadSize, "page slot payload", err))
            return false;
    }
    // First checkpoint: frame u64, globalT u64, frame kind u8, keyframe anchor
    // u64, CPU and chipset state, RAM page refs, then the peripheral blobs
    const uint64_t fixed = 8 + 8 + 1 + 8 + uint64_t(info.cpuStateSize) + info.chipsetStateSize;
    const uint64_t refs = uint64_t(info.machine.ramPageBound) * dump::kSubPagesPerEmuPage * sizeof(uint32_t);
    if (!Skip(in, fixed + refs, "first checkpoint", err))
        return false;
    uint16_t blobCount = 0;
    if (!Read(in, blobCount, "peripheral blob count", err))
        return false;
    if (blobCount > dump::kMaxPeripheralBlobsPerCheckpoint)
    {
        err = "implausible peripheral blob count " + std::to_string(blobCount);
        return false;
    }
    for (uint16_t b = 0; b < blobCount; ++b)
    {
        uint8_t id = 0;
        uint32_t size = 0;
        if (!Read(in, id, "peripheral blob", err) || !Read(in, size, "peripheral blob", err))
            return false;
        if (size > dump::kMaxPeripheralBlobBytes)
        {
            err = "implausible peripheral blob size " + std::to_string(size);
            return false;
        }
        if (id < 64)
            mask |= uint64_t(1) << id;
        if (!Skip(in, size, "peripheral blob", err))
            return false;
    }
    return true;
}

}  // namespace

std::string PeripheralIdName(uint8_t id)
{
    switch (static_cast<PeripheralId>(id))
    {
        case PeripheralId::TurboSound: return "turbosound";
        case PeripheralId::BetaDisk: return "betadisk";
        case PeripheralId::Tape: return "tape";
        case PeripheralId::Covox: return "covox";
        case PeripheralId::TSFM: return "tsfm";
        case PeripheralId::GeneralSound: return "gs";
        case PeripheralId::ScorpionProfROM: return "scorpion-profrom";
        case PeripheralId::KempstonMouse: return "kempston-mouse";
        case PeripheralId::AtmPaging: return "atm-paging";
        case PeripheralId::ProfiPaging: return "profi-paging";
        case PeripheralId::MoonSound: return "moonsound";
        case PeripheralId::GeneralSoundLightweight: return "gs-lw";
        case PeripheralId::NeoGS: return "neogs";
        case PeripheralId::Plus3Paging: return "plus3-paging";
        case PeripheralId::Upd765: return "upd765";
        case PeripheralId::EvoSdCard: return "evo-sdcard";
        case PeripheralId::TsConfPaging: return "tsconf";
        case PeripheralId::AtaChannel: return "ata";
        case PeripheralId::Ds12887: return "ds12887";
        case PeripheralId::EvoPs2: return "evo-ps2";
        case PeripheralId::ZxNetUsb: return "zxnetusb";
        case PeripheralId::EvoTurboCache: return "evo-turbo-cache";
        case PeripheralId::Count: break;
    }
    return "id" + std::to_string(id);
}

GSTypeKind GeneralSoundOf(uint64_t peripheralMask)
{
    auto has = [peripheralMask](PeripheralId id) { return (peripheralMask >> static_cast<uint8_t>(id)) & 1u; };
    if (has(PeripheralId::GeneralSound))
        return GSTypeKind::Z80;
    if (has(PeripheralId::GeneralSoundLightweight))
        return GSTypeKind::LW;
    if (has(PeripheralId::NeoGS))
        return GSTypeKind::NGS;
    return GSTypeKind::NONE;
}

const char* GeneralSoundName(GSTypeKind kind)
{
    switch (kind)
    {
        case GSTypeKind::NONE: return "none";
        case GSTypeKind::Z80: return "z80";
        case GSTypeKind::BASS: return "bass";
        case GSTypeKind::LW: return "lw";
        case GSTypeKind::NGS: return "ngs";
    }
    return "none";
}

void DescribeRecordedMachine(TTDRecordedMachine& machine)
{
    machine.model.clear();
    if (machine.modelId < N_MM_MODELS)
        if (const TMemModel* m = Config::FindModelByEnum(static_cast<MEM_MODEL>(machine.modelId)))
            machine.model = m->ShortName ? m->ShortName : "";
    machine.peripherals.clear();
    for (uint8_t id = 0; id < 64; ++id)
        if ((machine.peripheralMask >> id) & 1u)
            machine.peripherals.push_back(PeripheralIdName(id));
    machine.generalSound = GeneralSoundOf(machine.peripheralMask);
    auto has = [&](PeripheralId id) { return (machine.peripheralMask >> static_cast<uint8_t>(id)) & 1u; };
    machine.turboSound = has(PeripheralId::TSFM) ? "tsfm" : has(PeripheralId::TurboSound) ? "turbosound" : "none";
}

bool ReadTTDFileInfo(std::istream& in, TTDFileInfo& info, std::string& err)
{
    info = TTDFileInfo{};
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, dump::kMagic, 4) != 0)
    {
        err = "not a .ttd file (bad magic)";
        return false;
    }
    if (!Read(in, info.schemaVersion, "schema version", err))
        return false;
    if (info.schemaVersion != dump::kSchemaVersion)
    {
        err = "unsupported .ttd schema v" + std::to_string(info.schemaVersion) + " (this build reads v" +
              std::to_string(dump::kSchemaVersion) + ")";
        return false;
    }
    if (!Read(in, info.flags, "flags", err) || !Read(in, info.machine.modelId, "model", err) ||
        !Read(in, info.machine.ramPageBound, "RAM page bound", err) ||
        !Read(in, info.cpuStateSize, "CPU state size", err) ||
        !Read(in, info.chipsetStateSize, "chipset state size", err) ||
        !Read(in, info.machine.romSignature, "ROM signature", err) ||
        !Read(in, info.capturedAtUnixMs, "capture time", err))
        return false;
    uint8_t idLen = 0;
    if (!Read(in, idLen, "emulator id", err))
        return false;
    info.emulatorId.resize(idLen);
    if (idLen)
    {
        in.read(&info.emulatorId[0], idLen);
        if (!in)
        {
            err = "truncated file (emulator id)";
            return false;
        }
    }
    uint64_t reserved = 0;
    if (!Read(in, info.sessionState, "session state", err) || !Read(in, info.startFrame, "start frame", err) ||
        !Read(in, info.endFrame, "end frame", err) || !Read(in, info.pageStoreCount, "page store count", err) ||
        !Read(in, info.checkpointCount, "checkpoint count", err) || !Read(in, reserved, "peripheral mask", err))
        return false;

    const uint16_t f = info.flags;
    info.hasWriteJournal = f & dump::kFlagsHasWriteJournal;
    info.writeJournalComplete = f & dump::kFlagsWriteJournalComplete;
    info.hasCoverageIndex = f & dump::kFlagsHasCoverageIndex;
    info.hasBookmarks = f & dump::kFlagsHasBookmarks;
    info.hasInputJournal = f & dump::kFlagsHasInputJournal;
    info.hasExternalEvents = f & dump::kFlagsHasExternalEvents;
    info.hasPortJournals = f & dump::kFlagsHasPortJournals;
    info.topClockTime = f & dump::kFlagsTopClockTime;

    if (f & dump::kFlagsHasPeripheralMask)
    {
        info.machine.peripheralMask = reserved;
        info.peripheralsFromHeader = true;
    }
    else if (!ScanPeripherals(in, info, info.machine.peripheralMask, err))
        return false;
    DescribeRecordedMachine(info.machine);
    return true;
}

bool ReadTTDFileInfo(const std::string& path, TTDFileInfo& info, std::string& err)
{
    std::ifstream in(FileHelper::ToFsPath(path), std::ios::binary);
    if (!in)
    {
        err = "cannot open " + path;
        return false;
    }
    if (!ReadTTDFileInfo(in, info, err))
        return false;
    info.path = path;
    info.fileBytes = FileHelper::GetFileSize(path);
    return true;
}

}  // namespace ttd
