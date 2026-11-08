#pragma once

/// @file protocol.h
/// @brief The GUI control plane's wire format (docs/inprogress/2026-11-07-gui-control-plane §4.4): newline-delimited
/// JSON in the core WebSocket plane's envelope (`op`, `rid`, `call` / `result` / `error`, `subscribe` / `event`,
/// `auth`), where a `call` names a command and a `result` carries `ok` instead of an HTTP status.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace guicontrol
{

/// Changes only on an incompatible change (`welcome.protocol`)
inline constexpr int kProtocolVersion = 1;

/// The error codes of a `result`
namespace errors
{
inline constexpr const char* kNotFound = "not_found";
inline constexpr const char* kBadRequest = "bad_request";
inline constexpr const char* kAmbiguous = "ambiguous";
inline constexpr const char* kNotNow = "not_now";
inline constexpr const char* kUnauthorized = "unauthorized";  ///< reserved for a later token
inline constexpr const char* kFailed = "failed";
inline constexpr const char* kTimeout = "timeout";  ///< the client's own: no answer in time
}  // namespace errors

/// The topics a client subscribes to
namespace topics
{
inline constexpr const char* kState = "state";      ///< a node's state, latest value, coalesced to max_hz
inline constexpr const char* kNodes = "nodes";      ///< windows and views appearing and going
inline constexpr const char* kFocus = "focus";      ///< per interface: the focused node and when it took focus
inline constexpr const char* kActivity = "activity";  ///< who ran which command
}  // namespace topics

/// What a command answers
struct Result
{
    bool ok = true;
    QString error;    ///< "" on success, else one of `errors`
    QString message;  ///< one sentence a user can act on
    QJsonObject body;

    static Result Success(QJsonObject body = {});
    static Result Failure(QString error, QString message, QJsonObject body = {});

    /// `{"ok":…, "error":…, "message":…, "body":…}`: the fields of a `result` message without `op` / `rid`
    QJsonObject ToJson() const;
    static Result FromJson(const QJsonObject& message);
};

/// One command as its handler sees it
struct Call
{
    QString method;  ///< "window.move"
    QJsonObject params;
    QString client;  ///< who asks: "unreal-gui", "bindings", a peer address
};

/// Answers a call; may be called later (an async handler), always on the host's thread
using Done = std::function<void(Result result)>;

/// One message as one line (compact JSON and '\n')
QByteArray Encode(const QJsonObject& message);

/// region <Parameters of a call>
namespace params
{
/// An address: a number, or text in decimal, 0x.., #.., $.. or ..h (as the core's AddressParam); 0..#FFFF
bool Address(const QJsonObject& params, const char* name, quint16& address, Result& failure);
/// `dir`: up, down, left, right as a step (dx, dy)
bool Direction(const QJsonObject& params, int& dx, int& dy, Result& failure);
/// `n` pages, signed, default 1
bool Pages(const QJsonObject& params, int& pages, Result& failure);
/// A toggle's `on`: absent = flip `current`
bool Toggle(const QJsonObject& params, bool current, bool& on, Result& failure);
/// `name` as hex text: "0x6A00"
QString Hex(quint16 address);
}  // namespace params
/// endregion </Parameters of a call>

}  // namespace guicontrol
