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
    qDebug() << "WebAPIClient: Endpoint set to" << host << ":" << port;
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
        qDebug() << "WebAPIClient: Invalid JSON response";
        emit connectionStatusChanged(false);
        return;
    }

    QJsonObject obj = doc.object();
    
    // Check for error response
    if (obj.contains("error"))
    {
        qDebug() << "WebAPIClient: API error:" << obj.value("error").toString();
        emit connectionStatusChanged(false);
        emit errorOccurred(obj.value("message").toString());
        return;
    }

    // Successful response has "emulators" array
    if (!obj.contains("emulators"))
    {
        qDebug() << "WebAPIClient: Unexpected response format";
        emit connectionStatusChanged(false);
        return;
    }

    emit connectionStatusChanged(true);

    QJsonArray emulators = obj.value("emulators").toArray();
    emit emulatorListReceived(emulators);
}

void WebAPIClient::onEnableSharedMemoryReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QString emulatorId = reply->property("emulatorId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        qDebug() << "WebAPIClient: Failed to enable shared memory:" << reply->errorString();
        emit errorOccurred(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();

    // Check for error response
    if (obj.contains("error"))
    {
        qDebug() << "WebAPIClient: API error enabling shared memory:" << obj.value("error").toString();
        emit errorOccurred(obj.value("message").toString());
        return;
    }

    // Successful response has "enabled" field
    if (!obj.value("enabled").toBool(false))
    {
        qDebug() << "WebAPIClient: Shared memory was not enabled";
        return;
    }

    // Construct monitoring shared memory name from emulator ID
    // The MonitoringManager uses the last 12 characters of the UUID for the SHM name
    // Format: /unreal_monitor_{short_id} (POSIX) or Local\\unreal_monitor_{short_id} (Windows)
    QString shortId = emulatorId.right(12).remove('-');
    
#ifdef _WIN32
    QString shmName = QString("Local\\unreal_monitor_%1").arg(shortId);
#else
    QString shmName = QString("/unreal_monitor_%1").arg(shortId);
#endif
    
    // Size is determined by MonitoringManager (manifest header describes the layout)
    // Pass 0 — ScreenViewer will read the actual size from the SHM region
    qint64 shmSize = 0;

    qDebug() << "WebAPIClient: Shared memory enabled for" << emulatorId << "at" << shmName;
    emit sharedMemoryEnabled(emulatorId, shmName, shmSize);
}

void WebAPIClient::onDisableSharedMemoryReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QString emulatorId = reply->property("emulatorId").toString();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        qDebug() << "WebAPIClient: Failed to disable shared memory:" << reply->errorString();
        return;
    }

    qDebug() << "WebAPIClient: Shared memory disabled for" << emulatorId;
    emit sharedMemoryDisabled(emulatorId);
}
