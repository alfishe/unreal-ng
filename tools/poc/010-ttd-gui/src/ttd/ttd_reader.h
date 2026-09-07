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
// All fields after the two u64s are u8/byte arrays, so no alignment padding.
// ---------------------------------------------------------------------------
struct ChipsetState {
    uint64_t t_states;
    uint64_t frame_counter;
    uint8_t  p7ffd;
    uint8_t  pfe;
    uint8_t  peff7;
    uint8_t  pxxxx;
    uint8_t  pbffd;
    uint8_t  pfffd;
    uint8_t  pdffd;
    uint8_t  pfdfd;
    uint8_t  p1ffd;
    uint8_t  pff77;
    uint8_t  border_attr;
    uint8_t  flags;
    // Extended port latches
    uint8_t  p7efd;
    uint8_t  p78fd;
    uint8_t  p7afd;
    uint8_t  p7cfd;
    uint8_t  gmx_config;
    uint8_t  gmx_magic_shift;
    uint8_t  p00;
    uint8_t  p80fd;
    uint8_t  afe;
    uint8_t  afb;
    uint8_t  aff77;
    uint8_t  active_ay;
    uint8_t  pbd;
    uint8_t  pbe;
    uint8_t  pbf;
    uint8_t  pffba;
    uint8_t  p7fba;
    uint8_t  p0f;
    uint8_t  p1f;
    uint8_t  p4f;
    uint8_t  p5f;
    uint8_t  plsy256;
    uint8_t  wd_shadow[4];
    uint8_t  comp_pal[16];
    uint8_t  ulaplus_mode;
    uint8_t  ulaplus_reg;
    uint8_t  ulaplus_cram[64];
    uint8_t  pfff7[32];
};
// sizeof = 8+8+34+4+16+1+1+64+32 = 168

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
    QByteArray ay_blob;
    QByteArray fdc_blob;
    QByteArray tape_blob;
    QByteArray covox_blob;

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
