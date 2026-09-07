/**
 * @file tapeexportaudiodialog.h
 * @brief TapeExportAudioDialog — Tape Manager "Export to audio…"
 *        (tape-audio-bridge design §7.3).
 *
 * Renders the inserted tape image (whole tape or any catalog-index block
 * range) to WAV/FLAC through the headless RenderTapeToAudio bridge. The
 * render runs on a worker thread; progress and completion marshal back via
 * queued invocations, so the UI thread never blocks — the
 * non-blocking-by-construction pattern from tape-manager §9.3.
 */

#pragma once

#include <QDialog>

#include <atomic>
#include <cstddef>
#include <thread>

#include "tapeaudio/tapeaudiorenderer.h"  // TapeRenderResult

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QSpinBox;

class TapeExportAudioDialog : public QDialog
{
    Q_OBJECT

public:
    /// `firstBlock`/`lastBlock` are catalog indices (exactly what the Tape
    /// Manager table shows) pre-filling the range; a SIZE_MAX pair (or a
    /// zero block count) means whole tape.
    explicit TapeExportAudioDialog(const QString& sourcePath, size_t blockCount,
                                   size_t firstBlock, size_t lastBlock,
                                   QWidget* parent = nullptr);

    ~TapeExportAudioDialog() override;

protected:
    /// Cancel (and the window close box) abort a running render first;
    /// only an idle dialog closes.
    void reject() override;

private slots:
    void onBrowseOutput();
    void onFormatChanged();
    void onStart();
    void onRenderProgress(size_t blocksDone, size_t blocksTotal);
    void onRenderFinished();

private:
    void setRunning(bool running);
    QString suggestedOutputPath() const;

    const QString _sourcePath;
    const size_t _blockCount;

    QSpinBox* _firstBlockSpin = nullptr;
    QSpinBox* _lastBlockSpin = nullptr;
    QComboBox* _formatCombo = nullptr;
    QComboBox* _rateCombo = nullptr;
    QDoubleSpinBox* _amplitudeSpin = nullptr;
    QCheckBox* _invertCheck = nullptr;
    QLineEdit* _outputEdit = nullptr;
    QPushButton* _browseButton = nullptr;
    QPushButton* _renderButton = nullptr;
    QPushButton* _closeButton = nullptr;  // "Cancel" while rendering, "Close" when idle
    QProgressBar* _progressBar = nullptr;
    QLabel* _statusLabel = nullptr;

    std::thread _worker;
    std::atomic<bool> _cancelRequested{false};
    TapeRenderResult _result;  // worker writes, UI reads after join (onRenderFinished)
};
