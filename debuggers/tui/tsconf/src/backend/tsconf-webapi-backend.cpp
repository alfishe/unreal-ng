// tsconf-webapi-backend.cpp - see tsconf-webapi-backend.h.
#include "backend/tsconf-webapi-backend.h"

#include "backend/json-mini.h"

namespace dbg {
namespace {

// The #xxAF register numbers (core/src/emulator/platforms/tsconf/tsconf.h)
enum : size_t {
    kVConfig = 0x00, kVPage = 0x01, kGXOffsL = 0x02, kGXOffsH = 0x03, kGYOffsL = 0x04, kGYOffsH = 0x05,
    kTsConfig = 0x06, kPalSel = 0x07, kBorder = 0x0F, kPage0 = 0x10, kFmAddr = 0x15, kTmPage = 0x16,
    kT0GPage = 0x17, kT1GPage = 0x18, kSgPage = 0x19, kSysConfig = 0x20,
    kMemConfig = 0x21, kHsInt = 0x22, kVsIntL = 0x23, kVsIntH = 0x24, kDmaLen = 0x26, kDmaCtrl = 0x27,
    kDmaNum = 0x28, kFddVirt = 0x29, kIntMask = 0x2A, kCacheConfig = 0x2B, kT0XOffsL = 0x40, kT1XOffsL = 0x44,
    kRegCount = 0x48
};

bool Bit(uint8_t value, int bit) { return ((value >> bit) & 1) != 0; }

uint32_t Number(const Json* object, const char* key) {
    int64_t v = 0;
    return object != nullptr && object->Get(key, &v) ? static_cast<uint32_t>(v) : 0;
}

}  // namespace

bool TsConfWebApiBackend::GetTsConf(TsConfState& out) {
    if (!Attach()) return false;
    if (Snapshots() == 0) GetRegs(0);
    if (tsAt_ != Snapshots()) {
        tsAt_ = Snapshots();
        tsValid_ = false;
        // 404 or "available": false on a machine without TS-Conf: the board is not painted
        const HttpReply reply = Api("GET", "/state/tsconf");
        const Json root = Json::Parse(reply.IsSuccess() ? reply.body : std::string(), nullptr);
        bool available = false;
        const Json* regs = root.Find("regs");
        if (root.Get("available", &available) && available && regs != nullptr && regs->IsArray() &&
            regs->Size() >= kRegCount) {
            uint8_t r[kRegCount];
            for (size_t i = 0; i < kRegCount; ++i) {
                int64_t v = 0;
                regs->At(i).AsInt(&v);
                r[i] = static_cast<uint8_t>(v);
            }
            TsConfState t;
            t.vconf = r[kVConfig];
            t.tsconf = r[kTsConfig];
            t.sysconf = r[kSysConfig];
            t.cacheconf = r[kCacheConfig];
            t.memconf = r[kMemConfig];
            t.palsel = r[kPalSel];
            t.fmaddr = r[kFmAddr];
            t.rres = t.vconf >> 6;
            t.vmode = t.vconf & 3;
            t.zclk = t.sysconf & 3;
            t.nogfx = Bit(t.vconf, 5);
            t.notsu = Bit(t.vconf, 4);
            t.gfxovr = Bit(t.vconf, 3);
            t.ftEn = Bit(t.vconf, 2);
            t.sEn = Bit(t.tsconf, 7);
            t.t1En = Bit(t.tsconf, 6);
            t.t0En = Bit(t.tsconf, 5);
            t.t1zEn = Bit(t.tsconf, 3);
            t.t0zEn = Bit(t.tsconf, 2);
            t.w0Ram = Bit(t.memconf, 3);
            t.w0MapN = Bit(t.memconf, 2);
            t.w0We = Bit(t.memconf, 1);
            t.rom128 = Bit(t.memconf, 0);
            t.vpage = r[kVPage];
            t.gXoffs = static_cast<uint16_t>(((r[kGXOffsH] & 1) << 8) | r[kGXOffsL]);
            t.gYoffs = static_cast<uint16_t>(((r[kGYOffsH] & 1) << 8) | r[kGYOffsL]);
            t.t0gpage = r[kT0GPage];
            t.t1gpage = r[kT1GPage];
            t.t0Xoffs = static_cast<uint16_t>(((r[kT0XOffsL + 1] & 1) << 8) | r[kT0XOffsL]);
            t.t0Yoffs = static_cast<uint16_t>(((r[kT0XOffsL + 3] & 1) << 8) | r[kT0XOffsL + 2]);
            t.t1Xoffs = static_cast<uint16_t>(((r[kT1XOffsL + 1] & 1) << 8) | r[kT1XOffsL]);
            t.t1Yoffs = static_cast<uint16_t>(((r[kT1XOffsL + 3] & 1) << 8) | r[kT1XOffsL + 2]);
            t.t1pal = static_cast<uint8_t>(t.palsel >> 6);
            t.t0pal = static_cast<uint8_t>((t.palsel >> 4) & 3);
            t.gpal = static_cast<uint8_t>(t.palsel & 0x0F);
            t.tmpage = r[kTmPage];
            t.border = r[kBorder];
            t.fddvirt = r[kFddVirt];
            t.fmEn = Bit(t.fmaddr, 4);
            t.fmAddr = static_cast<uint16_t>(t.fmaddr & 0x0F);
            for (int w = 0; w < 4; ++w) t.page[w] = r[kPage0 + static_cast<size_t>(w)];
            t.sgpage = r[kSgPage];
            const Json* dma = root.Find("dma");
            t.saddr = Number(dma, "programmed_source");
            t.daddr = Number(dma, "programmed_destination");
            t.dmanum = r[kDmaNum];
            t.dmalen = r[kDmaLen];
            bool busy = false;
            if (dma != nullptr) dma->Get("busy", &busy);
            t.dma.active = busy;
            t.dma.saddr = Number(dma, "source");
            t.dma.daddr = Number(dma, "destination");
            t.dma.num = t.dmanum;  // the report has no remaining counts
            t.dma.len = t.dmalen;
            const uint8_t ctrl = r[kDmaCtrl];
            t.dma.ctrl = ctrl;
            t.dma.rw = static_cast<uint8_t>(ctrl >> 7);
            t.dma.opt = static_cast<uint8_t>((ctrl >> 6) & 1);
            t.dma.sAlgn = static_cast<uint8_t>((ctrl >> 5) & 1);
            t.dma.dAlgn = static_cast<uint8_t>((ctrl >> 4) & 1);
            t.dma.asz = static_cast<uint8_t>((ctrl >> 3) & 1);
            t.dma.dev = static_cast<uint8_t>(ctrl & 7);
            t.hsint = r[kHsInt];
            t.vsint = static_cast<uint16_t>(((r[kVsIntH] & 1) << 8) | r[kVsIntL]);
            t.intFrame = Bit(r[kIntMask], 0);
            t.intLine = Bit(r[kIntMask], 1);
            t.intDma = Bit(r[kIntMask], 2);
            ts_ = t;
            tsValid_ = true;
        }
    }
    if (tsValid_) out = ts_;
    return tsValid_;
}

std::vector<PcHistEntry> TsConfWebApiBackend::GetPcHistory(int) {
    if (!Attach()) return {};
    if (Snapshots() == 0) GetRegs(0);
    if (histAt_ != Snapshots()) {
        histAt_ = Snapshots();
        hist_.clear();
        // Newest first; the first read starts the recording
        const HttpReply reply = Api("GET", "/debug/pchist?depth=32");
        const Json root = Json::Parse(reply.IsSuccess() ? reply.body : std::string(), nullptr);
        const Json* entries = root.Find("entries");
        if (entries != nullptr && entries->IsArray()) {
            for (size_t i = 0; i < entries->Size(); ++i) {
                const Json& e = entries->At(i);
                hist_.push_back(PcHistEntry{static_cast<uint8_t>(Number(&e, "page")),
                                            static_cast<uint16_t>(Number(&e, "address"))});
            }
        }
    }
    return hist_;
}

}  // namespace dbg
