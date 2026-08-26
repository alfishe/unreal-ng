#include "stdafx.h"
#include "pch.h"

#include <base/featuremanager.h>
#include <emulator/ports/models/portdecoder_pentagon128.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/ports/portdiagrecorder.h>

#include <fstream>
#include <vector>

/// Tests for the Port Diagnostic Recorder (runtime feature "porttrace").
/// Design: docs/inprogress/2026-08-24-diagnostic-observability/

/// region <Helpers>

namespace
{
    /// Minimal PortDevice stand-in so hadHandler / wasDecoded flags can be observed
    class MockPortDevice final : public PortDevice
    {
    public:
        uint8_t portDeviceInMethod(uint16_t port) override
        {
            inPorts.push_back(port);
            return inValue;
        }

        void portDeviceOutMethod(uint16_t port, uint8_t value) override
        {
            outPorts.emplace_back(port, value);
        }

        uint8_t inValue = 0x42;
        std::vector<uint16_t> inPorts;
        std::vector<std::pair<uint16_t, uint8_t>> outPorts;
    };
}

/// endregion </Helpers>

/// region <Fixture>

class PortTrace_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    PortDecoder_Pentagon128* _portDecoder = nullptr;
    FeatureManager* _featureManager = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _portDecoder = new PortDecoder_Pentagon128(_context);
        _featureManager = new FeatureManager(_context);
        _context->pFeatureManager = _featureManager;
        _context->pPortDecoder = _portDecoder;
    }

    void TearDown() override
    {
        _context->pPortDecoder = nullptr;
        _context->pFeatureManager = nullptr;

        delete _portDecoder;
        _portDecoder = nullptr;
        delete _featureManager;
        _featureManager = nullptr;
        delete _context;
        _context = nullptr;
    }

    /// Enable the porttrace feature (FeatureManager cascade instantiates the recorder)
    PortDiagnosticRecorder* enablePortTrace()
    {
        EXPECT_TRUE(_featureManager->setFeature(Features::kPortTrace, true));
        PortDiagnosticRecorder* recorder = _portDecoder->getPortTraceRecorder();
        EXPECT_NE(recorder, nullptr);
        return recorder;
    }
};

/// endregion </Fixture>

/// region <Feature gating>

TEST_F(PortTrace_Test, FeatureRegisteredWithCorrectMetadata)
{
    auto features = _featureManager->listFeatures();
    auto it = std::find_if(features.begin(), features.end(),
                           [](const FeatureManager::FeatureInfo& f) { return f.id == Features::kPortTrace; });
    ASSERT_NE(it, features.end()) << "porttrace feature must be registered";

    EXPECT_EQ(it->alias, Features::kPortTraceAlias) << "alias must be 'pt'";
    EXPECT_EQ(it->category, Features::kCategoryDebug);
    EXPECT_FALSE(it->enabled) << "must be OFF by default";
}

TEST_F(PortTrace_Test, RecorderNotInstantiatedWhileFeatureOff)
{
    // Default state: feature off, no recorder, no buffer memory
    EXPECT_EQ(_portDecoder->getPortTraceRecorder(), nullptr);

    // I/O works normally with the feature off
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1234);
    uint8_t result = _portDecoder->DecodePortIn(0x7FFD, 0x1234);
    (void)result;

    EXPECT_EQ(_portDecoder->getPortTraceRecorder(), nullptr);
}

TEST_F(PortTrace_Test, FeatureToggleInstantiatesAndReleasesRecorder)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    ASSERT_NE(recorder, nullptr);

    // Toggle off mid-run: recorder released, subsequent I/O must not crash
    ASSERT_TRUE(_featureManager->setFeature(Features::kPortTrace, false));
    EXPECT_EQ(_portDecoder->getPortTraceRecorder(), nullptr);

    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1234);

    // Toggle on again: fresh recorder
    recorder = enablePortTrace();
    ASSERT_NE(recorder, nullptr);
    EXPECT_EQ(recorder->eventCount(), 0u);
}

TEST_F(PortTrace_Test, NoCaptureWhileSessionStopped)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();

    // Feature on but no start(): nothing recorded
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1234);
    EXPECT_EQ(recorder->eventCount(), 0u);
    EXPECT_EQ(recorder->getSessionState(), PortTraceSessionState::Stopped);
}

/// endregion </Feature gating>

/// region <Event capture and disposition>

TEST_F(PortTrace_Test, SingleEventPerOutOperation)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    // Inline-handled port (FFFD has no registered handler here but decodes via rule 0)
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1234);

    ASSERT_EQ(recorder->eventCount(), 1u) << "exactly ONE event per I/O operation";

    PortTraceEvent event = recorder->getAll()[0];
    EXPECT_EQ(event.rawPort, 0xFFFD);
    EXPECT_EQ(event.decodedPort, 0xFFFD);
    EXPECT_EQ(event.value, 0xFE);
    EXPECT_EQ(event.pc, 0x1234);
    EXPECT_TRUE(event.isOut());
    EXPECT_EQ(event.decodeRuleIndex, 0) << "FFFD is rule #0 in the Pentagon decode table";
    EXPECT_EQ(event.deviceId, PortDeviceId::AY_FFFD);
    EXPECT_FALSE(event.hadHandler()) << "no AY device registered in this fixture";
}

TEST_F(PortTrace_Test, AyMirrorResolvedWithRawPortPreserved)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    // 0xFEFD is an alias of #FFFD (A15=1, A14=1, A1=0)
    _portDecoder->DecodePortOut(0xFEFD, 0xFE, 0x2000);

    ASSERT_EQ(recorder->eventCount(), 1u);
    PortTraceEvent event = recorder->getAll()[0];
    EXPECT_EQ(event.rawPort, 0xFEFD) << "raw bus value must be preserved";
    EXPECT_EQ(event.decodedPort, 0xFFFD) << "mirror must resolve to the canonical port";
    EXPECT_EQ(event.deviceId, PortDeviceId::AY_FFFD);
}

TEST_F(PortTrace_Test, InlineHandledInReadback7FFD)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    // IN from #7FFD is handled inline by the decoder (p7FFD read-back)
    _portDecoder->DecodePortIn(0x7FFD, 0x3000);

    ASSERT_EQ(recorder->eventCount(), 1u) << "inline handling must not double-record";
    PortTraceEvent event = recorder->getAll()[0];
    EXPECT_FALSE(event.isOut());
    EXPECT_EQ(event.decodedPort, 0x7FFD);
    EXPECT_EQ(event.decodeRuleIndex, 2) << "7FFD is rule #2 in the Pentagon decode table";
    EXPECT_TRUE(event.wasHandledInline());
    EXPECT_TRUE(event.wasDecoded());
    EXPECT_EQ(event.deviceId, PortDeviceId::Memory_7FFD);
}

TEST_F(PortTrace_Test, UnmappedPortRecordedAsUnmapped)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    // 0x0001 matches no Pentagon decode rule (odd address, no BDI/AY/COVOX pattern)
    _portDecoder->DecodePortOut(0x0001, 0x55, 0x4000);

    ASSERT_EQ(recorder->eventCount(), 1u);
    PortTraceEvent event = recorder->getAll()[0];
    EXPECT_EQ(event.rawPort, 0x0001);
    EXPECT_EQ(event.decodedPort, 0x0000) << "unmapped signature";
    EXPECT_EQ(event.decodeRuleIndex, PortTraceRule::kNoMatch);
    EXPECT_EQ(event.deviceId, PortDeviceId::None);
    EXPECT_FALSE(event.wasDecoded());
    EXPECT_FALSE(event.hadHandler());
}

TEST_F(PortTrace_Test, Beta128GateRecordedWithTrdosState)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();

    MockPortDevice fdc;
    for (uint16_t port : {0x001F, 0x003F, 0x005F, 0x007F, 0x00FF})
        _portDecoder->RegisterPortHandler(port, &fdc);

    // TR-DOS inactive: FDC off the bus, writes gated
    _context->emulatorState.flags &= ~CF_TRDOS;
    recorder->start();
    _portDecoder->DecodePortOut(0x001F, 0x88, 0x3D00);

    ASSERT_EQ(recorder->eventCount(), 1u);
    PortTraceEvent gated = recorder->getAll()[0];
    EXPECT_EQ(gated.decodedPort, 0x0000) << "gate drops the decode";
    EXPECT_TRUE(gated.wasBeta128Gated());
    EXPECT_FALSE(gated.cfTrdosActive()) << "trace must show WHY the gate fired";
    EXPECT_TRUE(fdc.outPorts.empty()) << "gated write must not reach the FDC";

    // TR-DOS active: same write goes through, BDI fallback rule attributes it
    _context->emulatorState.flags |= CF_TRDOS;
    recorder->start();  // restart clears the buffer
    _portDecoder->DecodePortOut(0x001F, 0x88, 0x3D00);

    ASSERT_EQ(recorder->eventCount(), 1u);
    PortTraceEvent served = recorder->getAll()[0];
    EXPECT_EQ(served.decodedPort, 0x001F);
    EXPECT_EQ(served.decodeRuleIndex, PortTraceRule::kBdiFallback);
    EXPECT_FALSE(served.wasBeta128Gated());
    EXPECT_TRUE(served.cfTrdosActive());
    EXPECT_TRUE(served.hadHandler());
    EXPECT_EQ(served.deviceId, PortDeviceId::WD1793_Status);
    ASSERT_EQ(fdc.outPorts.size(), 1u);
}

TEST_F(PortTrace_Test, HandlerAttributionForRegisteredDevice)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();

    MockPortDevice ay;
    _portDecoder->RegisterPortHandler(0xFFFD, &ay);
    _portDecoder->RegisterPortHandler(0xBFFD, &ay);

    recorder->start();

    // TurboSound chip-select sequence: (FFFD, 0xFE) -> (FFFD, 0x08) -> (BFFD, 0x20)
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x8000);
    _portDecoder->DecodePortOut(0xFFFD, 0x08, 0x8005);
    _portDecoder->DecodePortOut(0xBFFD, 0x20, 0x800A);

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 3u) << "ordered sequence, no drops, no duplicates";

    EXPECT_EQ(events[0].decodedPort, 0xFFFD);
    EXPECT_EQ(events[0].value, 0xFE);
    EXPECT_TRUE(events[0].hadHandler());
    EXPECT_TRUE(events[0].wasDecoded());

    EXPECT_EQ(events[1].decodedPort, 0xFFFD);
    EXPECT_EQ(events[1].value, 0x08);

    EXPECT_EQ(events[2].decodedPort, 0xBFFD);
    EXPECT_EQ(events[2].value, 0x20);
    EXPECT_EQ(events[2].deviceId, PortDeviceId::AY_BFFD);

    ASSERT_EQ(ay.outPorts.size(), 3u) << "all writes reached the device exactly once";
}

/// endregion </Event capture and disposition>

/// region <Filtering>

TEST_F(PortTrace_Test, CompoundIncludeRuleIsAndWithinOrAcross)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();

    // Handler needed: PeripheralPortIn's no-handler warning path requires pCore,
    // which this lightweight test context does not create
    MockPortDevice ay;
    _portDecoder->RegisterPortHandler(0xFFFD, &ay);

    // include { port=FFFD AND direction=OUT } — compound rule
    PortTraceFilterRule rule;
    rule.decodedPort = 0xFFFD;
    rule.directionOut = true;
    recorder->addIncludeRule(rule);

    recorder->start();
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1000);  // matches (port AND out)
    _portDecoder->DecodePortIn(0xFFFD, 0x1000);         // rejected (IN)
    _portDecoder->DecodePortOut(0x0001, 0x55, 0x1000);  // rejected (other port)

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].decodedPort, 0xFFFD);
    EXPECT_TRUE(events[0].isOut());
    EXPECT_EQ(recorder->totalFiltered(), 2u);
}

TEST_F(PortTrace_Test, ExcludeAlwaysWinsOverInclude)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();

    // Empty include = everything; exclude all INs
    PortTraceFilterRule noIns;
    noIns.directionOut = false;
    recorder->addExcludeRule(noIns);

    recorder->start();
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1000);
    _portDecoder->DecodePortIn(0x7FFD, 0x1000);

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events[0].isOut());
}

TEST_F(PortTrace_Test, UnmappedOnlyFilter)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->presetUnmapped();

    recorder->start();
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1000);  // decoded — rejected
    _portDecoder->DecodePortOut(0x0001, 0x55, 0x2000);  // unmapped — recorded

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].rawPort, 0x0001);
    EXPECT_EQ(events[0].decodedPort, 0x0000);
}

TEST_F(PortTrace_Test, LiveFilterReconfigurationWhileCapturing)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    _portDecoder->DecodePortOut(0x0001, 0x11, 0x1000);  // recorded (no filter yet)

    // Narrow the filter WITHOUT stopping — earlier events stay in the buffer
    PortTraceFilterRule onlyFffd;
    onlyFffd.decodedPort = 0xFFFD;
    recorder->addIncludeRule(onlyFffd);

    _portDecoder->DecodePortOut(0x0001, 0x22, 0x1000);  // now rejected
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1000);  // recorded

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 2u) << "pre-change events preserved, filter applies to new events only";
    EXPECT_EQ(events[0].value, 0x11);
    EXPECT_EQ(events[1].decodedPort, 0xFFFD);
}

TEST_F(PortTrace_Test, PresetAyOnly)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->presetAyOnly();

    recorder->start();
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1000);
    _portDecoder->DecodePortOut(0xBFFD, 0x20, 0x1000);
    _portDecoder->DecodePortOut(0x0001, 0x55, 0x1000);

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].decodedPort, 0xFFFD);
    EXPECT_EQ(events[1].decodedPort, 0xBFFD);
}

/// endregion </Filtering>

/// region <Session control and overflow>

TEST_F(PortTrace_Test, PauseResumeStopSemantics)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    _portDecoder->DecodePortOut(0xFFFD, 0x01, 0x1000);
    recorder->pause();
    _portDecoder->DecodePortOut(0xFFFD, 0x02, 0x1000);  // not recorded (paused)
    recorder->resume();
    _portDecoder->DecodePortOut(0xFFFD, 0x03, 0x1000);
    recorder->stop();
    _portDecoder->DecodePortOut(0xFFFD, 0x04, 0x1000);  // not recorded (stopped)

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].value, 0x01);
    EXPECT_EQ(events[1].value, 0x03);

    // Data preserved after stop; clear purges it
    recorder->clear();
    EXPECT_EQ(recorder->eventCount(), 0u);
}

TEST_F(PortTrace_Test, RingOverflowKeepsNewestEvents)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    ASSERT_TRUE(recorder->setCapacity(4));
    ASSERT_TRUE(recorder->setOverflowMode(PortTraceOverflowMode::Ring));
    recorder->start();

    for (uint8_t i = 1; i <= 6; i++)
        _portDecoder->DecodePortOut(0xFFFD, i, 0x1000);

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].value, 3) << "oldest two evicted";
    EXPECT_EQ(events[3].value, 6);
    EXPECT_EQ(recorder->totalEvicted(), 2u);
    EXPECT_TRUE(recorder->isCapturing()) << "ring mode keeps capturing";
}

TEST_F(PortTrace_Test, StopWhenFullKeepsStartOfRun)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    ASSERT_TRUE(recorder->setCapacity(4));
    ASSERT_TRUE(recorder->setOverflowMode(PortTraceOverflowMode::StopWhenFull));
    recorder->start();

    for (uint8_t i = 1; i <= 6; i++)
        _portDecoder->DecodePortOut(0xFFFD, i, 0x1000);

    auto events = recorder->getAll();
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0].value, 1) << "start of the run preserved";
    EXPECT_EQ(events[3].value, 4);
    EXPECT_EQ(recorder->totalEvicted(), 0u);
    EXPECT_FALSE(recorder->isCapturing()) << "auto-stopped when full";
    EXPECT_TRUE(recorder->wasAutoStopped());
}

TEST_F(PortTrace_Test, ConfigurationRejectedWhileCapturing)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    EXPECT_FALSE(recorder->setCapacity(128));
    EXPECT_FALSE(recorder->setOverflowMode(PortTraceOverflowMode::StopWhenFull));
    EXPECT_FALSE(recorder->setCapacity(0)) << "zero capacity is invalid in any state";
}

TEST_F(PortTrace_Test, GetLastReturnsTail)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    recorder->start();

    for (uint8_t i = 1; i <= 5; i++)
        _portDecoder->DecodePortOut(0xFFFD, i, 0x1000);

    auto last2 = recorder->getLast(2);
    ASSERT_EQ(last2.size(), 2u);
    EXPECT_EQ(last2[0].value, 4);
    EXPECT_EQ(last2[1].value, 5);

    auto lastAll = recorder->getLast(100);
    EXPECT_EQ(lastAll.size(), 5u);
}

/// endregion </Session control and overflow>

/// region <PortActivitySummary>

TEST_F(PortTrace_Test, ActivitySummaryCountsPerFrame)
{
    enablePortTrace();

    _context->emulatorState.frame_counter = 100;

    // Summary counts even without an active capture session (feature-on only)
    _portDecoder->DecodePortOut(0xFFFD, 0xFE, 0x1000);  // decoded OUT
    _portDecoder->DecodePortOut(0x0001, 0x55, 0x1000);  // unmapped OUT
    _portDecoder->DecodePortIn(0x7FFD, 0x1000);         // decoded IN

    const PortActivitySummary& summary = _portDecoder->getActivitySummary();
    EXPECT_EQ(summary.frameNumber, 100u);
    EXPECT_EQ(summary.outCount, 2u);
    EXPECT_EQ(summary.inCount, 1u);
    EXPECT_EQ(summary.unmappedOutCount, 1u);
    EXPECT_EQ(summary.unmappedInCount, 0u);

    // Frame advance resets the counters
    _context->emulatorState.frame_counter = 101;
    _portDecoder->DecodePortOut(0xFFFD, 0x08, 0x1000);

    EXPECT_EQ(summary.frameNumber, 101u);
    EXPECT_EQ(summary.outCount, 1u);
    EXPECT_EQ(summary.inCount, 0u);
    EXPECT_EQ(summary.unmappedOutCount, 0u);
}

TEST_F(PortTrace_Test, ActivitySummaryCountsBeta128Gated)
{
    enablePortTrace();
    _context->emulatorState.flags &= ~CF_TRDOS;

    _portDecoder->DecodePortOut(0x001F, 0x88, 0x1000);  // gated

    const PortActivitySummary& summary = _portDecoder->getActivitySummary();
    EXPECT_EQ(summary.beta128GatedCount, 1u);
    EXPECT_EQ(summary.unmappedOutCount, 0u) << "gated is not counted as unmapped";
}

/// endregion </PortActivitySummary>

/// region <decodePortEx rule attribution>

TEST_F(PortTrace_Test, DecodePortExRuleAttribution)
{
    // Rule indices follow the Pentagon decode table order
    EXPECT_EQ(_portDecoder->decodePortEx(0xFFFD).ruleIndex, 0);
    EXPECT_EQ(_portDecoder->decodePortEx(0xBFFD).ruleIndex, 1);
    EXPECT_EQ(_portDecoder->decodePortEx(0x7FFD).ruleIndex, 2);
    EXPECT_EQ(_portDecoder->decodePortEx(0x00FE).ruleIndex, 3);
    EXPECT_EQ(_portDecoder->decodePortEx(0x00F1).ruleIndex, 4);  // SOUNDRIVE -> COVOX
    EXPECT_EQ(_portDecoder->decodePortEx(0x00FF).ruleIndex, 5);  // Beta128 system

    // BDI fallback attribution
    DecodeResult bdi = _portDecoder->decodePortEx(0x001F);
    EXPECT_EQ(bdi.port, 0x001F);
    EXPECT_EQ(bdi.ruleIndex, PortTraceRule::kBdiFallback);

    // No match
    DecodeResult none = _portDecoder->decodePortEx(0x0001);
    EXPECT_EQ(none.port, 0x0000);
    EXPECT_EQ(none.ruleIndex, PortTraceRule::kNoMatch);

    // decodePort() delegation stays consistent with decodePortEx()
    EXPECT_EQ(_portDecoder->decodePort(0x00F1), 0x00FB);
    EXPECT_EQ(_portDecoder->decodePort(0x0001), 0x0000);
}

/// endregion </decodePortEx rule attribution>

/// region <Export>

TEST_F(PortTrace_Test, ExportAllFormats)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();

    MockPortDevice fdc;
    for (uint16_t port : {0x001F, 0x003F, 0x005F, 0x007F, 0x00FF})
        _portDecoder->RegisterPortHandler(port, &fdc);

    _context->emulatorState.flags &= ~CF_TRDOS;
    recorder->start();

    _portDecoder->DecodePortOut(0xFEFD, 0xFE, 0x8000);  // AY mirror -> FFFD
    _portDecoder->DecodePortOut(0x0001, 0x55, 0x8005);  // unmapped
    _portDecoder->DecodePortOut(0x001F, 0x88, 0x3D00);  // Beta128-gated (CF_TRDOS off)
    recorder->stop();
    ASSERT_EQ(recorder->eventCount(), 3u);

    PortTraceSessionInfo info = _portDecoder->getPortTraceSessionInfo();
    EXPECT_EQ(info.decodeRules.size(), 6u) << "Pentagon decode table must be embedded";
    EXPECT_EQ(info.decodeRules[0].port, 0xFFFD);
    EXPECT_EQ(info.decodeRules[2].port, 0x7FFD);

    // All three formats write successfully; the offline converter
    // (tools/porttrace/porttrace_convert.py) is validated against these exact artifacts
    ASSERT_TRUE(recorder->saveToFile("porttrace_export_test.json", PortTraceExportFormat::JSON, info));
    ASSERT_TRUE(recorder->saveToFile("porttrace_export_test.csv", PortTraceExportFormat::CSV, info));
    ASSERT_TRUE(recorder->saveToFile("porttrace_export_test.bin", PortTraceExportFormat::Binary, info));

    // Binary header sanity: magic, version, count, rule count
    std::ifstream in("porttrace_export_test.bin", std::ios::binary);
    ASSERT_TRUE(in.good());
    uint8_t header[32] = {};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    EXPECT_EQ(memcmp(header, "PTRC", 4), 0);
    uint16_t version = 0, ruleCount = 0;
    uint32_t count = 0;
    memcpy(&version, header + 4, 2);
    memcpy(&count, header + 6, 4);
    memcpy(&ruleCount, header + 18, 2);
    EXPECT_EQ(version, 1);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(ruleCount, 6);
}

TEST_F(PortTrace_Test, FilterDescription)
{
    PortDiagnosticRecorder* recorder = enablePortTrace();
    EXPECT_EQ(recorder->describeFilter(), "All ports");

    PortTraceFilterRule rule;
    rule.decodedPort = 0xFFFD;
    rule.directionOut = true;
    recorder->addIncludeRule(rule);

    PortTraceFilterRule excl;
    excl.device = PortDeviceId::WD1793_Data;
    recorder->addExcludeRule(excl);

    std::string description = recorder->describeFilter();
    EXPECT_NE(description.find("port=0xFFFD"), std::string::npos) << description;
    EXPECT_NE(description.find("direction=OUT"), std::string::npos) << description;
    EXPECT_NE(description.find("exclude"), std::string::npos) << description;
    EXPECT_NE(description.find("WD1793_Data"), std::string::npos) << description;
}

/// endregion </Export>
