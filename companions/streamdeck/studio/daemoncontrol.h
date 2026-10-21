// The studio's side of the daemon's automation (unreal-streamdeck run --api 37053, sdb/daemondecks.h): the deck
// list, brightness and a scenario per deck; and the daemon process when the studio starts one (no daemon answers at
// launch, or the editor's Run finds none). The studio is a client: it never opens USB, the daemon owns the decks.
// Calls are short blocking HTTP requests to 127.0.0.1 on the caller's thread (the GUI's).
#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <nlohmann/json.hpp>
#include <string>

class DaemonControl : public QObject
{
    Q_OBJECT
public:
    static constexpr int kApiPort = 37053;  // the daemon's automation

    explicit DaemonControl(QObject* parent = nullptr);
    ~DaemonControl() override;

    void SetApiPort(int port)
    {
        _apiPort = port;
    }
    [[nodiscard]] int ApiPort() const
    {
        return _apiPort;
    }
    [[nodiscard]] QString ApiUrl() const;
    // The daemon executable: $UNREAL_STREAMDECK_BIN, beside the studio (or its bundle, or the suite's bundle), PATH.
    [[nodiscard]] static QString Path();

    struct Result
    {
        int status = 0;  // 0 = no answer (error says why)
        nlohmann::json body;
        QString error;
        [[nodiscard]] bool Ok() const
        {
            return status >= 200 && status < 300;
        }
    };
    Result Call(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr,
                int timeoutMs = 1500) const;
    // GET /api/v1/health answers.
    [[nodiscard]] bool Answers(int timeoutMs = 400) const;

    // Per deck ("first" = the first real deck). Errors are for people.
    [[nodiscard]] nlohmann::json Decks(QString* error = nullptr) const;
    bool Brightness(const QString& serial, int* percent, QString* error = nullptr) const;
    bool SetBrightness(const QString& serial, int percent, QString* error = nullptr) const;
    // The deck plays `file` (watched: edits reload it); Restore gives it the daemon's start scenario back.
    bool PlayScenario(const QString& serial, const QString& file, QString* error = nullptr) const;
    bool RestoreScenario(const QString& serial, QString* error = nullptr) const;

    // The daemon process this studio started: `Path() args`. False when it cannot start or ends at once (no deck
    // without --wait, a bad scenario, a taken port); the error then carries its output. checkEarlyExit false: no
    // wait for that (the GUI is not held; an early end shows in Output and StateChanged).
    bool Launch(const QStringList& args, QString* error, bool checkEarlyExit = true);
    void Terminate();  // ends the process this studio started (none: nothing)
    [[nodiscard]] bool Launched() const
    {
        return _proc.state() != QProcess::NotRunning;
    }
    [[nodiscard]] QStringList Output() const
    {
        return _output;
    }

signals:
    void StateChanged();
    void OutputLine(const QString& line);

private:
    void Line(const QString& line);

    int _apiPort = kApiPort;
    QProcess _proc;
    QStringList _output;
};
