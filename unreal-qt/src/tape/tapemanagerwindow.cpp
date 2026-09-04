/**
 * @file tapemanagerwindow.cpp
 * @brief Implementation of TapeManagerWindow (design §9 — the Qt Tape Manager).
 *
 * Everything here runs on the UI thread: snapshots arrive queued from
 * EmulatorBinding, commands leave through the binding's bracketed tape*
 * methods. The window never touches Tape* or EmulatorContext directly.
 */

#include "tapemanagerwindow.h"

#include <algorithm>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "emulator/emulatorbinding.h"
#include "tape/tapeblockdialog.h"
#include "tape/tapeexportaudiodialog.h"

namespace
{
/// Short reject phrase for the badge (design §9.1: "block 3 is turbo").
QString ShortRejectPhrase(FastLoadRejectEnum reason)
{
    switch (reason)
    {
        case FastLoadRejectEnum::NonStandardTiming:
            return QStringLiteral("turbo");
        case FastLoadRejectEnum::PulseStream:
            return QStringLiteral("a pulse stream");
        case FastLoadRejectEnum::NonStandardFlag:
            return QStringLiteral("custom-loader flag");
        case FastLoadRejectEnum::ChecksumInvalid:
            return QStringLiteral("checksum-invalid");
        case FastLoadRejectEnum::Headerless:
            return QStringLiteral("headerless");
        case FastLoadRejectEnum::Unplayable:
            return QStringLiteral("unplayable");
        case FastLoadRejectEnum::ControlFlowInert:
            return QStringLiteral("control-flow");
        default:
            return QStringLiteral("not ROM-standard");
    }
}

QString StateName(TapePlaybackState state)
{
    switch (state)
    {
        case TapePlaybackState::Playing:
            return QStringLiteral("Playing");
        case TapePlaybackState::Paused:
            return QStringLiteral("Paused");
        case TapePlaybackState::Ended:
            return QStringLiteral("End of tape");
        case TapePlaybackState::Idle:
        default:
            return QStringLiteral("Idle");
    }
}

QString SecondsText(double seconds)
{
    return QString::number(seconds, 'f', 1) + QStringLiteral("s");
}
}  // namespace

TapeManagerWindow::TapeManagerWindow(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("TapeManagerWindow"));
    buildUi();
    reset();
}

void TapeManagerWindow::buildUi()
{
    // ---- Toolbar row: [Play][Pause][Stop][Rewind][Prev][Next]      N blocks ----
    auto* toolbarRow = new QHBoxLayout();
    _playButton = new QToolButton(this);
    _playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    _playButton->setToolTip(tr("Play: resume the frozen position in place, or start at the consumption cursor"));
    connect(_playButton, &QToolButton::clicked, this, [this]() {
        if (_binding) _binding->tapePlay();
    });

    _pauseButton = new QToolButton(this);
    _pauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    _pauseButton->setToolTip(tr("Pause: freeze the head exactly where it is"));
    connect(_pauseButton, &QToolButton::clicked, this, [this]() {
        if (_binding) _binding->tapePause();
    });

    _stopButton = new QToolButton(this);
    _stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    _stopButton->setToolTip(tr("Stop & eject: stop playback and drop the image (same as CLI 'tape stop')"));
    connect(_stopButton, &QToolButton::clicked, this, [this]() {
        if (_binding) _binding->tapeStop();
    });

    _rewindButton = new QToolButton(this);
    _rewindButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    _rewindButton->setToolTip(tr("Rewind to block 0 (image and catalog kept)"));
    connect(_rewindButton, &QToolButton::clicked, this, [this]() {
        if (_binding) _binding->tapeRewind();
    });

    // r10: fast previous/next track positioning — one block relative to the
    // head (in-flight block while playing/paused, else the next-up block)
    _prevBlockButton = new QToolButton(this);
    _prevBlockButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekBackward));
    _prevBlockButton->setToolTip(tr("Previous block: seek to the block before the current one"));
    connect(_prevBlockButton, &QToolButton::clicked, this, [this]() {
        if (!_binding) return;
        const size_t current = currentBlockIndex();
        if (current > 0)
        {
            _binding->tapeSeekToBlock(current - 1);
        }
    });

    _nextBlockButton = new QToolButton(this);
    _nextBlockButton->setIcon(style()->standardIcon(QStyle::SP_MediaSeekForward));
    _nextBlockButton->setToolTip(tr("Next block: seek to the block after the current one"));
    connect(_nextBlockButton, &QToolButton::clicked, this, [this]() {
        if (!_binding || _catalogSize == 0) return;
        const size_t current = currentBlockIndex();
        if (current + 1 < _catalogSize)
        {
            _binding->tapeSeekToBlock(current + 1);
        }
    });

    // tape-audio-bridge §7.3: offline render of the inserted image (or the
    // selected block range) to WAV/FLAC — a file conversion, no playback state
    _exportAudioButton = new QToolButton(this);
    _exportAudioButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    _exportAudioButton->setText(tr("Export to audio…"));
    _exportAudioButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _exportAudioButton->setToolTip(tr("Render the inserted tape (or the selected block range) to a WAV/FLAC file"));
    connect(_exportAudioButton, &QToolButton::clicked, this, [this]() {
        const QModelIndex current = _table->currentIndex();
        if (current.isValid())
        {
            openExportAudioDialog(static_cast<size_t>(current.row()), static_cast<size_t>(current.row()));
        }
        else
        {
            openExportAudioDialog(SIZE_MAX, SIZE_MAX);
        }
    });

    _blockCountLabel = new QLabel(this);

    toolbarRow->addWidget(_playButton);
    toolbarRow->addWidget(_pauseButton);
    toolbarRow->addWidget(_stopButton);
    toolbarRow->addWidget(_rewindButton);
    toolbarRow->addWidget(_prevBlockButton);
    toolbarRow->addWidget(_nextBlockButton);
    toolbarRow->addWidget(_exportAudioButton);
    toolbarRow->addStretch(1);
    toolbarRow->addWidget(_blockCountLabel);

    // ---- Fast-load badge (design §9.1 line 2) ----
    _badgeLabel = new QLabel(this);
    _badgeLabel->setTextFormat(Qt::PlainText);

    // ---- Progress line (design §9.1 line 3) ----
    _progressBar = new QProgressBar(this);
    _progressBar->setMaximum(100);
    _progressBar->setTextVisible(false);
    _progressBar->setFixedHeight(10);
    _progressLabel = new QLabel(this);
    auto* progressRow = new QHBoxLayout();
    progressRow->addWidget(_progressBar, 1);
    progressRow->addSpacing(8);
    progressRow->addWidget(_progressLabel);

    // ---- Block table ----
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
    connect(_table, &QTableView::doubleClicked, this, &TapeManagerWindow::onBlockDoubleClicked);
    _table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_table, &QTableView::customContextMenuRequested, this, &TapeManagerWindow::onTableContextMenu);
    connect(_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &TapeManagerWindow::onCurrentRowChanged);

    // ---- Details pane ----
    _detailsLabel = new QLabel(this);
    _detailsLabel->setTextFormat(Qt::RichText);
    _detailsLabel->setWordWrap(true);
    _detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    _detailsLabel->setMinimumHeight(84);
    _detailsLabel->setStyleSheet(QStringLiteral("QLabel { padding: 6px; }"));

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(toolbarRow);
    layout->addWidget(_badgeLabel);
    layout->addLayout(progressRow);
    layout->addWidget(_table, 1);
    layout->addWidget(_detailsLabel);

    setWindowFlag(Qt::Window, true);
    resize(780, 540);
}

void TapeManagerWindow::setBinding(EmulatorBinding* binding)
{
    if (_binding)
    {
        disconnect(_binding, nullptr, this, nullptr);
    }
    _binding = binding;
    if (!_binding)
    {
        reset();
        return;
    }

    // Snapshots are emitted on the UI thread (from the binding's queued
    // lambda), so a plain auto-connection is a direct call here.
    connect(_binding, &EmulatorBinding::tapeStateChanged, this, &TapeManagerWindow::onTapeSnapshot);
    connect(_binding, &EmulatorBinding::bound, this, &TapeManagerWindow::onBound);
    connect(_binding, &EmulatorBinding::unbound, this, &TapeManagerWindow::onUnbound);

    if (_binding->isBound())
    {
        onBound();
    }
    else
    {
        reset();
    }
}

void TapeManagerWindow::reset()
{
    _hasSnapshot = false;
    _lastSnapshot = TapeUiSnapshot{};
    _hasGenerationSnapshot = false;
    _catalogValid = false;
    _catalogSize = 0;
    _formatId.clear();
    _plan = TapeFastLoadPlan{};
    _detailsRow = -1;
    _model->Rebuild({}, TapeFastLoadPlan{});
    _blockCountLabel->setText(QString());
    _badgeLabel->setText(tr("No emulator bound"));
    _badgeLabel->setStyleSheet(QString());
    _progressBar->setValue(0);
    _progressLabel->setText(QString());
    _detailsLabel->setText(tr("No emulator bound — start an emulator and insert a tape."));
    updateToolbarEnabledState();
    updateWindowTitle();
}

void TapeManagerWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    emit visibilityChanged(true);
}

void TapeManagerWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}

void TapeManagerWindow::onBound()
{
    // Commands stay disabled until the first snapshot proves an image; the
    // producer ships one within a frame of binding.
    _badgeLabel->setText(tr("Bound — waiting for tape state…"));
}

void TapeManagerWindow::onUnbound()
{
    reset();
}

void TapeManagerWindow::onTapeSnapshot(const TapeUiSnapshot& snapshot)
{
    _lastSnapshot = snapshot;
    _hasSnapshot = true;

    // Latch the generation-scoped fields once per image (§9.3 rule 3): the
    // per-tick refresh paths below read the cache, never the snapshot copies
    if (snapshot.catalogChanged)
    {
        _hasGenerationSnapshot = true;
        _catalogValid = snapshot.catalogValid;
        _catalogSize = snapshot.catalog.size();
        _formatId = snapshot.formatId;
        _plan = snapshot.plan;
    }

    rebuildFromSnapshot(snapshot);
    updateBadge();
    updateProgress();
    updateToolbarEnabledState();
    updateWindowTitle();

    // Keep the details pane on the selected row across ticks
    if (_detailsRow >= 0 && !_model->descriptorAt(_detailsRow))
    {
        _detailsRow = -1;
    }
    updateDetails();
}

void TapeManagerWindow::rebuildFromSnapshot(const TapeUiSnapshot& snapshot)
{
    if (snapshot.catalogChanged)
    {
        // Table rebuild rides ONLY the generation change (design §9.3 rule 3);
        // selection does not survive a reset, so the details pane drops to the
        // image summary until a new row is picked.
        _detailsRow = -1;
        if (snapshot.catalogValid)
        {
            _model->Rebuild(snapshot.catalog, snapshot.plan);
            _blockCountLabel->setText(tr("%1 block(s)").arg(snapshot.catalog.size()));
        }
        else
        {
            _model->Rebuild({}, TapeFastLoadPlan{});
            _blockCountLabel->setText(tr("0 blocks"));
        }
    }
    _model->UpdatePosition(snapshot.state, snapshot.position, snapshot.cursor);
}

void TapeManagerWindow::onBlockDoubleClicked(const QModelIndex& index)
{
    // r9: double-click rewinds (seeks) to the block — FR-10's double-click
    // mapping restored; the content popup moved to the context menu
    if (index.isValid() && _binding)
    {
        _binding->tapeSeekToBlock(static_cast<size_t>(index.row()));
    }
}

void TapeManagerWindow::onTableContextMenu(const QPoint& pos)
{
    // r9: the context menu owns BOTH row actions — rewind and details
    const QModelIndex index = _table->indexAt(pos);
    if (!index.isValid() || !_binding)
    {
        return;
    }

    QMenu menu(this);
    QAction* rewindAction = menu.addAction(tr("Rewind to block %1").arg(index.row()));
    QAction* detailsAction = menu.addAction(tr("Details…"));
    menu.addSeparator();
    QAction* exportAction = menu.addAction(tr("Export to audio…"));
    QAction* chosen = menu.exec(_table->viewport()->mapToGlobal(pos));
    if (chosen == rewindAction)
    {
        _binding->tapeSeekToBlock(static_cast<size_t>(index.row()));
    }
    else if (chosen == detailsAction)
    {
        openBlockDialog(index.row());
    }
    else if (chosen == exportAction)
    {
        openExportAudioDialog(static_cast<size_t>(index.row()), static_cast<size_t>(index.row()));
    }
}

void TapeManagerWindow::openBlockDialog(int row)
{
    const TapeBlockDescriptor* descriptor = _model->descriptorAt(row);
    if (!descriptor || !_binding)
    {
        return;
    }

    std::vector<uint8_t> payload;
    if (!_binding->tapeGetBlockData(static_cast<size_t>(row), payload))
    {
        return;  // no image (or row past the block list) — nothing to show
    }

    // Program-classified data blocks decode through their paired header
    const TapeBlockDescriptor* pairedHeader = nullptr;
    if (descriptor->kind == TapeBlockKindEnum::Data && descriptor->pairedHeaderIndex != SIZE_MAX)
    {
        pairedHeader = _model->descriptorAt(static_cast<int>(descriptor->pairedHeaderIndex));
    }

    TapeBlockDialog dialog(*descriptor, pairedHeader, payload, this);
    dialog.exec();
}

void TapeManagerWindow::openExportAudioDialog(size_t firstBlock, size_t lastBlock)
{
    // §7.3: the source is the inserted image itself — the path the snapshot
    // carries, reloaded headlessly by the renderer (no emulator state touched)
    const bool hasImage = _hasGenerationSnapshot && _catalogValid && _catalogSize > 0;
    if (!hasImage || _lastSnapshot.filePath.isEmpty())
    {
        return;
    }

    TapeExportAudioDialog dialog(_lastSnapshot.filePath, _catalogSize, firstBlock, lastBlock, this);
    dialog.exec();
}

void TapeManagerWindow::onCurrentRowChanged(const QModelIndex& current, const QModelIndex& previous)
{
    Q_UNUSED(previous);
    _detailsRow = current.isValid() ? current.row() : -1;
    updateDetails();
}

void TapeManagerWindow::updateBadge()
{
    if (!_hasSnapshot)
    {
        return;
    }
    const TapeUiSnapshot& snapshot = _lastSnapshot;

    if (snapshot.filePath.isEmpty())
    {
        _badgeLabel->setText(tr("No tape inserted"));
        _badgeLabel->setStyleSheet(QString());
        return;
    }
    if (!_hasGenerationSnapshot || !_catalogValid)
    {
        _badgeLabel->setText(tr("Tape image unreadable: %1").arg(snapshot.filePath));
        _badgeLabel->setStyleSheet(QStringLiteral("color: #b04040;"));
        return;
    }

    const TapeFastLoadPlan& plan = _plan;
    QString text;
    QString color;

    switch (plan.verdict)
    {
        case FastLoadVerdictEnum::Full:
            text = tr("⚡ Fast load: FULL — all %1 eligible blocks (~%2 of %3)")
                       .arg(plan.eligibleBlocks)
                       .arg(SecondsText(plan.acceleratedSeconds))
                       .arg(SecondsText(plan.totalSeconds));
            color = QStringLiteral("color: #2e7d32;");  // green
            break;
        case FastLoadVerdictEnum::Partial:
            text = tr("⚡ Fast load: PARTIAL — blocks 0-%1 (~%2 of %3) · block %4 is %5")
                       .arg(plan.stickinessHorizon > 0 ? plan.stickinessHorizon - 1 : 0)
                       .arg(SecondsText(plan.acceleratedSeconds))
                       .arg(SecondsText(plan.totalSeconds))
                       .arg(plan.firstRejectIndex != SIZE_MAX ? static_cast<qulonglong>(plan.firstRejectIndex) : 0)
                       .arg(ShortRejectPhrase(plan.firstRejectReason));
            color = QStringLiteral("color: #b07800;");  // amber
            break;
        case FastLoadVerdictEnum::None:
            text = tr("⚡ Fast load: NONE — block %1 is %2 (whole tape at real speed)")
                       .arg(plan.firstRejectIndex != SIZE_MAX ? static_cast<qulonglong>(plan.firstRejectIndex) : 0)
                       .arg(ShortRejectPhrase(plan.firstRejectReason));
            color = QStringLiteral("color: #808080;");  // grey
            break;
        case FastLoadVerdictEnum::Empty:
        default:
            text = tr("⚡ Fast load: no playable blocks");
            color = QStringLiteral("color: #808080;");
            break;
    }

    // The badge always shows the plan's PREDICTION; the toggle state makes the
    // prediction-vs-observation gap legible instead of confusing (§8.3)
    if (!snapshot.fastTapeEnabled)
    {
        text += tr(" · trap disabled (Machine → Fast tape loading)");
    }

    _badgeLabel->setText(text);
    _badgeLabel->setStyleSheet(color);
}

void TapeManagerWindow::updateProgress()
{
    if (!_hasSnapshot)
    {
        return;
    }
    const TapeUiSnapshot& snapshot = _lastSnapshot;

    if (snapshot.filePath.isEmpty())
    {
        _progressBar->setValue(0);
        _progressLabel->setText(QString());
        return;
    }

    const bool inFlight =
        (snapshot.state == TapePlaybackState::Playing || snapshot.state == TapePlaybackState::Paused);
    if (inFlight && snapshot.position.has_value())
    {
        const TapePosition& position = *snapshot.position;
        int percent = 0;
        if (position.blockTotalSeconds > 0.0)
        {
            percent = static_cast<int>((position.secondsIntoBlock / position.blockTotalSeconds) * 100.0);
            percent = qBound(0, percent, 100);
        }
        _progressBar->setValue(percent);
        _progressLabel->setText(tr("Block %1/%2 ▸ %3 / %4 · %5")
                                    .arg(position.blockIndex)
                                    .arg(_lastSnapshot.catalog.empty() ? 0 : _lastSnapshot.catalog.size() - 1)
                                    .arg(SecondsText(position.secondsIntoBlock))
                                    .arg(SecondsText(position.blockTotalSeconds))
                                    .arg(StateName(snapshot.state)));
        return;
    }

    _progressBar->setValue(0);
    if (snapshot.state == TapePlaybackState::Ended)
    {
        _progressLabel->setText(tr("■ End of tape"));
    }
    else if (snapshot.position.has_value())
    {
        _progressLabel->setText(tr("■ %1 · next: block %2").arg(StateName(snapshot.state)).arg(snapshot.position->blockIndex));
    }
    else
    {
        _progressLabel->setText(tr("■ %1").arg(StateName(snapshot.state)));
    }
}

size_t TapeManagerWindow::currentBlockIndex() const
{
    // Anchor for the r10 prev/next buttons: the in-flight block whenever the
    // head sits somewhere in the image (playing, paused or positioned by a
    // seek), else the consumption cursor (the block that plays next)
    if (_hasSnapshot && _lastSnapshot.position.has_value())
    {
        return _lastSnapshot.position->blockIndex;
    }
    return _hasSnapshot ? _lastSnapshot.cursor : 0;
}

void TapeManagerWindow::updateToolbarEnabledState()
{
    const bool hasImage = _hasGenerationSnapshot && _catalogValid && _catalogSize > 0;
    const bool playing = _hasSnapshot && _lastSnapshot.state == TapePlaybackState::Playing;
    const bool paused = _hasSnapshot && _lastSnapshot.state == TapePlaybackState::Paused;
    const size_t current = currentBlockIndex();

    _playButton->setEnabled(hasImage && !playing);
    _pauseButton->setEnabled(playing);
    _stopButton->setEnabled(playing || paused);
    _rewindButton->setEnabled(hasImage);
    _prevBlockButton->setEnabled(hasImage && current > 0);
    _nextBlockButton->setEnabled(hasImage && current + 1 < _catalogSize);
    _exportAudioButton->setEnabled(hasImage);
}

void TapeManagerWindow::updateWindowTitle()
{
    // r8: the file is what identifies the window; the instance id only as a
    // short label (symbolic id or "#" + id tail) — a full UUID is noise
    const QString label = !_lastSnapshot.emulatorLabel.isEmpty() ? _lastSnapshot.emulatorLabel
                                                                  : _lastSnapshot.emulatorId;
    QString title = tr("Tape Manager");
    if (_hasSnapshot && !label.isEmpty())
    {
        title += QStringLiteral(" — ") + label;
        if (!_lastSnapshot.filePath.isEmpty())
        {
            const QString format = _formatId.isEmpty()
                                       ? QString()
                                       : QStringLiteral(" (%1)").arg(_formatId.toUpper());
            title += QStringLiteral(" — ") + QFileInfo(_lastSnapshot.filePath).fileName() + format;
        }
    }
    setWindowTitle(title);
}

void TapeManagerWindow::updateDetails()
{
    // No selection: image summary (file, verdict — the plan's one-liner)
    const TapeBlockDescriptor* descriptor = _model->descriptorAt(_detailsRow);
    if (!descriptor)
    {
        if (!_hasSnapshot || _lastSnapshot.filePath.isEmpty())
        {
            _detailsLabel->setText(
                tr("<i>Double-click rewinds to a block · right-click opens the block menu · ▸ marks the in-flight block · grey rows are consumed</i>"));
            return;
        }
        _detailsLabel->setText(tr("<b>%1</b> — %2 block(s), ~%3 total · %4<br>"
                                  "<i>Double-click rewinds to a block · right-click opens the block menu · ▸ marks the in-flight block · grey rows are consumed</i>")
                                   .arg(_lastSnapshot.filePath.toHtmlEscaped())
                                   .arg(_catalogSize)
                                   .arg(SecondsText(_plan.totalSeconds))
                                   .arg(QString::fromStdString(_plan.summary).toHtmlEscaped()));
        return;
    }

    const int row = _detailsRow;
    QString checksum = tr("n/a");
    if (descriptor->rawSize > 0)
    {
        checksum = descriptor->checksumValid ? tr("OK") : tr("INVALID");
    }

    // Fast-load line for this block
    QString fastLoadLine;
    const auto reject = _model->fastLoadRejectAt(row);
    if (reject.has_value())
    {
        if (reject.value() == FastLoadRejectEnum::None)
        {
            fastLoadLine = tr("Fast load: yes — ROM-standard, trap-shaped.");
        }
        else if (reject.value() == FastLoadRejectEnum::ControlBlock)
        {
            fastLoadLine = tr("Structural control entry — no payload, skipped by fast load.");
        }
        else
        {
            fastLoadLine = tr("Fast load: no — %1.").arg(ShortRejectPhrase(reject.value()));
            if (_plan.verdict == FastLoadVerdictEnum::Partial &&
                static_cast<size_t>(row) >= _plan.stickinessHorizon)
            {
                fastLoadLine += tr(" Blocks from %1 on play at real speed (fallback stickiness).")
                                    .arg(_plan.stickinessHorizon);
            }
        }
    }

    // Timing line: verbatim profile for turbo/pulse blocks (§9.1)
    QString timingLine;
    if (descriptor->timing.profile != TapeSpeedProfileEnum::StandardRom)
    {
        const TapeTimingProfile& timing = descriptor->timing;
        timingLine = tr(" · timing: pilot %1×%2T, sync %3/%4T, zero %5T / one %6T, pause %7 ms")
                         .arg(timing.pilotPulses)
                         .arg(timing.pilotHalfPeriod)
                         .arg(timing.sync1)
                         .arg(timing.sync2)
                         .arg(timing.zeroHalfPeriod)
                         .arg(timing.oneHalfPeriod)
                         .arg(timing.pauseMs);
    }

    // Header interpretation line — the block's own header, the paired
    // header's interpretation (r8: a Program body reads as its header), or the
    // headerless explanation for custom-loader payloads
    QString headerLine;
    const TapeBlockDescriptor* pairedHeader = _model->pairedHeaderAt(row);
    if (descriptor->headerValid)
    {
        headerLine = tr(" · %1 '%2', length %3, params %4/%5")
                         .arg(QString::fromUtf8(getTapeBlockTypeName(descriptor->headerType)))
                         .arg(QString::fromStdString(descriptor->name).toHtmlEscaped())
                         .arg(descriptor->declaredLength)
                         .arg(descriptor->param1)
                         .arg(descriptor->param2);
    }
    else if (pairedHeader)
    {
        headerLine = tr(" · body of %1 '%2', declared length %3")
                         .arg(QString::fromUtf8(getTapeBlockTypeName(pairedHeader->headerType)))
                         .arg(QString::fromStdString(pairedHeader->name).toHtmlEscaped())
                         .arg(pairedHeader->declaredLength);
    }
    else if (!descriptor->name.empty())
    {
        headerLine = tr(" · '%1'").arg(QString::fromStdString(descriptor->name).toHtmlEscaped());
    }
    else if (descriptor->headerless)
    {
        headerLine = tr(" · headerless — custom-loader payload");
    }

    // Payload preview hex (first bytes of the descriptor's copy)
    QString preview;
    const int previewCount = std::min<int>(8, static_cast<int>(descriptor->payloadPreview.size()));
    if (previewCount > 0)
    {
        for (int i = 0; i < previewCount; i++)
        {
            if (i > 0)
            {
                preview += QLatin1Char(' ');
            }
            preview += QString::number(descriptor->payloadPreview[static_cast<size_t>(i)], 16).rightJustified(2, QLatin1Char('0'));
        }
    }

    _detailsLabel->setText(tr("Block %1 — %2, ~%3, checksum %4%5%6<br>%7<br>"
                              "<i>Double-click rewinds to a block · right-click opens the block menu · ▸ marks the in-flight block · grey rows are consumed</i>")
                               .arg(row)
                               .arg(QString::fromUtf8(getTapeBlockKindName(descriptor->kind)))
                               .arg(SecondsText(descriptor->estimatedSeconds))
                               .arg(checksum)
                               .arg(headerLine, timingLine, fastLoadLine));
    if (!preview.isEmpty())
    {
        _detailsLabel->setText(_detailsLabel->text() + tr("<br>Preview: %1").arg(preview));
    }
}
