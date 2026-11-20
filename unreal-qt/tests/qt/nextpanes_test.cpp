// The debugger's ZX Spectrum Next panes (Next TODO "Qt panels"): each a ReportPane over the report the automation planes serve;
// a stop refresh lights what changed since the last stop, a live one marks nothing, another machine says why it is empty.

#include <QApplication>
#include <QTableWidget>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <memory>

#include "debugger/nextpanes.h"
#include "debugger/reportpane.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextPanes_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("next-panes-test", model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
    }
    void Out(uint16_t port, uint8_t value)
    {
        auto* ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(ports, nullptr);
        ports->DecodePortOut(port, value, 0);
    }
    static int RowWith(QTableWidget* table, int column, const QString& text)
    {
        for (int row = 0; row < table->rowCount(); row++)
            if (table->item(row, column) && table->item(row, column)->text() == text)
                return row;
        return -1;
    }
    static QTreeWidgetItem* Field(QTreeWidget* tree, const QString& name)
    {
        const QList<QTreeWidgetItem*> found = tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive, 0);
        return found.isEmpty() ? nullptr : found.front();
    }
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    QWidget _parent;
};

TEST_F(NextPanes_Test, SixPanesInTabOrder)
{
    const std::vector<NextPanes::Pane> panes = NextPanes::Create(&_parent);
    ASSERT_EQ(panes.size(), 6u);
    const char* titles[] = {"NextREG", "MMU", "Copper", "Sprites", "DMA", "CTC / IM2"};
    for (size_t i = 0; i < panes.size(); i++)
    {
        EXPECT_EQ(panes[i].title, titles[i]);
        EXPECT_NE(panes[i].pane, nullptr);
        EXPECT_FALSE(panes[i].tooltip.isEmpty());
    }
}

TEST_F(NextPanes_Test, StopRefreshLightsAChangedRegisterLiveDoesNot)
{
    ASSERT_NO_FATAL_FAILURE(Create("NEXT"));
    ReportPane* regs = NextPanes::Create(&_parent).front().pane;
    regs->refresh(_context, false);
    QTableWidget* table = regs->table();
    ASSERT_NE(table, nullptr);
    const int row = RowWith(table, 0, "0x7F");  // NR #7F, the user register: read / write, reset 0xFF
    ASSERT_GE(row, 0) << "the NextREG file lists NR #7F";
    EXPECT_TRUE(regs->status().isEmpty());

    Out(0x243B, 0x7F);
    Out(0x253B, 0x42);
    regs->refresh(_context, true);
    EXPECT_EQ(table->item(row, 1)->text(), "0x42") << "a live refresh shows the value";
    EXPECT_FALSE(regs->isMarked(QStringLiteral("%1.value").arg(row))) << "... and marks nothing";
    EXPECT_FALSE(regs->status().isEmpty()) << "it says the values are live";

    regs->refresh(_context, false);
    EXPECT_TRUE(regs->isMarked(QStringLiteral("%1.value").arg(row))) << "the stop compares with the last stop";
    EXPECT_FALSE(regs->isMarked(QStringLiteral("%1.name").arg(row)));
    regs->refresh(_context, false);
    EXPECT_FALSE(regs->isMarked(QStringLiteral("%1.value").arg(row))) << "unchanged since that stop";
}

TEST_F(NextPanes_Test, DmaFieldsAndCtcChannels)
{
    ASSERT_NO_FATAL_FAILURE(Create("NEXT"));
    const std::vector<NextPanes::Pane> panes = NextPanes::Create(&_parent);
    ReportPane* dma = panes[4].pane;
    dma->refresh(_context, false);
    EXPECT_EQ(dma->table(), nullptr) << "the DMA is fields only";
    QTreeWidgetItem* direction = Field(dma->fields(), "direction");
    ASSERT_NE(direction, nullptr);
    EXPECT_TRUE(direction->text(1) == "a_to_b" || direction->text(1) == "b_to_a") << direction->text(1).toStdString();
    ASSERT_NE(Field(dma->fields(), "a"), nullptr) << "port A as a nested object";

    ReportPane* ctc = panes[5].pane;
    ctc->refresh(_context, false);
    ASSERT_NE(ctc->table(), nullptr);
    EXPECT_EQ(ctc->table()->rowCount(), 4) << "four CTC channels";
    EXPECT_NE(Field(ctc->fields(), "pending"), nullptr) << "the IM2 controller's fields";

    ReportPane* copper = panes[2].pane;
    copper->refresh(_context, false);
    ASSERT_NE(copper->table(), nullptr);
    EXPECT_LE(copper->table()->rowCount(), 1) << "an empty list: only the word at the copper's PC";
}

TEST_F(NextPanes_Test, AnotherMachineSaysWhy)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    for (const NextPanes::Pane& pane : NextPanes::Create(&_parent))
    {
        pane.pane->refresh(_context, false);
        EXPECT_EQ(pane.pane->status(), "not a ZX Spectrum Next") << pane.title.toStdString();
        if (pane.pane->table())
            EXPECT_EQ(pane.pane->table()->rowCount(), 0);
    }
}
