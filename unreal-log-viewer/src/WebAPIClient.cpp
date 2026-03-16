#include "WebAPIClient.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

WebAPIClient::WebAPIClient(QObject* parent)
    : QObject(parent)
    , _networkManager(new QNetworkAccessManager(this))
{
}

void WebAPIClient::setEndpoint(const QString& host, int port)
{
    _host = host;
    _port = port;
}

QString WebAPIClient::buildUrl(const QString& path) const
{
    return QString("http://%1:%2%3").arg(_host).arg(_port).arg(path);
}

void WebAPIClient::fetchEmulatorList()
{
    QNetworkRequest request(QUrl(buildUrl("/api/v1/emulator")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = _networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &WebAPIClient::onEmulatorListReply);
}

void WebAPIClient::enableSharedMemory(const QString& emulatorId)
{
    QString path = QString("/api/v1/emulator/%1/feature/sharedmemory").arg(emulatorId);
    QNetworkRequest request(QUrl(buildUrl(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["enabled"] = true;

    QNetworkReply* reply = _networkManager->put(request, QJsonDocument(body).toJson());
    reply->setProperty("emulatorId", emulatorId);
    connect(reply, &QNetworkReply::finished, this, &WebAPIClient::onEnableSharedMemoryReply);
}

void WebAPIClient::disableSharedMemory(const QString& emulatorId)
{
    QString path = QString("/api/v1/emulator/%1/feature/sharedmemory").arg(emulatorId);
    QNetworkRequest request(QUrl(buildUrl(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["enabled"] = false;

    QNetworkReply* reply = _networkManager->put(request, QJsonDocument(body).toJson());
    reply->setProperty("emulatorId", emulatorId);
    connect(reply, &QNetworkReply::finished, this, &WebAPIClient::onDisableSharedMemoryReply);
}

void WebAPIClient::onEmulatorListReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        emit connectionStatusChanged(false);
        emit errorOccurred(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isObject())
    {
        emit connectionStatusChanged(false);
        return;
    }

    QJsonObject obj = doc.object();

    if (obj.contains("error"))
    {
        emit connectionStatusChanged(false);
        emit errorOccurred(obj.value("message").toString());
        return;
    }

    if (!obj.contains("emulators"))
    {
        emit connectionStatusChanged(false);
        return;
    }

    emit connectionStatusChanged(true);
    emit emulatorListReceived(obj.value("emulators").toArray());
}

void WebAPIClient::onEnableSharedMemoryReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QString emulatorId = reply->property("emulatorId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        emit errorOccurred(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    if (obj.contains("error"))
    {
        emit errorOccurred(obj.value("message").toString());
        return;
    }

    if (!obj.value("enabled").toBool(false))
        return;

    // Derive SHM name: /unreal_monitor_{last 12 chars of UUID}
    QString clean = emulatorId;
    clean.remove('-');
    QString shortId = clean.right(12);

#ifdef _WIN32
    QString shmName = QString("unreal_monitor_%1").arg(shortId);
#else
    QString shmName = QString("/unreal_monitor_%1").arg(shortId);
#endif

    emit sharedMemoryEnabled(emulatorId, shmName);
}

void WebAPIClient::onDisableSharedMemoryReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QString emulatorId = reply->property("emulatorId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
        return;

    emit sharedMemoryDisabled(emulatorId);
}

// === Logging API ===

void WebAPIClient::fetchLogging(const QString& emulatorId)
{
    QString path = QString("/api/v1/emulator/%1/logging").arg(emulatorId);
    QNetworkRequest request(QUrl(buildUrl(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = _networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred(reply->errorString());
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            emit loggingStateReceived(doc.object());
    });
}

void WebAPIClient::setLoggingLevel(const QString& emulatorId, const QString& level)
{
    QString path = QString("/api/v1/emulator/%1/logging/level").arg(emulatorId);
    QNetworkRequest request(QUrl(buildUrl(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["level"] = level;
    QNetworkReply* reply = _networkManager->put(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred(reply->errorString());
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            emit loggingUpdateDone(doc.object().value("message").toString());
    });
}

void WebAPIClient::setLoggingModule(const QString& emulatorId, const QString& moduleName,
                                     const QJsonObject& params)
{
    QString path = QString("/api/v1/emulator/%1/logging/module/%2").arg(emulatorId, moduleName);
    QNetworkRequest request(QUrl(buildUrl(path)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = _networkManager->put(request, QJsonDocument(params).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred(reply->errorString());
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isObject())
            emit loggingUpdateDone(doc.object().value("message").toString());
    });
}
