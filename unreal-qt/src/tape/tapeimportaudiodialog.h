/**
 * @file tapeimportaudiodialog.h
 * @brief TapeImportAudioDialog — File → Import Audio to Tape
 *        (tape-audio-bridge design §7.3).
 *
 * WAV/FLAC/MP3 in, tape image out. Preview runs the full recognition
 * pipeline on a worker thread and renders the recognized catalog through
 * the same TapeBlockTableModel the Tape Manager uses; Save As dispatches to
 * the production writers behind the TAP export gate; "Insert into emulator"
 * emits a path MainWindow loads through the ordinary LoadTape flow.
 */

#pragma once

#include <QDialog>

#include <atomic>
#include <thread>

#include "tape/tapeblocktablemodel.h"
#include "tapeaudio/tapeaudioimporter.h"  // TapeImportResult

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QTableView;

class TapeImportAudioDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TapeImportAudioDialog(QWidget* parent = nullptr);
    ~TapeImportAudioDialog() override;

signals:
    /// "Insert into emulator on close": the saved image path, to be loaded
    /// exactly like File → Open Tape.
    void insertRequested(const QString& tapePath);

protected:
    /// Aborts a running preview first; an idle dialog emits-and-closes.
    void reject() override;

private slots:
    void onBrowseSource();
    void onPreview();
    void onPreviewStage(const QString& stage);
    void onPreviewFinished();
    void onSaveAs();

private:
    void setRunning(bool running);
    void showResult();

    QLineEdit* _sourceEdit = nullptr;
    QPushButton* _browseButton = nullptr;
    QPushButton* _previewButton = nullptr;
    QPushButton* _saveButton = nullptr;
    QCheckBox* _insertCheck = nullptr;
    QPushButton* _closeButton = nullptr;
    QProgressBar* _progressBar = nullptr;
    QLabel* _statusLabel = nullptr;
    QLabel* _summaryLabel = nullptr;
    QLabel* _tapGateLabel = nullptr;
    QTableView* _table = nullptr;
    TapeBlockTableModel* _model = nullptr;

    std::thread _worker;
    std::atomic<bool> _cancelRequested{false};
    TapeImportResult _result;  // worker writes, UI reads after join (onPreviewFinished)
    QString _savedPath;
};
