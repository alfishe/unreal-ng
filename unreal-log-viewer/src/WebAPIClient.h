#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

class WebAPIClient : public QObject
{
    Q_OBJECT

public:
    explicit WebAPIClient(QObject* parent = nullptr);
    ~WebAPIClient() override = default;

    void setEndpoint(const QString& host, int port);
    void fetchEmulatorList();
    void enableSharedMemory(const QString& emulatorId);
    void disableSharedMemory(const QString& emulatorId);

    // Logging API
    void fetchLogging(const QString& emulatorId);
    void setLoggingLevel(const QString& emulatorId, const QString& level);
    void setLoggingModule(const QString& emulatorId, const QString& moduleName,
                          const QJsonObject& params);

signals:
    void connectionStatusChanged(bool connected);
    void emulatorListReceived(const QJsonArray& emulators);
    void sharedMemoryEnabled(const QString& emulatorId, const QString& shmName);
    void sharedMemoryDisabled(const QString& emulatorId);
    void loggingStateReceived(const QJsonObject& state);
    void loggingUpdateDone(const QString& message);
    void errorOccurred(const QString& error);

private slots:
    void onEmulatorListReply();
    void onEnableSharedMemoryReply();
    void onDisableSharedMemoryReply();

private:
    QString buildUrl(const QString& path) const;

    QNetworkAccessManager* _networkManager = nullptr;
    QString _host = "localhost";
    int _port = 8090;
};
