// The debugger's stop line: which breakpoint stopped the machine, on what access, the condition that held, the hit
// count, with the links to edit it and to the list.

#include <gtest/gtest.h>

#include <memory>

#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpointstopline.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

class BreakpointStopLine_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("breakpoint-stopline-test", "48K",
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    BreakpointSnapshot Add(const BreakpointSpec& spec)
    {
        std::string error;
        const uint16_t id = _emulator->GetBreakpointManager()->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return _emulator->GetBreakpointManager()->GetBreakpointById(id);
    }
    std::shared_ptr<Emulator> _emulator;
};

TEST_F(BreakpointStopLine_Test, TellsTheBreakpointTheAccessAndTheCondition)
{
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = 0x0000;
    spec.hasEnd = true;
    spec.addressEnd = 0xFFFF;
    spec.condition = "A == 0 && HL < #4000";
    const BreakpointSnapshot bp = Add(spec);
    ASSERT_TRUE(bp);
    BreakpointHit hit;
    hit.id = bp->breakpointID;
    hit.address = 0x0038;
    const QString line = BreakpointStopLine(*bp, hit);
    EXPECT_TRUE(line.contains(QString("Stopped by breakpoint #%1").arg(bp->breakpointID))) << line.toStdString();
    EXPECT_TRUE(line.contains("execution at $0038 (watching $0000-$FFFF)")) << line.toStdString();
    EXPECT_TRUE(line.contains("when <code>A == 0 &amp;&amp; HL &lt; #4000</code>")) << line.toStdString();
    EXPECT_TRUE(line.contains("hit 0"));
    EXPECT_TRUE(line.contains("href='edit'") && line.contains("href='list'"));
}

TEST_F(BreakpointStopLine_Test, AWatchpointSaysTheDirection)
{
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_WRITE;
    spec.address = 0x5C3A;
    spec.hitMode = BRK_HIT_AT_LEAST;
    spec.hitTarget = 5;
    const BreakpointSnapshot bp = Add(spec);
    ASSERT_TRUE(bp);
    BreakpointHit hit;
    hit.id = bp->breakpointID;
    hit.address = 0x5C3A;
    hit.kind = BreakpointHitKind::MemoryWrite;
    const QString line = BreakpointStopLine(*bp, hit);
    EXPECT_TRUE(line.contains("write to $5C3A")) << line.toStdString();
    EXPECT_FALSE(line.contains("watching")) << "one address: no span";
    EXPECT_FALSE(line.contains("when")) << "no condition";
    EXPECT_TRUE(line.contains("stops on &gt;=5")) << line.toStdString();

    hit.kind = BreakpointHitKind::PortOut;
    hit.address = 0x00FE;
    EXPECT_TRUE(BreakpointStopLine(*bp, hit).contains("OUT to port $00FE"));
}
