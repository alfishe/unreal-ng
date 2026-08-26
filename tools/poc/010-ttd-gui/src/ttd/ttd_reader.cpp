//
// ttd_reader.cpp — Binary parser for .ttd dump files.
//
// Ports the Python reader from tools/verification/ttd-analyzer/src/ttd_format.py
// to C++. Reads the file into a QByteArray, then walks it with a sequential
// little-endian byte cursor.
//

#include "ttd_reader.h"

#include <QFile>
#include <QFileInfo>
#include <cstring>

namespace ttd {

// ---------------------------------------------------------------------------
// Sequential little-endian byte reader (mirrors Python's _Reader class)
// ---------------------------------------------------------------------------
class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size)
        : _data(data), _size(size), _pos(0) {}

    size_t pos() const { return _pos; }
    size_t remaining() const { return _size - _pos; }

    void need(size_t n) {
        if (_pos + n > _size)
            throw std::runtime_error(
                "unexpected end-of-file at offset " + std::to_string(_pos) +
                ": need " + std::to_string(n) + " more bytes");
    }

    uint8_t u8() {
        need(1);
        return _data[_pos++];
    }

    uint16_t u16() {
        need(2);
        uint16_t v;
        std::memcpy(&v, _data + _pos, 2);
        _pos += 2;
        return v;
    }

    uint32_t u32() {
        need(4);
        uint32_t v;
        std::memcpy(&v, _data + _pos, 4);
        _pos += 4;
        return v;
    }

    uint64_t u64() {
        need(8);
        uint64_t v;
        std::memcpy(&v, _data + _pos, 8);
        _pos += 8;
        return v;
    }

    const uint8_t* take(size_t n) {
        need(n);
        const uint8_t* ptr = _data + _pos;
        _pos += n;
        return ptr;
    }

    void skip(size_t n) {
        need(n);
        _pos += n;
    }

private:
    const uint8_t* _data;
    size_t _size;
    size_t _pos;
};

// ---------------------------------------------------------------------------
// Parsers (one per struct — matching the Python parse_* functions)
// ---------------------------------------------------------------------------

static TtdHeader parseHeader(ByteReader& r) {
    TtdHeader h{};

    // Magic
    const uint8_t* magic = r.take(4);
    if (std::memcmp(magic, kMagic, 4) != 0)
        throw std::runtime_error("bad magic — not a .ttd file");

    h.schema_version = r.u16();
    if (h.schema_version < kMinSupportedSchema || h.schema_version > kMaxSupportedSchema)
        throw std::runtime_error(
            "unsupported schema version " + std::to_string(h.schema_version) +
            " (supported: v" + std::to_string(kMinSupportedSchema) +
            "-v" + std::to_string(kMaxSupportedSchema) + ")");

    h.flags = r.u16();
    if ((h.flags & kFlagsLittleEndian) == 0)
        throw std::runtime_error("file is big-endian; only LE supported");

    h.model_id = r.u8();
    h.model_ram_pages = r.u8();
    h.cpu_state_size = r.u16();
    h.chipset_state_size = r.u16();
    h.captured_at_unix_ms = r.u64();

    uint8_t emu_id_len = r.u8();
    const uint8_t* emu_id = r.take(emu_id_len);
    h.emulator_id = QString::fromUtf8(reinterpret_cast<const char*>(emu_id), emu_id_len);

    h.session_state = r.u8();
    h.session_start_frame = r.u64();
    h.session_end_frame = r.u64();
    h.page_store_count = r.u32();
    h.checkpoint_count = r.u32();
    r.skip(8);  // reserved

    return h;
}

static CpuState parseCpu(ByteReader& r) {
    CpuState c{};
    c.pc = r.u16();   c.sp = r.u16();
    c.af = r.u16();   c.bc = r.u16();  c.de = r.u16();  c.hl = r.u16();
    c.ix = r.u16();   c.iy = r.u16();
    c.alt_af = r.u16(); c.alt_bc = r.u16(); c.alt_de = r.u16(); c.alt_hl = r.u16();
    c.i = r.u8();     c.r_low = r.u8();  c.r_hi = r.u8();
    c.iff1 = r.u8();  c.iff2 = r.u8();   c.im = r.u8();   c.halted = r.u8();
    r.u8();  // padding byte at offset 31
    c.memptr = r.u16();
    c.q = r.u8();
    r.u8();  // padding byte at offset 35
    c.eipos = r.u16(); c.haltpos = r.u16();
    c.nmi_in_progress = r.u8();
    c.int_pending = r.u8();
    c.int_gate = r.u8();
    r.u8();  // padding byte at offset 43
    c.halt_cycle = r.u32();
    return c;
}

static ChipsetState parseChipset(ByteReader& r) {
    ChipsetState cs{};
    cs.t_states = r.u64();
    cs.frame_counter = r.u64();
    cs.p7ffd = r.u8();
    cs.pfe = r.u8();
    cs.peff7 = r.u8();
    cs.pxxxx = r.u8();
    cs.pbffd = r.u8();
    cs.pfffd = r.u8();
    cs.pdffd = r.u8();
    cs.pfdfd = r.u8();
    cs.p1ffd = r.u8();
    cs.pff77 = r.u8();
    cs.border_attr = r.u8();
    cs.flags = r.u8();
    cs.p7efd = r.u8();
    cs.p78fd = r.u8();
    cs.p7afd = r.u8();
    cs.p7cfd = r.u8();
    cs.gmx_config = r.u8();
    cs.gmx_magic_shift = r.u8();
    cs.p00 = r.u8();
    cs.p80fd = r.u8();
    cs.afe = r.u8();
    cs.afb = r.u8();
    cs.aff77 = r.u8();
    cs.active_ay = r.u8();
    cs.pbd = r.u8();
    cs.pbe = r.u8();
    cs.pbf = r.u8();
    cs.pffba = r.u8();
    cs.p7fba = r.u8();
    cs.p0f = r.u8();
    cs.p1f = r.u8();
    cs.p4f = r.u8();
    cs.p5f = r.u8();
    cs.plsy256 = r.u8();
    const uint8_t* wd = r.take(4);
    std::memcpy(cs.wd_shadow, wd, 4);
    const uint8_t* pal = r.take(16);
    std::memcpy(cs.comp_pal, pal, 16);
    cs.ulaplus_mode = r.u8();
    cs.ulaplus_reg = r.u8();
    const uint8_t* cram = r.take(64);
    std::memcpy(cs.ulaplus_cram, cram, 64);
    const uint8_t* pfff = r.take(32);
    std::memcpy(cs.pfff7, pfff, 32);
    return cs;
}

static QByteArray parseBlob(ByteReader& r) {
    uint32_t size = r.u32();
    if (size > (1u << 20))
        throw std::runtime_error("implausible blob size " + std::to_string(size));
    const uint8_t* data = r.take(size);
    return QByteArray(reinterpret_cast<const char*>(data), static_cast<int>(size));
}

static PageSlot parseSlot(ByteReader& r, uint32_t index) {
    PageSlot s{};
    s.encoding = r.u8();
    if (s.encoding > kEncodingZero)
        throw std::runtime_error("slot " + std::to_string(index) + ": unknown encoding " + std::to_string(s.encoding));
    s.refcount = r.u32();
    s.prev_slot = r.u32();
    s.crc32c = r.u32();
    s.payload_size = r.u32();
    if (s.payload_size > (1u << 24))
        throw std::runtime_error("slot " + std::to_string(index) + ": implausible payload_size " + std::to_string(s.payload_size));
    s.payload = r.take(s.payload_size);

    // Cross-field validation
    if (s.encoding == kEncodingXorPrev) {
        if (s.prev_slot == kNeverTouchedSlot)
            throw std::runtime_error("slot " + std::to_string(index) + ": XorPrev but prev_slot is sentinel");
        if (s.payload_size == 0)
            throw std::runtime_error("slot " + std::to_string(index) + ": XorPrev but payload empty");
    } else if (s.encoding == kEncodingFull) {
        if (s.payload_size == 0)
            throw std::runtime_error("slot " + std::to_string(index) + ": Full but payload empty");
    } else if (s.encoding == kEncodingZero) {
        if (s.payload_size != 0)
            throw std::runtime_error("slot " + std::to_string(index) + ": Zero but payload non-empty");
    }

    return s;
}

static Checkpoint parseCheckpoint(ByteReader& r, uint8_t model_ram_pages, uint32_t index) {
    Checkpoint cp{};
    cp.frame = r.u64();
    cp.global_t = r.u64();
    cp.frame_kind = r.u8();
    if (cp.frame_kind > kFrameKindDeltaFrame)
        throw std::runtime_error("checkpoint " + std::to_string(index) + ": unknown frame_kind " + std::to_string(cp.frame_kind));
    cp.keyframe_anchor = r.u64();
    cp.cpu = parseCpu(r);
    cp.chipset = parseChipset(r);

    uint32_t refs_count = static_cast<uint32_t>(model_ram_pages) * kSubPagesPerEmuPage;
    cp.ram_sub_slots.reserve(refs_count);
    for (uint32_t i = 0; i < refs_count; ++i)
        cp.ram_sub_slots.push_back(r.u32());

    cp.ay_blob = parseBlob(r);
    cp.fdc_blob = parseBlob(r);
    cp.tape_blob = parseBlob(r);
    cp.covox_blob = parseBlob(r);

    return cp;
}

// ---------------------------------------------------------------------------
// TtdDump implementation
// ---------------------------------------------------------------------------

bool TtdDump::load(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        _error = QStringLiteral("Cannot open: %1 (%2)").arg(path, file.errorString());
        return false;
    }
    _fileData = file.readAll();
    file.close();

    const uint8_t* data = reinterpret_cast<const uint8_t*>(_fileData.constData());
    const size_t size = static_cast<size_t>(_fileData.size());

    try {
        ByteReader r(data, size);
        _header = parseHeader(r);

        // Page store
        _slots.clear();
        _slots.reserve(_header.page_store_count);
        for (uint32_t i = 0; i < _header.page_store_count; ++i)
            _slots.push_back(parseSlot(r, i));

        // Checkpoints
        _checkpoints.clear();
        _checkpoints.reserve(_header.checkpoint_count);
        for (uint32_t i = 0; i < _header.checkpoint_count; ++i)
            _checkpoints.push_back(parseCheckpoint(r, _header.model_ram_pages, i));

    } catch (const std::exception& e) {
        _error = QString::fromUtf8(e.what());
        return false;
    }

    return true;
}

const Checkpoint* TtdDump::checkpointAtFrame(uint64_t frame) const {
    if (_checkpoints.empty())
        return nullptr;
    if (frame <= _checkpoints.front().frame)
        return &_checkpoints.front();
    if (frame >= _checkpoints.back().frame)
        return &_checkpoints.back();

    // Binary search: find the last checkpoint with frame <= target
    size_t lo = 0, hi = _checkpoints.size() - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        if (_checkpoints[mid].frame <= frame)
            lo = mid;
        else
            hi = mid - 1;
    }
    return &_checkpoints[lo];
}

} // namespace ttd
