#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

/**
 * @brief HTTP client for communicating with Unreal-NG WebAPI
 * 
 * Handles emulator discovery and shared memory feature toggling.
 */
class WebAPIClient : public QObject
{
    Q_OBJECT

public:
    explicit WebAPIClient(QObject* parent = nullptr);
    ~WebAPIClient() override = default;

    /// Set the WebAPI endpoint (host and port)
    void setEndpoint(const QString& host, int port);

    /// Fetch list of all emulator instances
    void fetchEmulatorList();

    /// Enable shared memory feature for an emulator
    void enableSharedMemory(const QString& emulatorId);

    /// Disable shared memory feature for an emulator
    void disableSharedMemory(const QString& emulatorId);

signals:
    /// Emitted when connection status changes
    void connectionStatusChanged(bool connected);

    /// Emitted when emulator list is received
    /// @param emulators JSON array of emulator objects
    void emulatorListReceived(const QJsonArray& emulators);

    /// Emitted when shared memory is enabled for an emulator
    /// @param emulatorId The emulator ID
    /// @param shmName The shared memory region name
    /// @param shmSize The shared memory region size
    void sharedMemoryEnabled(const QString& emulatorId, const QString& shmName, qint64 shmSize);

    /// Emitted when shared memory is disabled for an emulator
    void sharedMemoryDisabled(const QString& emulatorId);

    /// Emitted on API error
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
