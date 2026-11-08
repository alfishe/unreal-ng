#include "guicontrol/protocol.h"

#include <QJsonDocument>

namespace guicontrol
{

Result Result::Success(QJsonObject body)
{
    Result result;
    result.body = std::move(body);
    return result;
}

Result Result::Failure(QString error, QString message, QJsonObject body)
{
    Result result;
    result.ok = false;
    result.error = std::move(error);
    result.message = std::move(message);
    result.body = std::move(body);
    return result;
}

QJsonObject Result::ToJson() const
{
    QJsonObject json{{"ok", ok}};
    if (!ok)
    {
        json["error"] = error;
        json["message"] = message;
    }
    if (!body.isEmpty())
        json["body"] = body;
    return json;
}

Result Result::FromJson(const QJsonObject& message)
{
    Result result;
    result.ok = message.value("ok").toBool();
    result.error = message.value("error").toString();
    result.message = message.value("message").toString();
    result.body = message.value("body").toObject();
    return result;
}

QByteArray Encode(const QJsonObject& message)
{
    QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

}  // namespace guicontrol
