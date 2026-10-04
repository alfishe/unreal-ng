/**
 * @file mediapanelwindow.cpp
 * @brief The media panel: every slot of the machine through MediaControl.
 */

#include "mediapanelwindow.h"

#include <algorithm>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "common/threadhelper.h"
#include "emulator/emulator.h"
#include "emulator/media/mediatargets.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatormanager.h"
#include "emulator/media/mediacontrol.h"

namespace
{
    enum Column
    {
        ColSlot,
        ColAlias,
        ColState,
        ColMedium,
        ColAccess,
        ColDirty,
        ColCount
    };

    QString Q(const std::string& text) { return QString::fromStdString(text); }
    std::string S(const QString& text) { return text.toStdString(); }

    std::string ReplyMessage(const StateNode& reply)
    {
        const StateNode* message = reply.find("message");
        return message ? message->s : std::string();
    }

    bool Ok(const StateNode& reply)
    {
        const StateNode* ok = reply.find("ok");
        return ok && ok->b;
    }
}  // namespace

/// region <MediaSlotTable>

MediaSlotTable::MediaSlotTable(QWidget* parent) : QTableWidget(parent)
{
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
}

void MediaSlotTable::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MediaSlotTable::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls() && rowAt(event->position().toPoint().y()) >= 0)
        event->acceptProposedAction();
    else
        event->ignore();
}

void MediaSlotTable::dropEvent(QDropEvent* event)
{
    const int row = rowAt(event->position().toPoint().y());
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
    {
        if (url.isLocalFile())
            paths << url.toLocalFile();
    }
    if (row >= 0 && !paths.isEmpty())
    {
        event->acceptProposedAction();
        emit filesDropped(row, paths);
    }
}

/// endregion </MediaSlotTable>

MediaPanelWindow::MediaPanelWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("Media"));
    buildUi();

    // The table follows the manager's revision: a cheap read, redrawn only on change
    _timer = new QTimer(this);
    _timer->setInterval(250);
    connect(_timer, &QTimer::timeout, this, &MediaPanelWindow::refresh);

    // BUGS.md #3: ticks once a second while a folder scan is in flight,
    // updates the "Scanning... (N entries)" text and watches for a stall
    _scanWatchdog = new QTimer(this);
    _scanWatchdog->setInterval(1000);
    connect(_scanWatchdog, &QTimer::timeout, this, &MediaPanelWindow::checkScanWatchdog);
}

MediaPanelWindow::~MediaPanelWindow()
{
    // The worker's completion lambda captures `this` and is marshaled back
    // via QMetaObject::invokeMethod - it must never fire after this object is
    // gone, so cancel and join unconditionally before the rest of the object
    // (in particular _scanStatus/_scanProgress, read by onInsertFolderFinished
    // only when invoked, but the thread itself must not outlive `this` either way)
    cancelInsertWorker();
    if (_insertWorker.joinable())
        _insertWorker.join();
}

void MediaPanelWindow::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    _table = new MediaSlotTable(this);
    _table->setColumnCount(ColCount);
    _table->setHorizontalHeaderLabels({tr("Slot"), tr("Alias"), tr("State"), tr("Medium"), tr("Access"), tr("Unsaved")});
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->verticalHeader()->setVisible(false);
    _table->horizontalHeader()->setSectionResizeMode(ColMedium, QHeaderView::Stretch);
    _table->setToolTip(tr("Drop a disk image, card image or folder on a row to insert it into that slot"));
    layout->addWidget(_table);

    // BUGS.md #3: shown only while an async folder scan is in flight
    _scanStatus = new QLabel(this);
    _scanStatus->hide();
    layout->addWidget(_scanStatus);
    _scanProgress = new QProgressBar(this);
    _scanProgress->setMaximum(0);  // indeterminate: no a-priori total entry count
    _scanProgress->setTextVisible(false);
    _scanProgress->setFixedHeight(10);
    _scanProgress->hide();
    layout->addWidget(_scanProgress);

    auto* buttons = new QHBoxLayout();
    auto add = [this, buttons](const QString& text, const QString& tip, void (MediaPanelWindow::*slot)()) {
        auto* button = new QPushButton(text, this);
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
        return button;
    };
    _insertFile = add(tr("Insert File..."), tr("Insert an image file into the selected slot"), &MediaPanelWindow::onInsertFile);
    _insertFolder = add(tr("Insert Folder..."), tr("A host folder as a disk (TR-DOS), a card or hard disk (FAT) or, in a CD-ROM drive, an audio CD of its MP3 / FLAC / WAV files"), &MediaPanelWindow::onInsertFolder);
    _eject = add(tr("Eject"), tr("Take the medium out"), &MediaPanelWindow::onEject);
    _save = add(tr("Save"), tr("Write the disk back into its file"), &MediaPanelWindow::onSave);
    _export = add(tr("Export..."), tr("Write a copy of the medium as it is now"), &MediaPanelWindow::onExport);
    _discard = add(tr("Discard"), tr("Drop the unsaved writes"), &MediaPanelWindow::onDiscard);
    _protect = add(tr("Protect"), tr("The slot's write-protect switch"), &MediaPanelWindow::onProtect);
    _protect->setCheckable(true);
    _create = add(tr("Create Blank..."), tr("A blank disk or card in the selected slot"), &MediaPanelWindow::onCreate);
    _compactFlash = add(tr("CF Card"),
                        tr("An empty IDE unit: the next disk image goes in as a CompactFlash card on an IDE adapter (a "
                           "disk that identifies itself as CFA) instead of a hard disk"),
                        &MediaPanelWindow::onCompactFlash);
    _compactFlash->setCheckable(true);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(_table, &QTableWidget::itemSelectionChanged, this, &MediaPanelWindow::updateButtons);
    connect(_table, &MediaSlotTable::filesDropped, this, &MediaPanelWindow::onFilesDropped);
    updateButtons();
}

void MediaPanelWindow::setBinding(EmulatorBinding* binding)
{
    if (_binding)
        disconnect(_binding, nullptr, this, nullptr);
    _binding = binding;
    if (_binding)
    {
        connect(_binding, &EmulatorBinding::bound, this, [this] { _revision = UINT64_MAX; refresh(); });
        connect(_binding, &EmulatorBinding::unbound, this, [this] {
            // The worker keeps the old emulator alive via its own shared_ptr
            // (BUGS.md #3) and is unaffected either way, but there is no
            // reason to let it keep scanning for a panel that has moved on
            cancelInsertWorker();
            _revision = UINT64_MAX;
            refresh();
        });
    }
    refresh();
}

void MediaPanelWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    _revision = UINT64_MAX;
    refresh();
    _timer->start();
    emit visibilityChanged(true);
}

void MediaPanelWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
    emit visibilityChanged(false);
}

void MediaPanelWindow::refresh()
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    MediaManager* manager = context ? context->pMediaManager : nullptr;
    if (!manager)
    {
        if (!_rows.empty() || _revision != 0)
        {
            _rows.clear();
            _revision = 0;
            rebuildTable();
        }
        return;
    }
    if (manager->Revision() == _revision)
        return;
    _revision = manager->Revision();
    _rows = MediaPanelRows(run("list", "", "", {}, false));
    rebuildTable();
}

void MediaPanelWindow::rebuildTable()
{
    const QString selected = selectedRow() ? Q(selectedRow()->slot) : QString();
    _table->setRowCount(static_cast<int>(_rows.size()));
    for (int r = 0; r < static_cast<int>(_rows.size()); r++)
    {
        const MediaPanelRow& row = _rows[static_cast<size_t>(r)];
        const bool scanning = !_insertSlot.empty() && row.slot == _insertSlot;
        const QString medium = row.medium.empty() ? QString() : QFileInfo(Q(row.medium)).fileName() + "  (" + Q(row.format) + ")";
        const QStringList cells = {Q(row.slot), Q(row.alias), Q(row.state), medium, Q(row.access), Q(row.dirty)};
        for (int c = 0; c < ColCount; c++)
        {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setToolTip(Q(row.label) + (row.medium.empty() ? QString() : "\n" + Q(row.medium)) +
                             (row.dirty.empty() ? QString() : tr("\nUnsaved: %1").arg(Q(row.dirty))) +
                             (row.writeProtect ? tr("\nwrite-protected") : QString()) +
                             (scanning ? tr("\nScanning a folder...") : QString()));
            if (row.detached)
                item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            if (scanning)
            {
                // BUGS.md #3: makes it unambiguous which mount point an
                // in-flight async folder scan belongs to. QPalette::Accent
                // (the system accent color, Qt 6.6+) rather than
                // QPalette::Highlight - Highlight fades to a dull gray
                // whenever the window is not key/focused, which made the
                // scanning row hard to notice; Accent stays the user's chosen
                // accent color regardless of focus
                QColor tint = palette().color(QPalette::Accent);
                tint.setAlpha(110);
                item->setBackground(tint);
            }
            _table->setItem(r, c, item);
        }
        if (Q(row.slot) == selected)
            _table->selectRow(r);
    }
    _table->resizeColumnsToContents();
    _table->horizontalHeader()->setSectionResizeMode(ColMedium, QHeaderView::Stretch);
    updateButtons();
}

const MediaPanelRow* MediaPanelWindow::selectedRow() const
{
    const auto rows = _table->selectionModel() ? _table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty())
        return nullptr;
    const int r = rows.front().row();
    return r >= 0 && r < static_cast<int>(_rows.size()) ? &_rows[static_cast<size_t>(r)] : nullptr;
}

void MediaPanelWindow::updateButtons()
{
    // BUGS.md #3: while a folder scan is in flight, every action is disabled
    // rather than let the user start a second insert or an eject that would
    // race the in-flight swap once the scan completes
    if (_insertWorker.joinable())
    {
        _insertFile->setEnabled(false);
        _insertFolder->setEnabled(false);
        _eject->setEnabled(false);
        _save->setEnabled(false);
        _export->setEnabled(false);
        _discard->setEnabled(false);
        _protect->setEnabled(false);
        _create->setEnabled(false);
        _compactFlash->setEnabled(false);
        return;
    }

    const MediaPanelRow* row = selectedRow();
    const bool slot = row && !row->detached;
    _insertFile->setEnabled(slot);
    _insertFolder->setEnabled(slot && row->acceptsFolder);
    _eject->setEnabled(slot && row->present);
    _save->setEnabled(row && (row->present || row->detached) && row->kind == "floppy");
    _export->setEnabled(row && (row->present || row->detached));
    _discard->setEnabled(row && (row->isDirty || row->detached));
    _protect->setEnabled(slot);
    _protect->setChecked(slot && row->writeProtect);
    _create->setEnabled(slot && (row->kind == "floppy" || row->kind == "block"));
    // The drive of an IDE unit changes only while it is empty; a CD-ROM drive stays one (an ISO picks it)
    const bool ideDisk = slot && row->ideUnit && row->kind != "optical";
    _compactFlash->setEnabled(ideDisk && !row->present);
    _compactFlash->setChecked(ideDisk && (row->present ? row->compactFlash : WantsCompactFlash(*row)));
}

bool MediaPanelWindow::WantsCompactFlash(const MediaPanelRow& row) const
{
    // The user's choice for the next insert, else the unit's drive as it is
    const auto it = _cfChoice.find(row.slot);
    return it != _cfChoice.end() ? it->second : row.compactFlash;
}

void MediaPanelWindow::onCompactFlash()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    _cfChoice[row->slot] = _compactFlash->isChecked();
}

StateNode MediaPanelWindow::run(const std::string& verb, const std::string& slot, const std::string& path,
                                std::map<std::string, std::string> options, bool askDisposition)
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    if (!emulator)
    {
        StateNode none = StateNode::Object();
        none["ok"] = false;
        none["message"] = "no emulator";
        return none;
    }
    MediaRequest request{verb, slot, path, options};
    StateNode reply = MediaControl(emulator->GetContext()).Execute(request).ToValue();

    if (askDisposition && MediaNeedsDisposition(reply))
    {
        QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                        tr("%1 has changes that are not saved.").arg(Q(slot)), QMessageBox::NoButton, this);
        QPushButton* save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
        QPushButton* exportTo = box.addButton(tr("Export..."), QMessageBox::ActionRole);
        QPushButton* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == save)
            options["save"] = "true";
        else if (box.clickedButton() == discard)
            options["discard"] = "true";
        else if (box.clickedButton() == exportTo)
        {
            const QString target = QFileDialog::getSaveFileName(this, tr("Export the changed medium"), _lastDirectory);
            if (target.isEmpty())
                return reply;
            options["export"] = S(target);
        }
        else
            return reply;
        request.options = options;
        reply = MediaControl(emulator->GetContext()).Execute(request).ToValue();
    }
    return reply;
}

void MediaPanelWindow::report(const StateNode& reply, const QString& what)
{
    if (!Ok(reply))
        QMessageBox::warning(this, what, Q(ReplyMessage(reply)));
    _revision = UINT64_MAX;
    refresh();
}

void MediaPanelWindow::insertInto(const std::string& slot, const QString& path)
{
    _lastDirectory = QFileInfo(path).absolutePath();
    // async: the UI does not wait for the swap delay; the table shows "pending"
    std::map<std::string, std::string> options = {{"async", "true"}};

    // An IDE unit takes the other kind of medium once its drive is swapped (the unit is empty):
    // a CD image (ISO, CUE, raw BIN, CD CHD) or a folder of MP3 / FLAC / WAV files (an audio CD)
    // needs a CD-ROM drive; a disk image or another folder a hard disk. The same classifier as
    // every insert target (MediaTargets::Classify) decides
    const auto row = std::find_if(_rows.begin(), _rows.end(), [&slot](const MediaPanelRow& r) { return r.slot == slot; });
    if (row != _rows.end() && slot.rfind("ide", 0) == 0 && !row->present)
    {
        const FileClass file = MediaTargets::Classify(S(path));
        const bool cdMedium = !file.kinds.empty() && file.kinds.front() == FileKind::Optical;
        const bool cdDrive = row->kind == "optical";
        if (cdMedium != cdDrive)
        {
            QString question;
            if (cdMedium && file.folder)
                question = tr("%1 is a hard disk unit. Make it a CD-ROM drive and insert the folder's MP3 / FLAC / WAV "
                              "files as an audio CD? (No: the folder becomes a disk volume)");
            else if (cdMedium)
                question = tr("%1 is a hard disk unit. Make it a CD-ROM drive for this disc?");
            else
                question = tr("%1 is a CD-ROM drive. Make it a hard disk unit for this image?");
            const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Insert"), question.arg(Q(slot)));
            if (answer == QMessageBox::Yes)
                options["device"] = cdMedium ? "cdrom" : (WantsCompactFlash(*row) ? "cf" : "disk");
            else if (!(cdMedium && file.folder))
                return;
        }
        // A disk image into an empty IDE disk unit: a hard disk or, with "CF Card" on, a CompactFlash card
        else if (!cdMedium && WantsCompactFlash(*row) != row->compactFlash)
            options["device"] = WantsCompactFlash(*row) ? "cf" : "disk";
    }

    // BUGS.md #3: a folder's scan + volume build can take seconds (a large
    // folder, a slow/network disk) - keep that off the UI thread. A plain
    // file insert stays synchronous: MediaFormatRegistry::Open for an image
    // file is cheap, and the existing disposition dialog (run()'s
    // askDisposition path) only makes sense on the UI thread anyway
    if (QFileInfo(path).isDir())
    {
        if (_insertWorker.joinable())
        {
            QMessageBox::warning(this, tr("Insert"), tr("Another folder is still being scanned."));
            return;
        }
        insertFolderAsync(slot, path, options);
        return;
    }
    report(run("insert", slot, S(path), options), tr("Insert"));
}

bool MediaPanelWindow::insertFolder(const std::string& slot, const QString& path, QString* reason)
{
    if (_insertWorker.joinable())
    {
        if (reason)
            *reason = tr("another folder is still being built");
        return false;
    }
    _lastDirectory = QFileInfo(path).absolutePath();
    insertFolderAsync(slot, path, {{"async", "true"}});
    return true;
}

void MediaPanelWindow::insertFolderAsync(const std::string& slot, const QString& path,
                                         std::map<std::string, std::string> options)
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    if (!emulator)
    {
        StateNode none = StateNode::Object();
        none["ok"] = false;
        none["message"] = "no emulator";
        report(none, tr("Insert"));
        return;
    }

    const std::string emulatorId = emulator->GetId();
    const std::string pathStd = S(path);

    _insertCancelRequested = false;
    _insertEntriesScanned = 0;
    _insertBytesScanned = 0;
    _insertLastSeenEntries = 0;
    _insertStalledTicks = 0;
    _insertSlot = slot;
    _insertPath = path;
    _scanStatus->setText(tr("<b>%1</b>: scanning %2...").arg(Q(slot), path.toHtmlEscaped()));
    _scanStatus->show();
    _scanProgress->show();
    rebuildTable();  // highlights _insertSlot's row right away, not just on the next refresh() tick
    _scanWatchdog->start();

    MediaRequest request;
    request.verb = "insert";
    request.selector = slot;
    request.path = pathStd;
    request.options = std::move(options);
    request.options["async"] = "true";
    // Captured by value (atomics behind pointers to `this`): safe to call
    // from the worker thread for as long as `this` is alive, which the
    // destructor guarantees by joining before any member is torn down
    request.cancelRequested = [this]() { return _insertCancelRequested.load(); };
    request.onProgress = [this](uint64_t count, uint64_t bytes) {
        _insertEntriesScanned.store(count);
        _insertBytesScanned.store(bytes);
    };

    _insertWorker = std::thread([this, emulatorId, request]() {
        ThreadHelper::setThreadName("media-scan");
        // A fresh lookup by id, not the EmulatorBinding's raw pointer: keeps
        // the Emulator (and its EmulatorContext/MediaManager) alive for the
        // scan's duration even if the GUI unbinds/rebinds concurrently. If
        // the emulator was removed outright, GetEmulator returns null and
        // this reports a plain failure instead of touching freed state
        std::shared_ptr<Emulator> keepAlive = EmulatorManager::GetInstance()->GetEmulator(emulatorId);
        StateNode reply;
        if (!keepAlive)
        {
            reply = StateNode::Object();
            reply["ok"] = false;
            reply["message"] = "emulator no longer available";
        }
        else
        {
            reply = MediaControl(keepAlive->GetContext()).Execute(request).ToValue();
        }
        QMetaObject::invokeMethod(
            this, [this, reply]() { onInsertFolderFinished(reply); }, Qt::QueuedConnection);
    });
}

void MediaPanelWindow::onInsertFolderFinished(const StateNode& reply)
{
    if (_insertWorker.joinable())
        _insertWorker.join();
    _scanWatchdog->stop();
    _scanStatus->hide();
    _scanProgress->hide();
    _insertSlot.clear();
    _insertPath.clear();
    report(reply, tr("Insert"));  // report() forces a refresh(), which clears the row highlight too
}

void MediaPanelWindow::checkScanWatchdog()
{
    const uint64_t current = _insertEntriesScanned.load();
    if (current != _insertLastSeenEntries)
    {
        _insertLastSeenEntries = current;
        _insertStalledTicks = 0;
    }
    else
    {
        ++_insertStalledTicks;
    }
    const QString size = QLocale().formattedDataSize(static_cast<qint64>(_insertBytesScanned.load()));
    _scanStatus->setText(tr("<b>%1</b>: scanning %2... (%3 entries, %4)")
                             .arg(Q(_insertSlot), _insertPath.toHtmlEscaped())
                             .arg(current)
                             .arg(size));

    if (_insertStalledTicks >= _insertStallTimeoutSeconds)
    {
        // Ask the worker to stop; its own completion still comes through
        // onInsertFolderFinished with whatever MediaError::Cancelled message
        // FolderSnapshot/FolderDiskBuilder produced - no UI state is
        // fabricated here, and the watchdog keeps ticking (harmlessly) until
        // the worker actually reports back and stops it
        _insertCancelRequested = true;
        _scanStatus->setText(
            tr("<b>%1</b>: no progress for %2s - cancelling...").arg(Q(_insertSlot)).arg(_insertStallTimeoutSeconds));
    }
}

void MediaPanelWindow::cancelInsertWorker()
{
    _insertCancelRequested = true;
}

void MediaPanelWindow::onInsertFile()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    const std::string slot = row->slot;
    const QString filter = Q(MediaFileFilter(run("formats", "", "", {{"kind", row->kind}}, false), row->kind));
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert into %1").arg(Q(slot)), _lastDirectory, filter);
    if (!path.isEmpty())
        insertInto(slot, path);
}

void MediaPanelWindow::onInsertFolder()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    const std::string slot = row->slot;
    const QString path = QFileDialog::getExistingDirectory(this, tr("Folder for %1").arg(Q(slot)), _lastDirectory);
    if (!path.isEmpty())
        insertInto(slot, path);
}

void MediaPanelWindow::onEject()
{
    if (const MediaPanelRow* row = selectedRow())
        report(run("eject", row->slot, "", {{"async", "true"}}), tr("Eject"));
}

void MediaPanelWindow::onSave()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    const std::string slot = row->slot;
    StateNode reply = run("save", slot, "", {}, false);
    if (!Ok(reply))
    {
        // No file of its own (folder, blank, Hobeta): ask where
        const QString filter = Q(MediaFileFilter(run("formats", "", "", {{"kind", "floppy"}}, false), "floppy"));
        const QString target = QFileDialog::getSaveFileName(this, tr("Save %1 as").arg(Q(slot)), _lastDirectory, filter);
        if (target.isEmpty())
            return;
        reply = run("save", slot, S(target), {}, false);
    }
    report(reply, tr("Save"));
}

void MediaPanelWindow::onExport()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    const std::string slot = row->slot;
    const QString target = QFileDialog::getSaveFileName(this, tr("Export %1").arg(Q(slot)), _lastDirectory);
    if (!target.isEmpty())
        report(run("export", slot, S(target), {}, false), tr("Export"));
}

void MediaPanelWindow::onDiscard()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    const std::string slot = row->slot;
    if (QMessageBox::question(this, tr("Discard"), tr("Drop the unsaved changes of %1?").arg(Q(slot))) == QMessageBox::Yes)
        report(run("discard", slot, "", {}, false), tr("Discard"));
}

void MediaPanelWindow::onProtect()
{
    if (const MediaPanelRow* row = selectedRow())
        report(run("protect", row->slot, "", {{"on", _protect->isChecked() ? "true" : "false"}}, false), tr("Protect"));
}

void MediaPanelWindow::onCreate()
{
    const MediaPanelRow* row = selectedRow();
    if (!row)
        return;
    const std::string slot = row->slot;
    std::map<std::string, std::string> options;
    if (row->kind == "block")
    {
        bool ok = false;
        const int megabytes = QInputDialog::getInt(this, tr("Blank card"), tr("Size in MiB:"), 64, 1, 2048, 1, &ok);
        if (!ok)
            return;
        options["size"] = std::to_string(static_cast<uint64_t>(megabytes) * 1024 * 1024);
    }
    report(run("create", slot, "", options), tr("Create Blank"));
}

void MediaPanelWindow::onFilesDropped(int row, const QStringList& paths)
{
    if (row < 0 || row >= static_cast<int>(_rows.size()) || _rows[static_cast<size_t>(row)].detached)
        return;
    const MediaPanelRow& target = _rows[static_cast<size_t>(row)];
    const QString& path = paths.front();

    // The row's slot must take the file (media-drop-targets design §6): the core's plan says which do.
    // An empty IDE unit may still swap its drive for a CD or a disk image (insertInto asks first)
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    if (emulator)
    {
        const FileClass file = MediaTargets::Classify(S(path));
        const MediaPlan plan = MediaTargets::Plan(emulator->GetContext(), file);
        const bool takes = std::any_of(plan.targets.begin(), plan.targets.end(),
                                       [&target](const MediaTarget& t) { return t.slotId == target.slot; });
        const bool driveSwap = target.slot.rfind("ide", 0) == 0 && !target.present &&
                               (file.Is(FileKind::Optical) || file.Is(FileKind::Hdd));
        if (!takes && !driveSwap)
        {
            const QString why = plan.Refused() ? Q(plan.refusal) : tr("it goes to %1").arg(Q(plan.SlotList()));
            QMessageBox::information(this, tr("Insert"),
                                     tr("%1 does not take %2: %3").arg(Q(target.label), QFileInfo(path).fileName(), why));
            return;
        }
    }
    insertInto(target.slot, path);
}
