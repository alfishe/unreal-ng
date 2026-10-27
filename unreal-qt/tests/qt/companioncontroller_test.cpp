// Companion clients in the GUI (docs/inprogress/2026-10-16-companion-client): the GUI is the only approver. A
// pairing request opens a prompt with the code and the scopes; Allow pairs it with the ticked scopes, Deny refuses it,
// the prompt goes when the device cancels; Tools > Companions... lists and revokes

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include "emulator/companioncontroller.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"

using namespace automation_gate;

class CompanionController_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Registry().ResetForTesting();
        Registry().SetApprovalAvailable(true);
    }

    void TearDown() override { Registry().ResetForTesting(); }

    static companion::CompanionRegistry& Registry() { return companion::CompanionRegistry::Instance(); }

    /// Lets closed prompts (WA_DeleteOnClose) go
    static void Settle()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    std::string Request(const std::string& name, uint32_t scopes = 0)
    {
        const companion::PairingRequestResult result = Registry().RequestPairing(name, "panel", scopes, "10.0.0.7");
        EXPECT_EQ(result.error, "") << result.message;
        return result.pairing.id;
    }

    QWidget _window;
};

TEST_F(CompanionController_Test, ARequestOpensAPromptAndAllowPairsWithTheTickedScopes)
{
    CompanionController controller(&_window, false);
    const std::string id = Request("Desk panel", ScopeRun | ScopeInput | ScopeMedia);
    controller.handleEvent(QStringLiteral("pairing_requested"), QString::fromStdString(id), {}, {});
    CompanionPairingPrompt* prompt = controller.prompt(QString::fromStdString(id));
    ASSERT_NE(prompt, nullptr);
    EXPECT_EQ(prompt->code().toStdString(), Registry().CodeOf(id));
    ASSERT_NE(prompt->scopeBox(QStringLiteral("media")), nullptr);
    EXPECT_TRUE(prompt->scopeBox(QStringLiteral("media"))->isChecked()) << "the scopes asked for are ticked";
    EXPECT_FALSE(prompt->scopeBox(QStringLiteral("machine"))->isChecked());

    prompt->scopeBox(QStringLiteral("media"))->setChecked(false);
    prompt->allowButton()->click();
    Settle();

    const std::vector<companion::CompanionInfo> companions = Registry().List();
    ASSERT_EQ(companions.size(), 1u);
    EXPECT_EQ(companions[0].name, "Desk panel");
    EXPECT_EQ(companions[0].scopes, ScopeRun | ScopeInput) << "the user narrowed the scopes";
    EXPECT_EQ(Registry().PollPairing(id).state, "approved");
    EXPECT_EQ(controller.prompt(QString::fromStdString(id)), nullptr) << "the prompt is gone";
}

TEST_F(CompanionController_Test, DenyRefusesAndACancelledRequestTakesItsPromptAway)
{
    CompanionController controller(&_window, false);
    const std::string denied = Request("Phone");
    controller.handleEvent(QStringLiteral("pairing_requested"), QString::fromStdString(denied), {}, {});
    ASSERT_NE(controller.prompt(QString::fromStdString(denied)), nullptr);
    controller.prompt(QString::fromStdString(denied))->denyButton()->click();
    Settle();
    EXPECT_EQ(Registry().PollPairing(denied).state, "denied");
    EXPECT_TRUE(Registry().List().empty());

    const companion::PairingRequestResult other = Registry().RequestPairing("Tablet", "", 0, "10.0.0.8");
    const QString cancelled = QString::fromStdString(other.pairing.id);
    controller.handleEvent(QStringLiteral("pairing_requested"), cancelled, {}, {});
    ASSERT_NE(controller.prompt(cancelled), nullptr);
    ASSERT_TRUE(Registry().CancelPairing(other.pairing.id));
    controller.handleEvent(QStringLiteral("pairing_cancelled"), cancelled, {}, {});
    Settle();
    EXPECT_EQ(controller.prompt(cancelled), nullptr);
    EXPECT_TRUE(Registry().List().empty()) << "a dismissed prompt decides nothing";
}

TEST_F(CompanionController_Test, TheCompanionsDialogListsAndRevokes)
{
    ASSERT_EQ(Registry().AddFromSpec("wall:unc_wall-secret-0123456789;panel:unc_panel-secret-0123456789:run"), 2u);
    CompanionController controller(&_window, false);
    controller.showCompanionsDialog();
    CompanionsDialog* dialog = controller.dialog();
    ASSERT_NE(dialog, nullptr);
    ASSERT_EQ(dialog->table()->rowCount(), 2);
    EXPECT_EQ(dialog->table()->item(1, 0)->text(), QStringLiteral("panel"));
    EXPECT_EQ(dialog->table()->item(1, 2)->text(), QStringLiteral("run"));

    dialog->table()->selectRow(0);
    auto buttons = dialog->findChildren<QPushButton*>();
    QPushButton* revoke = nullptr;
    for (QPushButton* b : buttons)
        if (b->text() == QStringLiteral("Revoke"))
            revoke = b;
    ASSERT_NE(revoke, nullptr);
    revoke->click();
    EXPECT_EQ(dialog->table()->rowCount(), 1);
    EXPECT_FALSE(Registry().Authenticate("unc_wall-secret-0123456789").has_value());
    EXPECT_TRUE(Registry().Authenticate("unc_panel-secret-0123456789").has_value());
    dialog->close();
    Settle();
}

TEST_F(CompanionController_Test, UntickingAcceptPairingRefusesNewRequestsAndDeniesTheWaitingOnes)
{
    CompanionController controller(&_window, false);
    const std::string waiting = Request("Phone");
    controller.handleEvent(QStringLiteral("pairing_requested"), QString::fromStdString(waiting), {}, {});
    controller.showCompanionsDialog();
    CompanionsDialog* dialog = controller.dialog();
    ASSERT_NE(dialog, nullptr);
    ASSERT_NE(dialog->acceptPairingBox(), nullptr);
    EXPECT_TRUE(dialog->acceptPairingBox()->isChecked()) << "on by default";

    bool signalled = true;
    QObject::connect(dialog, &CompanionsDialog::pairingAllowedChanged, [&](bool allowed) { signalled = allowed; });
    dialog->acceptPairingBox()->setChecked(false);
    EXPECT_FALSE(signalled);
    EXPECT_FALSE(Registry().IsPairingAllowed());
    EXPECT_EQ(Registry().PollPairing(waiting).state, "denied") << "the waiting request is refused too";
    controller.handleEvent(QStringLiteral("denied"), QString::fromStdString(waiting), {}, {});
    Settle();
    EXPECT_EQ(controller.prompt(QString::fromStdString(waiting)), nullptr) << "and its prompt goes";

    const companion::PairingRequestResult refused = Registry().RequestPairing("Tablet", "", 0, "10.0.0.8");
    EXPECT_EQ(refused.error, "pairing_disabled");

    dialog->acceptPairingBox()->setChecked(true);
    EXPECT_TRUE(Registry().IsPairingAllowed());
    EXPECT_EQ(Registry().RequestPairing("Tablet", "", 0, "10.0.0.8").error, "");
    dialog->close();
    Settle();
}
