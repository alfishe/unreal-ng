#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"
#include "debugger/targets/debugcpuid.h"

class EmulatorContext;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
struct GSSendRequest;

/// @file gssendpane.h
/// @brief The General Sound send console (gs-debugger P5.6, gui-card-debugger.md A5.3 "Send console", C11): a card side
/// tab of the debugger ("GS send"), not a dialog. Top to bottom:
///   - the command: a picker over the card profile's command set (`gs-firmware` object `commands`: "#2A Set module
///     master volume"), or any byte typed as #NN / $NN / 0xNN / decimal;
///   - its parameters, one field each, named and typed by the profile (pre byte first; a command with a variable count
///     takes its bytes in one field), the replies to read (empty: the profile's count), the command's note;
///   - Send: GSSend (the host driver every plane drives: `gs send`), never waiting on the GUI thread; the bytes go in as
///     live input through the card's mailbox, so the command log marks them `automation` and TTD records them;
///   - the sends of this tab, newest first: state, the command with its parameters, the replies decoded by the profile.
///     A send queued on a paused machine runs when it runs.
/// The ZX-MultiSound's card has no send (the planes address the GS slot's card): the tab says so. No device code here:
/// the reads and the send are the verbs every plane serves. The pane never reads by itself: its owner calls refresh and
/// send with a context it holds a lease on.
class GsSendPane : public QWidget
{
    Q_OBJECT

public:
    /// The sends the table keeps (the host driver keeps its last 16)
    static constexpr int kMaxRows = 16;

    /// `follow`: the debugger CPU tab in front (the card it shows: CardPanes::Front); null: the GS slot's
    explicit GsSendPane(std::shared_ptr<DebugCpuId> follow = {}, QWidget* parent = nullptr);

    /// Read the card's command set (when its profile changed) and the state of the sends not finished; `live`: the
    /// machine runs (a queued send waits for it otherwise)
    void refresh(EmulatorContext* context, bool live);
    /// Send what the form holds on `context`; false with `error` when the form or the driver refused it
    bool send(EmulatorContext* context, QString* error = nullptr);

    /// The form as a request: false with `error` naming the field that does not parse
    bool request(GSSendRequest& out, QString& error) const;
    /// Pick a command by its byte (the picker's entry, or the byte typed when the profile does not name it)
    void selectCommand(int command);
    /// Set the parameter field `index` (pre first) / the replies field
    void setParam(int index, const QString& text);
    void setReplies(const QString& text);

    /// A byte as the debugger's conditions write it: decimal digits, #NN, $NN, 0xNN
    static std::optional<int> ParseByte(const QString& text);

    /// region <For the tests>
    QString status() const;
    QString profileId() const { return QString::fromStdString(_profile); }
    int commandCount() const { return static_cast<int>(_commands.size()); }
    int paramCount() const { return static_cast<int>(_params.size()); }
    QString paramLabel(int index) const;
    QString noteText() const;
    QString rowsText() const;
    QString lastError() const { return _error; }
    QComboBox* picker() const { return _picker; }
    QPushButton* sendButton() const { return _send; }
    QTableWidget* table() const { return _table; }
    /// endregion

signals:
    /// The Send button: the owner calls send under its lease
    void sendRequested();

private:
    struct Sent
    {
        uint32_t id = 0;
        StateNode conversation;  ///< GSConversation::ToState as last read
        QString text;            ///< GSConversation::Text: the command, its parameters, the replies decoded
        bool finished = false;
    };

    void showUnavailable(const QString& why);
    void loadCommands(const StateNode& commands);
    void rebuildForm();
    const StateNode* current() const;  ///< the picked command's entry, null for a byte the profile does not name
    void showRows();

    std::shared_ptr<DebugCpuId> _follow;
    QLabel* _status = nullptr;
    QWidget* _body = nullptr;
    QComboBox* _picker = nullptr;
    QWidget* _form = nullptr;
    QFormLayout* _formLayout = nullptr;
    std::vector<QLineEdit*> _params;
    QLineEdit* _more = nullptr;     ///< the bytes of a variable count (space separated)
    QLineEdit* _replies = nullptr;
    QLabel* _note = nullptr;
    QPushButton* _send = nullptr;
    QLabel* _hint = nullptr;
    QTableWidget* _table = nullptr;

    bool _live = false;
    std::string _profile;            ///< the profile the picker lists
    std::vector<StateNode> _commands;  ///< its `commands` entries
    std::vector<Sent> _sent;           ///< newest last
    QString _error;
    int _formCommand = -2;  ///< the command the form was built for (-1: a byte the profile does not name)
};
