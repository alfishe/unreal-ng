#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>

class Emulator;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;

/// The asm-synchronizer's "Live source" window (asm-synchronizer.md §8, phase Y2): the source an assembler running in
/// the machine holds in RAM, as the last host build decoded it, with the guest's cursor line highlighted, the build's
/// errors and warnings on their lines and in a list, and a status line (assembler, build, labels, hints). It shows the
/// watch of AsmSyncService and starts one when none runs (stopping it again on close); Extract... and Convert... go
/// through AsmControl's sync-extract, as on the other surfaces (.recipe/analysis/asm-sources.md). The other way (part
/// B, Z2): Inject... writes a host source into the assembler (sync-inject), Push follows a host file and writes each
/// save (sync-push-watch; stopped on close when this window started it)
class LiveSourceWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LiveSourceWindow(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    ~LiveSourceWindow() override;

    /// Another instance chosen in the debugger: the watch this window started stays with the old one
    void setEmulator(std::shared_ptr<Emulator> emulator);

public slots:
    /// One look at the watch: the status every time, the text and hints when a new build is there
    void refresh();

private slots:
    void toggleWatch(bool on);
    void inject();
    void togglePush(bool on);
    void extract();
    void convert();
    void goToHint();

private:
    /// The instance (looked up by id: a destroyed one is gone, not dangling), its context and its watch. The context
    /// is valid while the caller holds the shared_ptr: looked up for one handler, never kept, never across a dialog
    std::shared_ptr<Emulator> instance() const;
    static struct EmulatorContext* context(const std::shared_ptr<Emulator>& emulator);
    static class AsmSyncService* service(const std::shared_ptr<Emulator>& emulator);
    void startWatch();
    void stopOwnWatch();
    void stopOwnPush();
    void highlight(int cursorLine);

    std::string _emulatorId;
    bool _ownWatch = false;                 ///< this window started the watch (and stops it)
    bool _ownPush = false;                  ///< this window started the push watch (and stops it)
    uint64_t _shownGeneration = 0;
    int _shownCursor = -2;
    QLabel* _status = nullptr;
    QPlainTextEdit* _text = nullptr;
    QListWidget* _hints = nullptr;
    QPushButton* _watch = nullptr;
    QPushButton* _inject = nullptr;
    QPushButton* _push = nullptr;
    QPushButton* _extract = nullptr;
    QPushButton* _convert = nullptr;
    QTimer* _timer = nullptr;
    QList<QPair<int, QString>> _hintLines;  ///< 1-based source line, severity ("error" / "warning")
    int _errors = 0;                        ///< of the shown build, with or without a line
    int _warnings = 0;
};
