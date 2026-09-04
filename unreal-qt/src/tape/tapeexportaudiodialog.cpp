/**
 * @file tapeexportaudiodialog.cpp
 * @brief Implementation of TapeExportAudioDialog (tape-audio-bridge §7.3).
 */

#include "tapeexportaudiodialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace
{
constexpr int FORMAT_WAV = 0;
constexpr int FORMAT_FLAC = 1;

QString SuffixForFormat(int formatIndex)
{
    return formatIndex == FORMAT_FLAC ? QStringLiteral("flac") : QStringLiteral("wav");
}
}  // namespace

TapeExportAudioDialog::TapeExportAudioDialog(const QString& sourcePath, size_t blockCount,
                                             size_t firstBlock, size_t lastBlock, QWidget* parent)
    : QDialog(parent), _sourcePath(sourcePath), _blockCount(blockCount)
{
    setObjectName(QStringLiteral("TapeExportAudioDialog"));
    setWindowTitle(tr("Export to audio — %1").arg(QFileInfo(_sourcePath).fileName()));

    // ---- Range: pre-filled from the Tape Manager selection, whole tape default ----
    const int maxIndex = _blockCount > 0 ? static_cast<int>(_blockCount - 1) : 0;
    const bool hasSelection = firstBlock != SIZE_MAX && lastBlock != SIZE_MAX && _blockCount > 0;
    const auto clampIndex = [maxIndex](size_t value) {
        return static_cast<int>(value > static_cast<size_t>(maxIndex) ? static_cast<size_t>(maxIndex) : value);
    };

    _firstBlockSpin = new QSpinBox(this);
    _firstBlockSpin->setRange(0, maxIndex);
    _firstBlockSpin->setValue(hasSelection ? clampIndex(firstBlock) : 0);
    _lastBlockSpin = new QSpinBox(this);
    _lastBlockSpin->setRange(0, maxIndex);
    _lastBlockSpin->setValue(hasSelection ? clampIndex(lastBlock) : maxIndex);

    auto* rangeRow = new QHBoxLayout();
    rangeRow->addWidget(_firstBlockSpin);
    rangeRow->addWidget(new QLabel(tr("to"), this));
    rangeRow->addWidget(_lastBlockSpin);
    rangeRow->addStretch(1);

    // ---- Format: WAV native, FLAC behind ffmpeg availability (§7.3) ----
    _formatCombo = new QComboBox(this);
    _formatCombo->addItem(tr("WAV"));
    _formatCombo->addItem(tr("FLAC"));
    if (!IsFlacRenderAvailable())
    {
        // §7.3: disable the choice with an explanatory tooltip instead of
        // letting the render fail after the fact
        if (auto* model = qobject_cast<QStandardItemModel*>(_formatCombo->model()))
        {
            if (QStandardItem* flacItem = model->item(FORMAT_FLAC))
            {
                flacItem->setEnabled(false);
                flacItem->setToolTip(tr("ffmpeg was not found on this system — export WAV instead"));
            }
        }
        _formatCombo->setToolTip(tr("ffmpeg was not found on this system — FLAC is unavailable"));
    }
    connect(_formatCombo, &QComboBox::currentIndexChanged, this, &TapeExportAudioDialog::onFormatChanged);

    _rateCombo = new QComboBox(this);
    _rateCombo->addItem(QStringLiteral("44100 Hz"), QVariant(44100));
    _rateCombo->addItem(QStringLiteral("48000 Hz"), QVariant(48000));
    _rateCombo->addItem(QStringLiteral("96000 Hz"), QVariant(96000));
    _rateCombo->setCurrentIndex(0);  // community standard default

    _amplitudeSpin = new QDoubleSpinBox(this);
    _amplitudeSpin->setRange(0.01, 1.0);
    _amplitudeSpin->setSingleStep(0.05);
    _amplitudeSpin->setDecimals(2);
    _amplitudeSpin->setValue(0.8);

    _invertCheck = new QCheckBox(tr("Invert polarity"), this);
    _invertCheck->setToolTip(tr("Flip the square-wave start level (captures that came out AC-coupled the other way)"));

    auto* form = new QFormLayout();
    form->addRow(tr("Source"), new QLabel(QFileInfo(_sourcePath).fileName(), this));
    form->addRow(tr("Blocks"), rangeRow);
    form->addRow(tr("Format"), _formatCombo);
    form->addRow(tr("Sample rate"), _rateCombo);
    form->addRow(tr("Amplitude"), _amplitudeSpin);
    form->addRow(QString(), _invertCheck);

    // ---- Output path ----
    _outputEdit = new QLineEdit(suggestedOutputPath(), this);
    _browseButton = new QPushButton(tr("Browse…"), this);
    connect(_browseButton, &QPushButton::clicked, this, &TapeExportAudioDialog::onBrowseOutput);
    auto* outputRow = new QHBoxLayout();
    outputRow->addWidget(_outputEdit, 1);
    outputRow->addWidget(_browseButton);
    form->addRow(tr("Output file"), outputRow);

    // ---- Progress + buttons ----
    _progressBar = new QProgressBar(this);
    _progressBar->setMaximum(100);
    _progressBar->setTextVisible(false);
    _progressBar->setFixedHeight(10);
    _statusLabel = new QLabel(this);
    _statusLabel->setTextFormat(Qt::PlainText);
    _statusLabel->setWordWrap(true);

    _renderButton = new QPushButton(tr("Render"), this);
    _renderButton->setDefault(true);
    connect(_renderButton, &QPushButton::clicked, this, &TapeExportAudioDialog::onStart);
    _closeButton = new QPushButton(tr("Close"), this);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    auto* buttonsRow = new QHBoxLayout();
    buttonsRow->addStretch(1);
    buttonsRow->addWidget(_renderButton);
    buttonsRow->addWidget(_closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(_progressBar);
    layout->addWidget(_statusLabel);
    layout->addLayout(buttonsRow);

    setMinimumWidth(520);
}

TapeExportAudioDialog::~TapeExportAudioDialog()
{
    // A dialog destroyed mid-render waits here — reject() already asked the
    // worker to stop (the render loop polls cancel per block)
    if (_worker.joinable())
    {
        _cancelRequested = true;
        _worker.join();
    }
}

void TapeExportAudioDialog::reject()
{
    if (_worker.joinable())
    {
        _cancelRequested = true;
        _statusLabel->setText(tr("Cancelling…"));
        return;  // onRenderFinished resets the dialog to idle
    }
    QDialog::reject();
}

void TapeExportAudioDialog::onBrowseOutput()
{
    const QString suffix = SuffixForFormat(_formatCombo->currentIndex());
    const QString filter = suffix == QLatin1String("flac") ? tr("FLAC audio (*.flac)") : tr("WAV audio (*.wav)");
    QString start = _outputEdit->text().trimmed();
    if (start.isEmpty())
    {
        start = suggestedOutputPath();
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export to audio"), start, filter);
    if (path.isEmpty())
    {
        return;
    }
    if (!path.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive))
    {
        path += QLatin1Char('.') + suffix;
    }
    _outputEdit->setText(path);
}

void TapeExportAudioDialog::onFormatChanged()
{
    // Keep the suggested name in step with the format — only swap when the
    // path still ends in one of the bridge suffixes (never fight user input)
    QString path = _outputEdit->text().trimmed();
    if (path.isEmpty())
    {
        _outputEdit->setText(suggestedOutputPath());
        return;
    }
    if (path.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive) ||
        path.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive))
    {
        path.chop(path.size() - path.lastIndexOf(QLatin1Char('.')));
        _outputEdit->setText(path + QLatin1Char('.') + SuffixForFormat(_formatCombo->currentIndex()));
    }
}

void TapeExportAudioDialog::onStart()
{
    const QString outputPath = _outputEdit->text().trimmed();
    if (outputPath.isEmpty())
    {
        _statusLabel->setText(tr("Pick an output file first."));
        return;
    }

    // The renderer picks its encoder by extension — the combo and the suffix
    // must agree or the file would be written by a different codec than shown
    const QString suffix = SuffixForFormat(_formatCombo->currentIndex());
    if (!outputPath.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive))
    {
        _statusLabel->setText(tr("Output extension must match the format (.%1).").arg(suffix));
        return;
    }
    if (_firstBlockSpin->value() > _lastBlockSpin->value())
    {
        _statusLabel->setText(tr("First block must not exceed last block."));
        return;
    }

    TapeRenderRequest request;
    request.sourcePath = _sourcePath.toStdString();
    request.firstBlock = static_cast<size_t>(_firstBlockSpin->value());
    request.lastBlock = static_cast<size_t>(_lastBlockSpin->value());
    request.outputPath = outputPath.toStdString();
    request.sampleRate = static_cast<uint32_t>(_rateCombo->currentData().toUInt());
    request.amplitude = _amplitudeSpin->value();
    request.invertLevel = _invertCheck->isChecked();
    request.onProgress = [this](size_t blocksDone, size_t blocksTotal) {
        QMetaObject::invokeMethod(
            this, [this, blocksDone, blocksTotal]() { onRenderProgress(blocksDone, blocksTotal); },
            Qt::QueuedConnection);
    };
    request.cancelRequested = [this]() { return _cancelRequested.load(); };

    setRunning(true);
    _cancelRequested = false;
    _result = TapeRenderResult{};
    _statusLabel->setText(tr("Rendering…"));

    _worker = std::thread([this, request]() {
        _result = RenderTapeToAudio(request);
        QMetaObject::invokeMethod(this, [this]() { onRenderFinished(); }, Qt::QueuedConnection);
    });
}

void TapeExportAudioDialog::onRenderProgress(size_t blocksDone, size_t blocksTotal)
{
    if (blocksTotal == 0)
    {
        return;
    }
    const int percent = static_cast<int>((blocksDone * 100) / blocksTotal);
    _progressBar->setValue(qBound(0, percent, 100));
    _statusLabel->setText(tr("Rendering block %1 of %2…")
                              .arg(static_cast<qulonglong>(blocksDone))
                              .arg(static_cast<qulonglong>(blocksTotal)));
}

void TapeExportAudioDialog::onRenderFinished()
{
    if (_worker.joinable())
    {
        _worker.join();
    }
    setRunning(false);

    if (_result.ok)
    {
        QString text = tr("Done — %1 block(s), %2 s of audio via %3.")
                           .arg(static_cast<qulonglong>(_result.blocksRendered))
                           .arg(QString::number(_result.durationSec, 'f', 1))
                           .arg(QString::fromStdString(_result.encoderUsed));
        if (!_result.warnings.empty())
        {
            text += tr(" %1").arg(QString::fromStdString(_result.warnings.front()));
            if (_result.warnings.size() > 1)
            {
                text += tr(" (+%1 more)").arg(_result.warnings.size() - 1);
            }
        }
        _statusLabel->setText(text);
    }
    else if (_result.errorText == "cancelled")
    {
        _progressBar->setValue(0);
        _statusLabel->setText(tr("Cancelled — no output file kept."));
    }
    else
    {
        QMessageBox::warning(this, tr("Export to audio"), QString::fromStdString(_result.errorText));
        _statusLabel->setText(tr("Render failed."));
    }
}

void TapeExportAudioDialog::setRunning(bool running)
{
    _firstBlockSpin->setEnabled(!running);
    _lastBlockSpin->setEnabled(!running);
    _formatCombo->setEnabled(!running);
    _rateCombo->setEnabled(!running);
    _amplitudeSpin->setEnabled(!running);
    _invertCheck->setEnabled(!running);
    _outputEdit->setEnabled(!running);
    _browseButton->setEnabled(!running);
    _renderButton->setEnabled(!running);
    // While the worker runs the dismiss button aborts it; idle it just closes
    // (a finished render leaves nothing to cancel)
    _closeButton->setText(running ? tr("Cancel") : tr("Close"));
    if (running)
    {
        _progressBar->setValue(0);
    }
}

QString TapeExportAudioDialog::suggestedOutputPath() const
{
    const QFileInfo source(_sourcePath);
    const int formatIndex = _formatCombo ? _formatCombo->currentIndex() : FORMAT_WAV;
    return source.absolutePath() + QLatin1Char('/') + source.completeBaseName() + QLatin1Char('.') +
           SuffixForFormat(formatIndex);
}
