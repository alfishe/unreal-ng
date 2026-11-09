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

namespace params
{

bool Address(const QJsonObject& params, const char* name, quint16& address, Result& failure)
{
    const QJsonValue value = params.value(QLatin1String(name));
    qint64 number = -1;
    if (value.isDouble())
    {
        const double d = value.toDouble();
        if (d == static_cast<double>(static_cast<qint64>(d)))
            number = static_cast<qint64>(d);
    }
    else if (value.isString())
    {
        QString text = value.toString().trimmed();
        int base = 10;
        if (text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        {
            text = text.mid(2);
            base = 16;
        }
        else if (text.startsWith('#') || text.startsWith('$'))
        {
            text = text.mid(1);
            base = 16;
        }
        else if (text.size() > 1 && text.endsWith('h', Qt::CaseInsensitive))
        {
            text.chop(1);
            base = 16;
        }
        bool ok = false;
        const qint64 parsed = text.toLongLong(&ok, base);
        if (ok && !text.isEmpty())
            number = parsed;
    }
    if (number < 0 || number > 0xFFFF)
    {
        failure = Result::Failure(errors::kBadRequest,
                                  value.isUndefined()
                                      ? QStringLiteral("%1: an address is needed").arg(QLatin1String(name))
                                      : QStringLiteral("%1: an address 0..#FFFF (decimal, 0x.., #.., $.., ..h)")
                                            .arg(QLatin1String(name)));
        return false;
    }
    address = static_cast<quint16>(number);
    return true;
}

bool Direction(const QJsonObject& params, int& dx, int& dy, Result& failure)
{
    const QString dir = params.value(QStringLiteral("dir")).toString();
    dx = dy = 0;
    if (dir == QLatin1String("up"))
        dy = -1;
    else if (dir == QLatin1String("down"))
        dy = 1;
    else if (dir == QLatin1String("left"))
        dx = -1;
    else if (dir == QLatin1String("right"))
        dx = 1;
    else
    {
        failure = Result::Failure(errors::kBadRequest, QStringLiteral("dir: up, down, left or right"));
        return false;
    }
    return true;
}

bool Pages(const QJsonObject& params, int& pages, Result& failure)
{
    const QJsonValue value = params.value(QStringLiteral("n"));
    if (value.isUndefined())
    {
        pages = 1;
        return true;
    }
    const double d = value.toDouble(0);
    if (!value.isDouble() || d != static_cast<double>(static_cast<int>(d)) || d == 0 || d < -256 || d > 256)
    {
        failure = Result::Failure(errors::kBadRequest, QStringLiteral("n: pages, a whole number -256..256 but 0"));
        return false;
    }
    pages = static_cast<int>(d);
    return true;
}

bool Toggle(const QJsonObject& params, bool current, bool& on, Result& failure)
{
    const QJsonValue value = params.value(QStringLiteral("on"));
    if (value.isUndefined() || value.isNull())
        on = !current;
    else if (value.isBool())
        on = value.toBool();
    // As text too: a client without the command's declaration (the CLI before a program's own commands) sends words
    else if (const QString word = value.toString().toLower(); word == "true" || word == "on" || word == "1")
        on = true;
    else if (word == "false" || word == "off" || word == "0")
        on = false;
    else
    {
        failure = Result::Failure(errors::kBadRequest, QStringLiteral("on: true or false (absent: flip)"));
        return false;
    }
    return true;
}

QString Hex(quint16 address)
{
    return QStringLiteral("0x%1").arg(address, 4, 16, QChar('0')).toUpper().replace(QLatin1String("0X"),
                                                                                     QLatin1String("0x"));
}

}  // namespace params

}  // namespace guicontrol
