/// @file ttdcontrollerreplaycheck.cpp
/// @brief TimeTravelController::QueryReplayCheck (docs/inprogress/2026-11-05-ttd-replay-check/README.md): frame N's
/// replay against checkpoint N + 1, the state the live machine had when it got there.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "debugger/ttd/engine/ttdpiecestore.h"
#include "debugger/ttd/ttdreplaycheck.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

namespace ttd
{

namespace
{

/// A named field of a POD state, compared and shown on its own
struct Field
{
    const char* name;
    size_t offset;
    size_t size;
};

#define TTD_FIELD(type, member) Field{#member, offsetof(type, member), sizeof(type::member)}

const Field kCpuFields[] = {
    TTD_FIELD(TTDCpuState, pc),          TTD_FIELD(TTDCpuState, sp),
    TTD_FIELD(TTDCpuState, af),          TTD_FIELD(TTDCpuState, bc),
    TTD_FIELD(TTDCpuState, de),          TTD_FIELD(TTDCpuState, hl),
    TTD_FIELD(TTDCpuState, ix),          TTD_FIELD(TTDCpuState, iy),
    TTD_FIELD(TTDCpuState, alt_af),      TTD_FIELD(TTDCpuState, alt_bc),
    TTD_FIELD(TTDCpuState, alt_de),      TTD_FIELD(TTDCpuState, alt_hl),
    TTD_FIELD(TTDCpuState, i),           TTD_FIELD(TTDCpuState, r_low),
    TTD_FIELD(TTDCpuState, r_hi),        TTD_FIELD(TTDCpuState, iff1),
    TTD_FIELD(TTDCpuState, iff2),        TTD_FIELD(TTDCpuState, im),
    TTD_FIELD(TTDCpuState, halted),      TTD_FIELD(TTDCpuState, nmi_pending),
    TTD_FIELD(TTDCpuState, memptr),      TTD_FIELD(TTDCpuState, q),
    TTD_FIELD(TTDCpuState, boundary),    TTD_FIELD(TTDCpuState, eipos),
    TTD_FIELD(TTDCpuState, haltpos),     TTD_FIELD(TTDCpuState, nmi_in_progress),
    TTD_FIELD(TTDCpuState, int_pending), TTD_FIELD(TTDCpuState, int_gate),
    TTD_FIELD(TTDCpuState, int_acked_in_pulse), TTD_FIELD(TTDCpuState, halt_cycle),
};

const Field kChipsetFields[] = {
    TTD_FIELD(TTDChipsetState, t_states),
    TTD_FIELD(TTDChipsetState, frame_counter),
    TTD_FIELD(TTDChipsetState, p7FFD),
    TTD_FIELD(TTDChipsetState, pFE),
    TTD_FIELD(TTDChipsetState, pEFF7),
    TTD_FIELD(TTDChipsetState, pBFFD),
    TTD_FIELD(TTDChipsetState, pFFFD),
    TTD_FIELD(TTDChipsetState, pFF77),
    TTD_FIELD(TTDChipsetState, border_attr),
    TTD_FIELD(TTDChipsetState, flags),
    TTD_FIELD(TTDChipsetState, wd_shadow),
    TTD_FIELD(TTDChipsetState, comp_pal),
    TTD_FIELD(TTDChipsetState, ulaplus_mode),
    TTD_FIELD(TTDChipsetState, ulaplus_reg),
    TTD_FIELD(TTDChipsetState, ulaplus_cram),
    TTD_FIELD(TTDChipsetState, hw_turbo_ratio),
    TTD_FIELD(TTDChipsetState, hw_turbo_ratio_applied),
    TTD_FIELD(TTDChipsetState, current_z80_frequency_multiplier),
    TTD_FIELD(TTDChipsetState, next_z80_frequency_multiplier),
    TTD_FIELD(TTDChipsetState, cpu_t_in_frame),
    TTD_FIELD(TTDChipsetState, hw_clock_den),
    TTD_FIELD(TTDChipsetState, hw_clock_den_applied),
    TTD_FIELD(TTDChipsetState, cpu_t_fraction),
};

#undef TTD_FIELD

/// Items listed per diverged frame; the rest are counted
constexpr size_t kMaxItemsPerFrame = 32;

uint64_t ValueOf(const uint8_t* p, size_t size)
{
    uint64_t v = 0;
    for (size_t k = 0; k < size && k < 8; ++k)
        v |= uint64_t(p[k]) << (8 * k);   // the fields are little-endian host values; shown, not stored
    return v;
}

/// Bytes that differ between two buffers (a size difference counts its bytes); the first such offset
uint32_t CountDiffering(const uint8_t* a, size_t na, const uint8_t* b, size_t nb, uint32_t& first)
{
    first = UINT32_MAX;
    if (na == nb && (na == 0 || std::memcmp(a, b, na) == 0))
        return 0;
    const size_t n = std::min(na, nb);
    uint32_t differing = 0;
    for (size_t k = 0; k < n; ++k)
        if (a[k] != b[k])
        {
            if (differing == 0)
                first = static_cast<uint32_t>(k);
            ++differing;
        }
    if (na != nb)
    {
        if (differing == 0)
            first = static_cast<uint32_t>(n);
        differing += static_cast<uint32_t>(std::max(na, nb) - n);
    }
    return differing;
}

}  // namespace

std::string TTDReplayCheckText(const TTDReplayCheckResult& r)
{
    if (!r.error.empty())
        return "replay check: " + r.error + "\n";
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "replay check f%llu..f%llu: %llu checked, %llu match, %llu diverged; skipped %llu barrier, %llu cut, "
                  "%llu no next checkpoint; %.1f ms\n",
                  static_cast<unsigned long long>(r.fromFrame), static_cast<unsigned long long>(r.toFrame),
                  static_cast<unsigned long long>(r.checked), static_cast<unsigned long long>(r.matched),
                  static_cast<unsigned long long>(r.diverged), static_cast<unsigned long long>(r.skippedBarrier),
                  static_cast<unsigned long long>(r.skippedCut), static_cast<unsigned long long>(r.skippedNoNext), r.ms);
    std::string text = buf;
    if (r.stoppedAtLimit)
        text += "stopped after f" + std::to_string(r.stoppedAfterFrame) + " (limit of diverged frames)\n";
    for (const TTDReplayCheckFrame& f : r.frames)
    {
        text += "f" + std::to_string(f.frame) + " -> f" + std::to_string(f.frame + 1) + ":\n";
        for (const TTDReplayCheckItem& i : f.items)
        {
            if (i.hasValues)
                std::snprintf(buf, sizeof(buf), "  %s %s: recorded #%llX, replayed #%llX\n", i.section.c_str(),
                              i.name.c_str(), static_cast<unsigned long long>(i.recorded),
                              static_cast<unsigned long long>(i.replayed));
            else
                std::snprintf(buf, sizeof(buf), "  %s %s: %u of %u bytes differ, first at +#%X\n", i.section.c_str(),
                              i.name.c_str(), i.differing, i.size, i.firstOffset);
            text += buf;
        }
        if (f.moreItems)
            text += "  ... " + std::to_string(f.moreItems) + " more\n";
    }
    return text;
}

TTDReplayCheckResult TimeTravelController::QueryReplayCheck(const TTDReplayCheckQuery& query)
{
    const auto started = std::chrono::steady_clock::now();
    // Restores and replays, then puts the machine back: an operation that changes it
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDReplayCheckResult result;
    auto finish = [&]() -> TTDReplayCheckResult& {
        result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    };

    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (_timeline.size() < 2 || !z80 || !_context->pEmulator || !_replayEngine)
    {
        result.error = "no recorded history of two frames or more";
        return finish();
    }
    const uint64_t first = _timeline.front().time.frame;
    const uint64_t last = _timeline.back().time.frame - 1;   // the last frame with a next checkpoint
    result.fromFrame = query.hasFrom ? query.fromFrame : first;
    result.toFrame = query.hasTo ? query.toFrame : last;
    if (result.fromFrame < first || result.toFrame > last || result.toFrame < result.fromFrame)
    {
        result.error = "the frames f" + std::to_string(result.fromFrame) + "..f" + std::to_string(result.toFrame) +
                       " are outside the checkable history f" + std::to_string(first) + "..f" + std::to_string(last);
        return finish();
    }
    const uint64_t stride = std::max<uint64_t>(query.stride, 1);
    const uint32_t limit = std::clamp(query.limit, 1u, TTDReplayCheckQuery::kMaxLimit);

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot live;
    SaveLiveState(live);

    const std::vector<TTDRegistryEntry>& entries = _peripherals.Entries();
    LiveStateSnapshot recorded;
    LiveStateSnapshot replayed;
    std::vector<uint8_t> pieceBytes(kTTDPieceSize);
    std::vector<TTDPieceId> nextIds;   // per piece of a region: its version at N + 1 when N + 1 changed it
    std::vector<uint8_t> candidates;   // per piece: compared (changed at N + 1, written by the replay, or unknown)
    {
        const KnownMemoryScope knownMemory{*this};
        ReplayModeScope replay(*this);
        for (uint64_t frame = result.fromFrame; frame <= result.toFrame; frame += stride)
        {
            const int64_t at = TimelineIndexAtOrBefore(TTDTimePoint{frame, 0});
            const size_t idx = static_cast<size_t>(at);
            if (at < 0 || _timeline[idx].time.frame != frame || idx + 1 >= _timeline.size() ||
                _timeline[idx + 1].time.frame != frame + 1)
            {
                ++result.skippedNoNext;
                continue;
            }
            // A barrier inside the frame (an unrecorded change) or a cut at the next frame's start (a snapshot
            // load's state): the next checkpoint is not what the replay of this frame makes
            TTDMachineTime start = 0;
            TTDMachineTime end = 0;
            if (_replayEngine->Frames().Start(frame, start) && _replayEngine->Frames().Start(frame + 1, end) &&
                _replayEngine->Events().FirstBarrierIn(start, end > start ? end - 1 : start))
            {
                ++result.skippedBarrier;
                continue;
            }
            if (CutAtFrameStart(frame + 1))
            {
                ++result.skippedCut;
                continue;
            }

            // The recorded truth: checkpoint N + 1 as the live machine reached it. Its memory is not copied: the
            // engine's versions of N + 1 are compared below with what the replay left
            RestoreCheckpointForReplay(_timeline[idx + 1]);
            SaveLiveState(recorded, /*withMemory=*/false);

            // The replay: checkpoint N, the frame run to its end
            RestoreCheckpointForReplay(_timeline[idx]);
            // The restore leaves every piece the session knew at N's version; the trackers then mark what the run
            // writes. Without that knowledge every piece is compared
            const bool writesKnown = _engineMemoryKnown;
            {
                // Nothing is compared on the picture (the caller's comes back at the end): the frame runs undrawn, as
                // a skipped turbo frame of the live machine does - the drawing is no machine state
                Screen* screen = _context->pScreen;
                const bool skipWas = screen && screen->IsTurboRenderSkip();
                if (screen)
                    screen->SetTurboRenderSkip(true);
                RunToFrameEnd();
                if (screen)
                    screen->SetTurboRenderSkip(skipWas);
            }
            SaveLiveState(replayed, /*withMemory=*/false);
            const TTDWrittenFn written = writesKnown ? PeekWrittenSinceRestore() : TTDWrittenFn{};
            ++result.checked;

            TTDReplayCheckFrame diverged;
            diverged.frame = frame;
            auto add = [&](TTDReplayCheckItem item) {
                if (diverged.items.size() < kMaxItemsPerFrame)
                    diverged.items.push_back(std::move(item));
                else
                    ++diverged.moreItems;
            };
            auto fields = [&](const char* section, const Field* list, size_t count, const void* a, const void* b) {
                const auto* pa = static_cast<const uint8_t*>(a);
                const auto* pb = static_cast<const uint8_t*>(b);
                for (size_t k = 0; k < count; ++k)
                {
                    const Field& f = list[k];
                    if (std::memcmp(pa + f.offset, pb + f.offset, f.size) == 0)
                        continue;
                    TTDReplayCheckItem item;
                    item.section = section;
                    item.name = f.name;
                    item.size = static_cast<uint32_t>(f.size);
                    item.differing = CountDiffering(pa + f.offset, f.size, pb + f.offset, f.size, item.firstOffset);
                    if (f.size <= 8)
                    {
                        item.hasValues = true;
                        item.recorded = ValueOf(pa + f.offset, f.size);
                        item.replayed = ValueOf(pb + f.offset, f.size);
                    }
                    add(std::move(item));
                }
            };
            fields("cpu", kCpuFields, std::size(kCpuFields), &recorded.cpu, &replayed.cpu);
            fields("chipset", kChipsetFields, std::size(kChipsetFields), &recorded.chipset, &replayed.chipset);
            if (recorded.z80TInFrame != replayed.z80TInFrame)
            {
                TTDReplayCheckItem item;
                item.section = "cpu";
                item.name = "t";
                item.size = 4;
                item.differing = 1;
                item.hasValues = true;
                item.recorded = recorded.z80TInFrame;
                item.replayed = replayed.z80TInFrame;
                add(std::move(item));
            }
            // Memory, per 4 KB piece, against checkpoint N + 1's stored versions: a piece the replay did not write
            // and whose version N and N + 1 share holds N + 1's bytes already; the others are decoded and compared.
            // Machine RAM is named per 16 KB page, a device region (General Sound, NeoGS, MoonSound memories,
            // Sprinter video RAM, ...) as a whole
            const int64_t engineN = _engine->CheckpointIndexOf({0, frame, 0});
            const int64_t engineNext = _engine->CheckpointIndexOf({0, frame + 1, 0});
            const std::vector<TTDRegionDesc>& regions = _engine->Regions();
            for (uint32_t r = 0; engineN >= 0 && engineNext >= 0 && r < regions.size(); ++r)
            {
                const TTDRegionDesc& desc = regions[r];
                if (!desc.memory || _engine->IsDeviceStateRegion(r))
                    continue;
                const bool ram = desc.name == "ram";
                const uint32_t piecesPerGroup = ram ? 4 : std::max<uint32_t>(desc.pieces, 1);
                TTDReplayCheckItem group;
                uint32_t groupIndex = UINT32_MAX;
                auto flush = [&]() {
                    if (groupIndex == UINT32_MAX || group.differing == 0)
                        return;
                    const size_t start = size_t(groupIndex) * piecesPerGroup * kTTDPieceSize;
                    group.section = ram ? "ram" : "region";
                    group.name = ram ? "page " + std::to_string(groupIndex)
                                     : (desc.ownerInstance.empty() ? desc.name : desc.ownerInstance + "." + desc.name);
                    group.size = static_cast<uint32_t>(
                        std::min<size_t>(size_t(piecesPerGroup) * kTTDPieceSize, desc.bytes - start));
                    add(std::move(group));
                };
                // Consecutive checkpoints: the pieces N + 1 recorded a new version of are its changes, every other
                // piece has N's version there. Otherwise (or without the run's writes) every piece is looked up
                const bool consecutive = engineNext == engineN + 1;
                nextIds.assign(desc.pieces, TimeTravelEngine::kAbsent);
                candidates.assign(desc.pieces, written && consecutive ? 0 : 1);
                if (written && consecutive)
                {
                    for (const auto& [piece, id] : _engine->ChangesAt(size_t(engineNext), r))
                        if (piece < desc.pieces)
                        {
                            nextIds[piece] = id;
                            candidates[piece] = 1;
                        }
                    for (uint32_t p = 0; p < desc.pieces; ++p)
                        if (!candidates[p] && written(r, p))
                            candidates[p] = 1;
                }
                for (uint32_t p = 0; p < desc.pieces; ++p)
                {
                    if (!candidates[p])
                        continue;
                    // Not changed at N + 1 (consecutive): N's version, the one N's restore left in memory
                    TTDPieceId next = nextIds[p];
                    if (next == TimeTravelEngine::kAbsent && written && consecutive)
                        next = _engine->VersionInMemory(r, p);
                    if (next == TimeTravelEngine::kAbsent)
                        next = _engine->VersionAt(size_t(engineNext), r, p);
                    if (next == TimeTravelEngine::kAbsent)
                        continue;
                    if (written && !written(r, p) && nextIds[p] == TimeTravelEngine::kAbsent &&
                        _engine->VersionAt(size_t(engineN), r, p) == next)
                        continue;
                    const size_t off = size_t(p) * kTTDPieceSize;
                    if (off >= desc.bytes)
                        break;
                    const size_t len = std::min<size_t>(kTTDPieceSize, desc.bytes - off);
                    uint32_t firstAt = 0;
                    uint32_t differing = static_cast<uint32_t>(len);   // a damaged stored version: all of it
                    if (_engine->PieceStore().Decode(next, pieceBytes.data()))
                        differing = CountDiffering(pieceBytes.data(), len, desc.memory + off, len, firstAt);
                    if (differing == 0)
                        continue;
                    if (p / piecesPerGroup != groupIndex)
                    {
                        flush();
                        group = TTDReplayCheckItem{};
                        groupIndex = p / piecesPerGroup;
                    }
                    if (group.differing == 0)
                        group.firstOffset = static_cast<uint32_t>(off - size_t(groupIndex) * piecesPerGroup * kTTDPieceSize) + firstAt;
                    group.differing += differing;
                }
                flush();
            }
            // Every registered device's whole state
            for (size_t d = 0; d < entries.size(); ++d)
            {
                static const std::vector<uint8_t> none;
                const std::vector<uint8_t>& a = d < recorded.deviceStates.size() ? recorded.deviceStates[d] : none;
                const std::vector<uint8_t>& b = d < replayed.deviceStates.size() ? replayed.deviceStates[d] : none;
                TTDReplayCheckItem item;
                item.differing = CountDiffering(a.data(), a.size(), b.data(), b.size(), item.firstOffset);
                if (item.differing == 0)
                    continue;
                item.section = "device";
                const std::string& instance = entries[d].key.instance;
                item.name = instance.empty() ? entries[d].device->TTDDeviceName() : instance;
                item.size = static_cast<uint32_t>(std::max(a.size(), b.size()));
                add(std::move(item));
            }
            if (diverged.items.empty() && diverged.moreItems == 0)
            {
                ++result.matched;
                continue;
            }
            ++result.diverged;
            result.frames.push_back(std::move(diverged));
            if (result.frames.size() >= limit && frame + stride <= result.toFrame)
            {
                result.stoppedAtLimit = true;
                result.stoppedAfterFrame = frame;
                break;
            }
        }
        replay.Exit();
    }
    {
        ReplayModeScope restore(*this);
        RestoreLiveState(live);
        restore.Exit();
        SetState(stateBefore);
    }
    return finish();
}

void TimeTravelController::CheckSeekLanding(TTDSeekResult& result)
{
    TTDReplayCheckQuery q;
    q.hasFrom = q.hasTo = true;
    q.fromFrame = q.toFrame = result.arrivedAt.frame;
    q.limit = 1;
    const TTDReplayCheckResult check = QueryReplayCheck(q);
    if (!check.error.empty() || check.checked == 0)
        return;   // the frame has no next checkpoint, or a barrier or a cut makes it uncheckable
    result.checked = true;
    result.checkDiverged = check.diverged != 0;
    result.checkText = TTDReplayCheckText(check);
    ++_seekChecks;
    if (result.checkDiverged)
    {
        ++_seekCheckDivergences;
        _lastSeekDivergence = result.checkText;
    }
}

}  // namespace ttd
