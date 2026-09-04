/**
 * @file tapeimportaudiodialog.cpp
 * @brief Implementation of TapeImportAudioDialog (tape-audio-bridge §7.3).
 */

#include "tapeimportaudiodialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include "loaders/tape/writer_tap.h"  // TapArchiveWriter::IsExportable (TAP gate)

TapeImportAudioDialog::TapeImportAudioDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("TapeImportAudioDialog"));
    setWindowTitle(tr("Import audio to tape"));
    resize(720, 480);

    // ---- Source picker + Preview ----
    _sourceEdit = new QLineEdit(this);
    _browseButton = new QPushButton(tr("Browse…"), this);
    connect(_browseButton, &QPushButton::clicked, this, &TapeImportAudioDialog::onBrowseSource);
    _previewButton = new QPushButton(tr("Preview"), this);
    _previewButton->setEnabled(false);
    connect(_previewButton, &QPushButton::clicked, this, &TapeImportAudioDialog::onPreview);
    connect(_sourceEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        _previewButton->setEnabled(!text.trimmed().isEmpty() && !_worker.joinable());
    });
    auto* sourceRow = new QHBoxLayout();
    sourceRow->addWidget(_sourceEdit, 1);
    sourceRow->addWidget(_browseButton);
    sourceRow->addWidget(_previewButton);

    // ---- Progress (stage-based: busy indicator) + summary + gate note ----
    _progressBar = new QProgressBar(this);
    _progressBar->setMaximum(0);
    _progressBar->setTextVisible(false);
    _progressBar->setFixedHeight(10);
    _progressBar->hide();
    _statusLabel = new QLabel(tr("Pick a WAV/FLAC/MP3 recording and press Preview."), this);
    _statusLabel->setTextFormat(Qt::PlainText);
    _statusLabel->setWordWrap(true);
    _summaryLabel = new QLabel(this);
    _summaryLabel->setTextFormat(Qt::RichText);
    _summaryLabel->setWordWrap(true);
    _summaryLabel->hide();
    _tapGateLabel = new QLabel(this);
    _tapGateLabel->setTextFormat(Qt::PlainText);
    _tapGateLabel->setWordWrap(true);
    _tapGateLabel->hide();

    // ---- Recognized-block preview: the Tape Manager's own table model ----
    _model = new TapeBlockTableModel(this);
    _table = new QTableView(this);
    _table->setModel(_model);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setAlternatingRowColors(true);
    _table->setSortingEnabled(false);  // rows must keep block-index order
    _table->verticalHeader()->hide();
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    _table->horizontalHeader()->setSectionResizeMode(TapeBlockTableModel::ColName, QHeaderView::Stretch);

    // ---- Save / insert / close ----
    _saveButton = new QPushButton(tr("Save As…"), this);
    _saveButton->setEnabled(false);
    connect(_saveButton, &QPushButton::clicked, this, &TapeImportAudioDialog::onSaveAs);
    _insertCheck = new QCheckBox(tr("Insert into emulator on close"), this);
    _insertCheck->setEnabled(false);
    _insertCheck->setToolTip(tr("Loads the saved image through the same path as File → Open Tape"));
    _closeButton = new QPushButton(tr("Close"), this);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    auto* buttonsRow = new QHBoxLayout();
    buttonsRow->addWidget(_saveButton);
    buttonsRow->addWidget(_insertCheck);
    buttonsRow->addStretch(1);
    buttonsRow->addWidget(_closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(sourceRow);
    layout->addWidget(_progressBar);
    layout->addWidget(_statusLabel);
    layout->addWidget(_summaryLabel);
    layout->addWidget(_tapGateLabel);
    layout->addWidget(_table, 1);
    layout->addLayout(buttonsRow);
}

TapeImportAudioDialog::~TapeImportAudioDialog()
{
    if (_worker.joinable())
    {
        _cancelRequested = true;
        _worker.join();
    }
}

void TapeImportAudioDialog::reject()
{
    if (_worker.joinable())
    {
        _cancelRequested = true;
        _statusLabel->setText(tr("Cancelling…"));
        return;  // onPreviewFinished resets the dialog to idle
    }
    if (_insertCheck->isEnabled() && _insertCheck->isChecked() && !_savedPath.isEmpty())
    {
        emit insertRequested(_savedPath);
    }
    QDialog::reject();
}

void TapeImportAudioDialog::onBrowseSource()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import audio"), _sourceEdit->text().trimmed(),
                                                      tr("Audio recordings (*.wav *.flac *.mp3);;All files (*)"));
    if (!path.isEmpty())
    {
        _sourceEdit->setText(path);
    }
}

void TapeImportAudioDialog::onPreview()
{
    const QString source = _sourceEdit->text().trimmed();
    if (source.isEmpty())
    {
        return;
    }

    TapeImportRequest request;
    request.sourcePath = source.toStdString();
    request.cancelRequested = [this]() { return _cancelRequested.load(); };
    request.onStage = [this](const std::string& stage) {
        QMetaObject::invokeMethod(
            this, [this, stageText = QString::fromStdString(stage)]() { onPreviewStage(stageText); },
            Qt::QueuedConnection);
    };

    setRunning(true);
    _cancelRequested = false;
    _result = TapeImportResult{};
    _savedPath.clear();
    _insertCheck->setChecked(false);
    _summaryLabel->hide();
    _tapGateLabel->hide();
    _model->Rebuild({}, TapeFastLoadPlan{});

    _worker = std::thread([this, request]() {
        _result = ImportAudioToTape(request);
        QMetaObject::invokeMethod(this, [this]() { onPreviewFinished(); }, Qt::QueuedConnection);
    });
}

void TapeImportAudioDialog::onPreviewStage(const QString& stage)
{
    if (stage == QLatin1String("decode"))
    {
        _statusLabel->setText(tr("Decoding audio…"));
    }
    else if (stage == QLatin1String("extract"))
    {
        _statusLabel->setText(tr("Extracting pulses…"));
    }
    else if (stage == QLatin1String("recognize"))
    {
        _statusLabel->setText(tr("Recognizing blocks…"));
    }
    else
    {
        _statusLabel->setText(tr("Working…"));
    }
}

void TapeImportAudioDialog::onPreviewFinished()
{
    if (_worker.joinable())
    {
        _worker.join();
    }
    setRunning(false);

    if (!_result.ok)
    {
        QMessageBox::warning(this, tr("Import audio to tape"), QString::fromStdString(_result.errorText));
        _statusLabel->setText(tr("Recognition failed — check the recording and try again."));
        return;
    }

    showResult();
}

void TapeImportAudioDialog::onSaveAs()
{
    if (!_result.ok)
    {
        return;
    }

    // TAP gate (§6.5): the gate decides which formats the dialog even offers;
    // the refusal reason stays visible under the table
    std::string reason;
    const bool tapPossible = TapArchiveWriter::IsExportable(_result.image, &reason);
    const QString filter = tapPossible ? tr("TZX tape image (*.tzx);;TAP tape image (*.tap)")
                                       : tr("TZX tape image (*.tzx)");

    const QFileInfo source(_sourceEdit->text().trimmed());
    const QString suggested = source.absolutePath() + QLatin1Char('/') + source.completeBaseName() +
                              QStringLiteral(".tzx");

    QString path = QFileDialog::getSaveFileName(this, tr("Save tape image"), suggested, filter);
    if (path.isEmpty())
    {
        return;
    }

    // The writer dispatches by extension — keep the suffix inside the offer
    const QString lowered = path.toLower();
    if (!lowered.endsWith(QStringLiteral(".tzx")) && !lowered.endsWith(QStringLiteral(".tap")))
    {
        path += QStringLiteral(".tzx");
    }
    if (path.toLower().endsWith(QStringLiteral(".tap")) && !tapPossible)
    {
        QMessageBox::warning(this, tr("Save tape image"), QString::fromStdString(reason));
        return;
    }

    const TapeSaveResult saved = SaveTapeImage(_result.image, path.toStdString());
    if (!saved.ok)
    {
        QMessageBox::warning(this, tr("Save tape image"), QString::fromStdString(saved.errorText));
        return;
    }

    _savedPath = path;
    _insertCheck->setEnabled(true);
    _statusLabel->setText(tr("Saved %1 block(s) → %2").arg(static_cast<qulonglong>(saved.blocksWritten)).arg(path));
}

void TapeImportAudioDialog::setRunning(bool running)
{
    _sourceEdit->setEnabled(!running);
    _browseButton->setEnabled(!running);
    _previewButton->setEnabled(!running && !_sourceEdit->text().trimmed().isEmpty());
    _saveButton->setEnabled(!running && _result.ok);
    _insertCheck->setEnabled(!running && !_savedPath.isEmpty());
    _closeButton->setText(running ? tr("Cancel") : tr("Close"));
    _progressBar->setVisible(running);
}

void TapeImportAudioDialog::showResult()
{
    // The recognized catalog through the Tape Manager's model — the same
    // table, the same columns, nothing importer-specific
    _model->Rebuild(_result.image.descriptors, TapeFastLoadPlan{});

    QString summary = tr("Recognized <b>%1 block(s)</b> · %2 · %3 Hz · %4 samples")
                          .arg(static_cast<qulonglong>(_result.blocksRecognized))
                          .arg(QString::fromStdString(_result.decoderUsed))
                          .arg(QString::number(_result.sampleRate))
                          .arg(static_cast<qulonglong>(_result.samplesDecoded));
    if (!_result.warnings.empty())
    {
        summary += tr("<br>%1").arg(QString::fromStdString(_result.warnings.front()).toHtmlEscaped());
        if (_result.warnings.size() > 1)
        {
            summary += tr(" (+%1 more)").arg(_result.warnings.size() - 1);
        }
    }
    _summaryLabel->setText(summary);
    _summaryLabel->show();

    // TAP gate visibility (§6.5): the reason names the block and the .tzx
    // alternative — only shown when the gate refuses
    std::string reason;
    if (TapArchiveWriter::IsExportable(_result.image, &reason))
    {
        _tapGateLabel->hide();
    }
    else
    {
        _tapGateLabel->setText(tr("TAP not possible: %1").arg(QString::fromStdString(reason)));
        _tapGateLabel->setStyleSheet(QStringLiteral("color: #b07800;"));
        _tapGateLabel->show();
    }

    _statusLabel->setText(tr("Recognition ready — Save As… writes the tape image."));
    _saveButton->setEnabled(true);
}
