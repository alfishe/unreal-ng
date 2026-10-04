#pragma once

/// @file rzxttdstate.h
/// @brief The RZX playback as a time-travel device (PeripheralId::RzxPlayback,
/// RZX requirements RZ-F19): while TTD records an RZX playback, every
/// checkpoint keeps the player's position (frame, fetches, IN position) and
/// its counters, so a seek back lands inside the playback and it plays on
/// from there - the recorded interrupts and IN values come out as they did.
/// The recording itself is the medium: a restore needs the same recording
/// loaded in the session (checked by its fingerprint).
///
/// Blob (84 bytes, little endian): u8 version 1, u8 state, u8 last IN value,
/// u8 first desync kind, u64 recording fingerprint, u64 frames done,
/// u32 fetches, u32 IN position, u64 interrupts, u64 desyncs, u64 snapshots
/// applied, i32 drift, i32 max drift, then the first desync: u32 block,
/// u64 frame, u32 expected, u32 actual, u16 PC, u16 port.

#include "debugger/ttd/ttdserializable.h"

namespace rzx
{
    class RzxSession;

    class RzxTtdState : public ttd::TTDSerializable
    {
    public:
        static constexpr uint8_t kVersion = 1;
        static constexpr size_t kSize = 84;

        explicit RzxTtdState(RzxSession& session) : _session(session) {}

        size_t TTDStateSize() const override
        {
            return kSize;
        }
        void TTDSaveState(uint8_t* dst) const override;
        void TTDLoadState(const uint8_t* src) override;
        std::string TTDDeviceName() const override
        {
            return "RzxPlayback";
        }
        ttd::PeripheralId TTDPeripheralId() const override
        {
            return ttd::PeripheralId::RzxPlayback;
        }

    private:
        RzxSession& _session;
    };
}  // namespace rzx
