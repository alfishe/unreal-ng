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
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
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

    auto* buttons = new QHBoxLayout();
    auto add = [this, buttons](const QString& text, const QString& tip, void (MediaPanelWindow::*slot)()) {
        auto* button = new QPushButton(text, this);
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
        return button;
    };
    _insertFile = add(tr("Insert File..."), tr("Insert an image file into the selected slot"), &MediaPanelWindow::onInsertFile);
    _insertFolder = add(tr("Insert Folder..."), tr("A host folder as a disk (TR-DOS) or card (FAT)"), &MediaPanelWindow::onInsertFolder);
    _eject = add(tr("Eject"), tr("Take the medium out"), &MediaPanelWindow::onEject);
    _save = add(tr("Save"), tr("Write the disk back into its file"), &MediaPanelWindow::onSave);
    _export = add(tr("Export..."), tr("Write a copy of the medium as it is now"), &MediaPanelWindow::onExport);
    _discard = add(tr("Discard"), tr("Drop the unsaved writes"), &MediaPanelWindow::onDiscard);
    _protect = add(tr("Protect"), tr("The slot's write-protect switch"), &MediaPanelWindow::onProtect);
    _protect->setCheckable(true);
    _create = add(tr("Create Blank..."), tr("A blank disk or card in the selected slot"), &MediaPanelWindow::onCreate);
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
        connect(_binding, &EmulatorBinding::unbound, this, [this] { _revision = UINT64_MAX; refresh(); });
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
    MediaManager* manager = emulator ? emulator->GetContext()->pMediaManager : nullptr;
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
        const QString medium = row.medium.empty() ? QString() : QFileInfo(Q(row.medium)).fileName() + "  (" + Q(row.format) + ")";
        const QStringList cells = {Q(row.slot), Q(row.alias), Q(row.state), medium, Q(row.access), Q(row.dirty)};
        for (int c = 0; c < ColCount; c++)
        {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setToolTip(Q(row.label) + (row.medium.empty() ? QString() : "\n" + Q(row.medium)) +
                             (row.dirty.empty() ? QString() : tr("\nUnsaved: %1").arg(Q(row.dirty))) +
                             (row.writeProtect ? tr("\nwrite-protected") : QString()));
            if (row.detached)
                item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
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
    else if (const StateNode* notes = reply.find("report"); notes && !notes->items.empty())
    {
        QStringList lines;
        for (const StateNode& line : notes->items)
            lines << Q(line.s);
        QMessageBox::information(this, what, lines.join("\n"));
    }
    _revision = UINT64_MAX;
    refresh();
}

void MediaPanelWindow::insertInto(const std::string& slot, const QString& path)
{
    _lastDirectory = QFileInfo(path).absolutePath();
    // async: the UI does not wait for the swap delay; the table shows "pending"
    std::map<std::string, std::string> options = {{"async", "true"}};

    // An IDE unit takes the other kind of medium once its drive is swapped:
    // an ISO needs a CD-ROM drive, a disk image a hard disk (the unit is empty)
    const auto row = std::find_if(_rows.begin(), _rows.end(), [&slot](const MediaPanelRow& r) { return r.slot == slot; });
    if (row != _rows.end() && slot.rfind("ide", 0) == 0 && !row->present)
    {
        const bool iso = QFileInfo(path).suffix().compare(QStringLiteral("iso"), Qt::CaseInsensitive) == 0;
        const bool cdDrive = row->kind == "optical";
        if (iso != cdDrive)
        {
            const QString question = iso ? tr("%1 is a hard disk unit. Make it a CD-ROM drive for this disc?")
                                         : tr("%1 is a CD-ROM drive. Make it a hard disk unit for this image?");
            if (QMessageBox::question(this, tr("Insert"), question.arg(Q(slot))) != QMessageBox::Yes)
                return;
            options["device"] = iso ? "cdrom" : "disk";
        }
    }
    report(run("insert", slot, S(path), options), tr("Insert"));
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
    insertInto(_rows[static_cast<size_t>(row)].slot, paths.front());
}
