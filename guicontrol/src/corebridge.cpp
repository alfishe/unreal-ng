#include "guicontrol/corebridge.h"

#include "guicontrol/interfaces.h"
#include "guicontrol/route.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QThreadPool>

#include <memory>

namespace guicontrol::corebridge
{

namespace
{
Result Run(const QString& method, const QString& target, const QJsonObject& params)
{
    if (method.isEmpty())
        return Result::Failure(errors::kBadRequest, QStringLiteral("a call needs a method"));
    if (method == QLatin1String("list"))
    {
        QJsonArray hosts;
        const Result result = route::ListAll({}, hosts);
        if (!result.ok)
            return result;
        return Result::Success(QJsonObject{{"hosts", hosts}});
    }
    const bool state = method == QLatin1String("state");
    const QString wanted = state ? params.value("target").toString(target) : target;
    if (state && wanted.isEmpty())
        return Result::Failure(errors::kBadRequest, QStringLiteral("state needs a target"));
    Result failure;
    std::unique_ptr<Client> client = route::Open({}, state ? QString() : method, wanted, failure);
    if (!client)
        return failure;
    return state ? client->Call(QStringLiteral("state"), {}, QJsonObject{{"target", wanted}}, kCallTimeoutMs)
                 : client->Call(method, target, params, kCallTimeoutMs);
}

std::string Text(const Result& result)
{
    return QJsonDocument(result.ToJson()).toJson(QJsonDocument::Compact).toStdString();
}
}  // namespace

std::string CatalogueJson()
{
    return guicontrol::CatalogueJson().toStdString();
}

void Call(const std::string& method, const std::string& target, const std::string& paramsJson,
          std::function<void(const std::string& resultJson)> done)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(paramsJson), &error);
    if (!paramsJson.empty() && !document.isObject())
    {
        done(Text(Result::Failure(errors::kBadRequest, QStringLiteral("the parameters are a JSON object"))));
        return;
    }
    const QString m = QString::fromStdString(method);
    const QString t = QString::fromStdString(target);
    const QJsonObject params = document.object();
    QCoreApplication* application = QCoreApplication::instance();
    if (application == nullptr)
    {
        done(Text(Result::Failure(errors::kFailed, QStringLiteral("the GUI is not running"))));
        return;
    }
    if (QThread::currentThread() == application->thread())
    {
        done(Text(Run(m, t, params)));
        return;
    }
    QThreadPool::globalInstance()->start([m, t, params, done = std::move(done)]() { done(Text(Run(m, t, params))); });
}

}  // namespace guicontrol::corebridge
