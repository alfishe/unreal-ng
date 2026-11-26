// unreal-debugger (not in ecat): the GS send tab (gui-card-debugger A5.3 "Send console", C11; gs-debugger P5.6), a
// card tab right after GS monitor, not a dialog; the monitor's Send... and Ctrl+Shift+S bring it to the front. Top to
// bottom:
//   - the command: a picker over the command set of the profile decoding the card ("#2A Set module master volume"),
//     editable so any byte goes as #NN, $NN, 0xNN or decimal;
//   - a field per parameter named by the profile (a pre byte "volume (before)"), a hint from its kind; "More bytes"
//     for a variable count or a byte the profile does not name; Replies (empty: the profile's count); the command's
//     note (what the replies mean, a transfer it starts, not in the programming guide);
//   - Send: the host driver's send (Emulator::gs_send: GSSend in process, POST control/audio/gs {action: send} over
//     the WebAPI), never waited for: the tab reads how far each send got (gs_conversation) at the window's stops and,
//     while in sight and the machine runs, every Emulator::live_card_ms. A send queued on a paused machine waits;
//   - the sends of this tab, newest first: id, state, the command with its parameters and the replies decoded.
// The ZX-MultiSound's card has no send (the planes address the GS slot's card): the tab says so

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "gsanalysis.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class GsSendView : public GsCardTab
{
    Q_OBJECT
public:
    //The sends the table keeps (the host driver keeps its last 16)
    static constexpr int kMaxRows = 16;
    enum { ColId = 0, ColState, ColText, ColCount };

    explicit GsSendView(Emulator * e, QWidget *parent);
    //The card's command set (read again when its profile changes) and the sends not finished
    void refresh() override;
    //Sends what the form holds; false with `error` (and the hint) when the form or the card refused it
    bool send(QString * error = nullptr);

    //A request as the form holds it
    struct Request {
        int command = 0;
        std::optional<int> pre;
        std::vector<int> params;
        std::optional<int> replies;
    };
    //The form as a request: false with `error` naming the field that does not parse
    bool request(Request &out, QString &error) const;
    //Picks a command by its byte (the picker's entry, or the byte typed when the profile does not name it)
    void select_command(int command);
    //The parameter field `index` (pre first; past them the More bytes field) / the replies field
    void set_param(int index, const QString &text);
    void set_replies(const QString &text);
    //A byte as the debugger's conditions write it: decimal digits, #NN, $NN, 0xNN
    static std::optional<int> parse_byte(const QString &text);

    //For the self-check
    QComboBox * picker() const { return m_picker; }
    QPushButton * send_button() const { return m_send; }
    QTableWidget * table() const { return m_table; }
    QWidget * body() const { return m_body; }
    QLabel * note() const { return m_note; }
    QLabel * hint() const { return m_hint; }
    const std::string &profile() const { return m_profile; }
    int command_count() const { return (int)m_commands.size(); }
    int param_count() const { return (int)m_params.size(); }
    QString param_label(int index) const;
    QString last_error() const { return m_error; }
    //The state of the newest send ("" none)
    QString newest_state() const;

protected:
    void restart() override;

private:
    struct Sent {
        uint32_t id = 0;
        QString state;     //queued, running, done, failed; "gone" when the driver no longer has it
        QString text;      //the conversation's line
        bool finished = false;
    };
    void show_unavailable(const QString &why);
    void load_commands(const QJsonArray &commands);
    void rebuild_form();
    //The picked command's entry; empty for a byte the profile does not name (or no byte)
    QJsonObject current() const;
    void take(Sent &s, const Emulator::GsJson &answer);
    void show_rows();

    QWidget * m_body;
    QComboBox * m_picker;
    QWidget * m_form;
    QFormLayout * m_form_layout;
    std::vector<QLineEdit *> m_params;
    QLineEdit * m_more = nullptr;     //the bytes of a variable count (space separated)
    QLineEdit * m_replies = nullptr;
    QLabel * m_note;
    QPushButton * m_send;
    QLabel * m_hint;
    QTableWidget * m_table;

    std::string m_profile;            //the profile the picker lists
    bool m_loaded = false;            //the command set was read once for this card
    std::vector<QJsonObject> m_commands;
    std::vector<Sent> m_sent;         //newest last
    QString m_error;
    int m_form_command = -2;          //the command the form was built for (-1: a byte the profile does not name)
};
