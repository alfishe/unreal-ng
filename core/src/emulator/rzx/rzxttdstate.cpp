#include "emulator/rzx/rzxttdstate.h"

#include <cstring>

#include "emulator/rzx/rzxsession.h"

namespace rzx
{
    namespace
    {
        template <typename T>
        void Put(uint8_t*& p, T value)
        {
            std::memcpy(p, &value, sizeof(T));
            p += sizeof(T);
        }

        template <typename T>
        T Get(const uint8_t*& p)
        {
            T value;
            std::memcpy(&value, p, sizeof(T));
            p += sizeof(T);
            return value;
        }
    }  // namespace

    void RzxTtdState::TTDSaveState(uint8_t* dst) const
    {
        if (!dst)
            return;
        std::memset(dst, 0, kSize);
        RzxPlayer::SavedState s;
        uint64_t fingerprint = 0;
        if (!_session.SaveTtdState(s, fingerprint))
            return;   // version 0: nothing played
        uint8_t* p = dst;
        Put<uint8_t>(p, kVersion);
        Put<uint8_t>(p, static_cast<uint8_t>(s.state));
        Put<uint8_t>(p, s.lastIn);
        Put<uint8_t>(p, static_cast<uint8_t>(s.firstDesync.kind));
        Put<uint64_t>(p, fingerprint);
        Put<uint64_t>(p, s.framesDone);
        Put<uint32_t>(p, s.fetches);
        Put<uint32_t>(p, s.inPos);
        Put<uint64_t>(p, s.interrupts);
        Put<uint64_t>(p, s.desyncs);
        Put<uint64_t>(p, s.snapshotsApplied);
        Put<int32_t>(p, s.drift);
        Put<int32_t>(p, s.maxDrift);
        Put<uint32_t>(p, s.firstDesync.block);
        Put<uint64_t>(p, s.firstDesync.frame);
        Put<uint32_t>(p, s.firstDesync.expected);
        Put<uint32_t>(p, s.firstDesync.actual);
        Put<uint16_t>(p, s.firstDesync.pc);
        Put<uint16_t>(p, s.firstDesync.port);
    }

    void RzxTtdState::TTDLoadState(const uint8_t* src)
    {
        if (!src || src[0] != kVersion)
            return;
        const uint8_t* p = src + 1;
        RzxPlayer::SavedState s;
        s.state = static_cast<PlayerState>(Get<uint8_t>(p));
        s.lastIn = Get<uint8_t>(p);
        s.firstDesync.kind = static_cast<DesyncKind>(Get<uint8_t>(p));
        const uint64_t fingerprint = Get<uint64_t>(p);
        s.framesDone = Get<uint64_t>(p);
        s.fetches = Get<uint32_t>(p);
        s.inPos = Get<uint32_t>(p);
        s.interrupts = Get<uint64_t>(p);
        s.desyncs = Get<uint64_t>(p);
        s.snapshotsApplied = Get<uint64_t>(p);
        s.drift = Get<int32_t>(p);
        s.maxDrift = Get<int32_t>(p);
        s.firstDesync.block = Get<uint32_t>(p);
        s.firstDesync.frame = Get<uint64_t>(p);
        s.firstDesync.expected = Get<uint32_t>(p);
        s.firstDesync.actual = Get<uint32_t>(p);
        s.firstDesync.pc = Get<uint16_t>(p);
        s.firstDesync.port = Get<uint16_t>(p);
        _session.RestoreTtdState(s, fingerprint);
    }
}  // namespace rzx
