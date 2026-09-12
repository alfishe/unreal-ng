#pragma once
//
// ttd_reader.h — Binary parser for .ttd dump files (schema v2).
//
// Parses the header, page store, and checkpoint table into in-memory structs.
// No decompression here — just index slot payloads for the materializer.
//

#include <QtGlobal>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <vector>
#include <cstdint>
#include "ttd_format.h"

namespace ttd {

// ---------------------------------------------------------------------------
// CPU state — matches the C++ TTDCpuState POD struct (48 bytes on disk).
// The struct has natural alignment: 3 explicit padding bytes at offsets
// 31, 35, and 43 (matching the Python reader's skip calls).
// ---------------------------------------------------------------------------
struct CpuState {
    uint16_t pc, sp;
    uint16_t af, bc, de, hl;
    uint16_t ix, iy;
    uint16_t alt_af, alt_bc, alt_de, alt_hl;
    uint8_t  i, r_low, r_hi, iff1, iff2, im, halted;
    uint8_t  _pad0;       // offset 31 — aligns memptr to 2 bytes
    uint16_t memptr;
    uint8_t  q;
    uint8_t  _pad1;       // offset 35 — aligns eipos to 2 bytes
    uint16_t eipos, haltpos;
    uint8_t  nmi_in_progress, int_pending, int_gate;
    uint8_t  _pad2;       // offset 43 — aligns halt_cycle to 4 bytes
    uint32_t halt_cycle;
};
static_assert(sizeof(CpuState) == 48, "CpuState must be 48 bytes");

// ---------------------------------------------------------------------------
// Chipset state — matches the C++ TTDChipsetState POD struct.
// Standard Spectrum 128K ports only: extended / model-specific latches moved
// out to TTDPeripheralRegistry serializers and arrive as peripheral blobs.
// All fields after the two u64s are u8/byte arrays, so no alignment padding.
// ---------------------------------------------------------------------------
struct ChipsetState {
    uint64_t t_states;
    uint64_t frame_counter;
    // Standard Spectrum 128K port latches
    uint8_t  p7ffd;
    uint8_t  pfe;
    uint8_t  peff7;
    uint8_t  pbffd;
    uint8_t  pfffd;
    uint8_t  pff77;
    uint8_t  border_attr;
    uint8_t  flags;
    // FDC state (common to Beta Disk models)
    uint8_t  wd_shadow[4];
    // Video / palette
    uint8_t  comp_pal[16];
    uint8_t  ulaplus_mode;
    uint8_t  ulaplus_reg;
    uint8_t  ulaplus_cram[64];
    uint8_t  reserved[10];
};
// sizeof = 8+8+8+4+16+1+1+64+10 = 120

// ---------------------------------------------------------------------------
// Page store slot
// ---------------------------------------------------------------------------
struct PageSlot {
    uint8_t  encoding;     // kEncodingFull / kEncodingXorPrev / kEncodingZero
    uint32_t refcount;     // informational
    uint32_t prev_slot;    // compact index for XorPrev
    uint32_t crc32c;       // writer stores 0; reader recomputes
    uint32_t payload_size;
    const uint8_t* payload; // pointer into mapped/loaded file data
};

// ---------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------
struct Checkpoint {
    uint64_t frame;
    uint64_t global_t;
    uint8_t  frame_kind;      // kFrameKindKeyFrame / kFrameKindDeltaFrame
    uint64_t keyframe_anchor;
    CpuState cpu;
    ChipsetState chipset;
    std::vector<uint32_t> ram_sub_slots;  // 4 * model_ram_pages entries
    // Peripheral blobs (skipped during parse — we only need RAM + chipset)
    // All peripheral state, keyed by PeripheralId. Every device goes through
    // TTDPeripheralRegistry — the core four and any model-specific ones (e.g.
    // Scorpion ProfROM plane/page) — so a device absent on this machine has no
    // entry rather than an empty fixed slot.
    QMap<uint8_t, QByteArray> peripheral_blobs;

    bool isKeyframe() const { return frame_kind == kFrameKindKeyFrame; }
};

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------
struct TtdHeader {
    uint16_t schema_version;
    uint16_t flags;
    uint8_t  model_id;
    uint8_t  model_ram_pages;
    uint16_t cpu_state_size;
    uint16_t chipset_state_size;
    uint64_t rom_signature;      // 0 = unknown; else fingerprint of the ROM set
    uint64_t captured_at_unix_ms;
    QString  emulator_id;
    uint8_t  session_state;
    uint64_t session_start_frame;
    uint64_t session_end_frame;
    uint32_t page_store_count;
    uint32_t checkpoint_count;
};

// ---------------------------------------------------------------------------
// Fully-parsed .ttd dump
// ---------------------------------------------------------------------------
class TtdDump {
public:
    TtdDump() = default;

    /// Load and parse a .ttd file. Returns false on error (errorMsg set).
    bool load(const QString& path);

    const TtdHeader& header() const { return _header; }
    const std::vector<Checkpoint>& checkpoints() const { return _checkpoints; }
    const PageSlot& slot(uint32_t index) const { return _slots.at(index); }
    const std::vector<PageSlot>& pageSlots() const { return _slots; }

    const QString& error() const { return _error; }

    /// Find the checkpoint at or before the given frame (binary search).
    /// Returns nullptr if frame is out of range.
    const Checkpoint* checkpointAtFrame(uint64_t frame) const;

private:
    QByteArray _fileData;
    TtdHeader _header;
    std::vector<PageSlot> _slots;
    std::vector<Checkpoint> _checkpoints;
    QString _error;
};

} // namespace ttd
