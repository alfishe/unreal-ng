#include "ttdtimelinefixture.h"

#include <cstddef>
#include <string>

#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"

namespace ttd
{
namespace bench
{

namespace
{
constexpr uint16_t kCode = 0x8000;
constexpr uint16_t kStart = 0x8040;            // the subroutines sit below
constexpr uint16_t kVectorTable = 0x9000;   // I = #90: 257 bytes of #91, the handler at #9191
constexpr uint16_t kHandler = 0x9191;
constexpr uint16_t kFrames = 0x9F00;        // word: frames counted by the handler
constexpr uint16_t kPhaseEnd = 0x9F04;      // word: the frame the running phase ends at
constexpr uint16_t kPhase = 0x9F06;         // byte: the running phase (TimelinePhaseKind)
constexpr uint16_t kLastFrame = 0x9F08;     // word: phase D's frame wait
constexpr uint16_t kScan = 0x9F10;          // byte: phase C's last half-row
constexpr uint16_t kStack = 0x9E00;

/// Bytes with labels and backward relative jumps resolved as they are emitted
struct Emitter
{
    std::vector<uint8_t> bytes;
    uint16_t origin = 0;

    uint16_t Here() const { return static_cast<uint16_t>(origin + bytes.size()); }
    void Byte(uint8_t b) { bytes.push_back(b); }
    void Bytes(std::initializer_list<uint8_t> list) { bytes.insert(bytes.end(), list); }
    void Word(uint16_t w)
    {
        Byte(static_cast<uint8_t>(w & 0xFF));
        Byte(static_cast<uint8_t>(w >> 8));
    }
    void Op16(std::initializer_list<uint8_t> op, uint16_t w)
    {
        Bytes(op);
        Word(w);
    }
    /// JR / DJNZ / JR cc back to `target`
    void Relative(uint8_t op, uint16_t target)
    {
        Byte(op);
        const int offset = static_cast<int>(target) - static_cast<int>(Here() + 1);
        Byte(static_cast<uint8_t>(static_cast<int8_t>(offset)));
    }
};

constexpr uint8_t kJrC = 0x38, kJrZ = 0x28, kDjnz = 0x10;

std::vector<uint8_t> Program(unsigned phaseFrames, uint16_t& setEnd, uint16_t& done)
{
    Emitter e;
    e.origin = kCode;
    // Subroutines first, at fixed places the phases call
    const uint16_t start = kStart;
    // SetEnd: (kPhaseEnd) = (kFrames) + phaseFrames
    setEnd = e.Here();
    e.Op16({0x2A}, kFrames);                       // ld hl,(frames)
    e.Op16({0x11}, static_cast<uint16_t>(phaseFrames));  // ld de,n
    e.Byte(0x19);                                  // add hl,de
    e.Op16({0x22}, kPhaseEnd);                     // ld (end),hl
    e.Byte(0xC9);                                  // ret
    // Done: carry while (kFrames) < (kPhaseEnd); A is kept
    done = e.Here();
    e.Op16({0x2A}, kFrames);                       // ld hl,(frames)
    e.Op16({0xED, 0x5B}, kPhaseEnd);               // ld de,(end)
    e.Byte(0xB7);                                  // or a
    e.Bytes({0xED, 0x52});                         // sbc hl,de
    e.Byte(0xC9);                                  // ret

    e.bytes.resize(start - kCode, 0x00);
    e.Byte(0xF3);                                  // di
    e.Op16({0x31}, kStack);                        // ld sp,nn
    e.Bytes({0x3E, static_cast<uint8_t>(kVectorTable >> 8), 0xED, 0x47, 0xED, 0x5E});   // ld a,#90; ld i,a; im 2
    e.Op16({0x21}, 0x0000);
    e.Op16({0x22}, kFrames);                       // frames = 0
    e.Byte(0xFB);                                  // ei
    const uint16_t cycle = e.Here();

    auto enter = [&](TimelinePhaseKind kind) {
        e.Bytes({0x3E, static_cast<uint8_t>(kind)});
        e.Op16({0x32}, kPhase);                    // ld (phase),a
        e.Op16({0xCD}, setEnd);                    // call SetEnd
    };

    // A: tight IN loop on #FE
    enter(TimelinePhaseKind::Tape);
    const uint16_t a = e.Here();
    e.Bytes({0x06, 50});                           // ld b,50
    const uint16_t a1 = e.Here();
    e.Bytes({0x3E, 0x7F, 0xDB, 0xFE});             // ld a,#7F; in a,(#FE)
    e.Relative(kDjnz, a1);
    e.Op16({0xCD}, done);
    e.Relative(kJrC, a);

    // B: fill #C000-#FFFF with a new value each pass
    enter(TimelinePhaseKind::Burst);
    e.Bytes({0x3E, 0x55});                         // ld a,#55
    const uint16_t b = e.Here();
    e.Op16({0x21}, 0xC000);
    e.Byte(0x77);                                  // ld (hl),a
    e.Op16({0x11}, 0xC001);
    e.Op16({0x01}, 0x3FFF);
    e.Bytes({0xED, 0xB0});                         // ldir
    e.Byte(0x3C);                                  // inc a
    e.Op16({0xCD}, done);
    e.Relative(kJrC, b);

    // C: HALT, then the eight half-rows
    enter(TimelinePhaseKind::Idle);
    const uint16_t c = e.Here();
    e.Byte(0x76);                                  // halt
    e.Bytes({0x06, 8, 0x16, 0xFE});                // ld b,8; ld d,#FE
    const uint16_t c1 = e.Here();
    e.Bytes({0x7A, 0xDB, 0xFE});                   // ld a,d; in a,(#FE)
    e.Op16({0x32}, kScan);                         // ld (scan),a
    e.Bytes({0xCB, 0x02});                         // rlc d
    e.Relative(kDjnz, c1);
    e.Op16({0xCD}, done);
    e.Relative(kJrC, c);

    // D: once a frame (busy wait, no HALT): AY, beeper, screen, paging
    enter(TimelinePhaseKind::Game);
    const uint16_t d = e.Here();
    e.Op16({0x2A}, kFrames);
    e.Op16({0x22}, kLastFrame);                    // last = frames
    const uint16_t d1 = e.Here();
    e.Op16({0x2A}, kFrames);
    e.Op16({0xED, 0x5B}, kLastFrame);
    e.Byte(0xB7);
    e.Bytes({0xED, 0x52});                         // frames - last
    e.Relative(kJrZ, d1);
    e.Op16({0x2A}, kFrames);                       // l = the frame's low byte
    auto ay = [&](uint8_t reg, std::initializer_list<uint8_t> value) {
        e.Op16({0x01}, 0xFFFD);
        e.Bytes({0x3E, reg, 0xED, 0x79});          // select
        e.Bytes({0x06, 0xBF});
        e.Bytes(value);
        e.Bytes({0xED, 0x79});                     // write
    };
    ay(7, {0x3E, 0x3E});                           // mixer: tone A
    ay(0, {0x7D});                                 // tone A fine = l
    ay(8, {0x3E, 0x0F});                           // volume A
    e.Bytes({0x3E, 0x10, 0xD3, 0xFE, 0xAF, 0xD3, 0xFE});   // beeper on, off (border 0)
    e.Byte(0x7D);                                  // ld a,l
    e.Op16({0x21}, 0x4000);
    e.Byte(0x77);
    e.Op16({0x11}, 0x4001);
    e.Op16({0x01}, 0x00FF);
    e.Bytes({0xED, 0xB0});                         // 256 screen bytes
    e.Op16({0x3A}, kFrames);
    e.Bytes({0xE6, 0x01, 0xF6, 0x10});             // and 1; or #10: page 0 / 1 at #C000, ROM 1, screen 5
    e.Op16({0x01}, 0x7FFD);
    e.Bytes({0xED, 0x79});
    e.Op16({0xCD}, done);
    e.Relative(kJrC, d);

    e.Op16({0xC3}, cycle);                         // jp cycle
    return e.bytes;
}
}  // namespace

TimelineSession::~TimelineSession()
{
    if (emulator)
        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}

bool TimelineSession::Ok() const
{
    return ttd != nullptr && endFrame > firstFrame && !phases.empty();
}

std::unique_ptr<TimelineSession> RecordTimelineProgram(unsigned phaseFrames, unsigned cycles, bool writeJournal)
{
    auto s = std::make_unique<TimelineSession>();
    s->emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttd-timeline-fixture", "PENTAGON",
                                                                          LoggerLevel::LogNone);
    if (!s->emulator || phaseFrames < 3 || cycles == 0)
        return s;
    EmulatorContext* context = s->context = s->emulator->GetContext();
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    s->emulator->EnableTurboMode();
    s->ttd = context->pTimeTravelController;
    s->ttd->SetEnableWriteJournal(writeJournal);

    uint16_t setEnd = 0, done = 0;
    const std::vector<uint8_t> code = Program(phaseFrames, setEnd, done);
    Memory* memory = context->pMemory;
    for (size_t i = 0; i < code.size(); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kCode + i), code[i]);
    for (uint16_t i = 0; i <= 0x100; i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kVectorTable + i), static_cast<uint8_t>(kHandler >> 8));
    const uint8_t handler[] = {0xE5, 0x2A, kFrames & 0xFF, kFrames >> 8, 0x23, 0x22, kFrames & 0xFF, kFrames >> 8,
                               0xE1, 0xFB, 0xED, 0x4D};   // push hl; frames++; pop hl; ei; reti
    for (size_t i = 0; i < sizeof(handler); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kHandler + i), handler[i]);
    memory->DirectWriteToZ80Memory(kPhase, 0xFF);
    context->pCore->GetZ80()->pc = kStart;

    if (!s->ttd->StartRecording())
        return s;
    DebugKeyboardManager* keys = context->pDebugManager->GetKeyboardManager();
    s->firstFrame = context->emulatorState.frame_counter;
    const uint64_t total = static_cast<uint64_t>(phaseFrames) * 4 * cycles;
    uint8_t phase = 0xFF;
    unsigned idlePhases = 0;
    TimelineKey pending;
    bool down = false;
    for (uint64_t n = 0; n < total; n++)
    {
        const uint64_t frame = context->emulatorState.frame_counter;
        // A key in the middle third of each idle phase, A and Q in turn
        if (!s->phases.empty() && s->phases.back().kind == TimelinePhaseKind::Idle)
        {
            const uint64_t into = frame - s->phases.back().firstFrame;
            if (!down && into == phaseFrames / 3)
            {
                pending.key = (idlePhases % 2) ? ZXKEY_Q : ZXKEY_A;
                pending.downFrame = frame;
                keys->PressKey(static_cast<ZXKeysEnum>(pending.key));
                down = true;
            }
            else if (down && into == 2 * phaseFrames / 3)
            {
                keys->ReleaseKey(static_cast<ZXKeysEnum>(pending.key));
                pending.upFrame = frame;
                s->keys.push_back(pending);
                down = false;
            }
        }
        s->emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        // The phase the program ran at the frame's end: a change began in this frame
        const uint8_t now = memory->DirectReadFromZ80Memory(kPhase);
        if (now != phase && now <= static_cast<uint8_t>(TimelinePhaseKind::Game))
        {
            if (!s->phases.empty())
                s->phases.back().endFrame = frame;
            TimelinePhase p;
            p.kind = static_cast<TimelinePhaseKind>(now);
            p.firstFrame = frame;
            s->phases.push_back(p);
            if (p.kind == TimelinePhaseKind::Idle)
                ++idlePhases;
            phase = now;
        }
    }
    if (down)
        keys->ReleaseKey(static_cast<ZXKeysEnum>(pending.key));
    s->endFrame = context->emulatorState.frame_counter;
    if (!s->phases.empty())
        s->phases.back().endFrame = s->endFrame;
    s->ttd->StopRecording();
    s->ttd->SeekTo(s->ttd->SessionEndPosition());
    return s;
}

}  // namespace bench
}  // namespace ttd
