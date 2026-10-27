#include "editorwidget.h"

#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

#include "../textedit.h"
#include "designcanvas.h"
#include "editordocument.h"
#include "inspector.h"
#include "presets.h"
#include "runcontroller.h"
#include "sdb/iconpacks.h"
#include "sdb/scenarioio.h"
#include "sdb/scenarioops.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
QString Qs(const std::string& s)
{
    return QString::fromStdString(s);
}
const char* kFilter =
    "Scenarios (*.json *.yaml *.yml *.csv *.tsv);;JSON (*.json);;YAML (*.yaml *.yml);;CSV (*.csv);;TSV (*.tsv)";

// The palette: presets dragged onto the canvas carry their name.
class PaletteTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override
    {
        if (items.isEmpty() || items[0]->data(0, Qt::UserRole).toString().isEmpty())
            return nullptr;
        auto* m = new QMimeData;
        m->setData(kPresetMime, items[0]->data(0, Qt::UserRole).toString().toUtf8());
        return m;
    }
    QStringList mimeTypes() const override
    {
        return {QString::fromLatin1(kPresetMime)};
    }
};
}  // namespace

EditorWidget::EditorWidget(DaemonControl* daemon, QWidget* parent) : QWidget(parent)
{
    _doc = new EditorDocument(this);
    _run = new RunController(_doc, daemon, this);
    _canvas = new DesignCanvas(_doc, this);
    _inspector = new Inspector(_doc, this);

    // ---- toolbar
    auto* bar = new QToolBar(this);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addAction(QStringLiteral("New"), this, &EditorWidget::CmdNew);
    bar->addAction(QStringLiteral("Open"), this, &EditorWidget::CmdOpen);
    bar->addAction(QStringLiteral("Save"), this, &EditorWidget::CmdSave);
    bar->addAction(QStringLiteral("Save as"), this, &EditorWidget::CmdSaveAs);
    bar->addAction(QStringLiteral("Export"), this, &EditorWidget::CmdExportCopy);
    bar->addAction(QStringLiteral("Import"), this, [this] { CmdImport(QStringLiteral("menu")); });
    auto* toV2 = bar->addAction(QStringLiteral("Convert to v2"), this, [this] {
        QStringList notes;
        if (!_doc->ConvertToV2(&notes))
            ShowMessage(QStringLiteral("the scenario is v2 already"));
        else
            ShowMessage(notes.isEmpty() ? QStringLiteral("converted to v2") : notes.join(QStringLiteral("; ")));
    });
    toV2->setToolTip(QStringLiteral("format v1 -> v2: REST steps with a catalogue action become action steps (Save "
                                    "converts too)"));
    bar->addSeparator();
    _undo = _doc->UndoStack()->createUndoAction(this, QStringLiteral("Undo"));
    _undo->setShortcut(QKeySequence::Undo);
    _redo = _doc->UndoStack()->createRedoAction(this, QStringLiteral("Redo"));
    _redo->setShortcut(QKeySequence::Redo);
    bar->addAction(_undo);
    bar->addAction(_redo);
    bar->addSeparator();
    _runAct = bar->addAction(QStringLiteral("▶ Run on the deck"), this, [this] {
        QString err;
        if (!RunOn({}, &err))
            ShowMessage(err);
    });
    _runAct->setToolTip(
        QStringLiteral("unreal-streamdeck plays this scenario on the deck; edits go live, the studio shows it"));
    _stopAct = bar->addAction(QStringLiteral("■ Stop"), this, [this] { StopRun(); });
    _stopAct->setEnabled(false);
    bar->addSeparator();
    {
        // the icon pack of the preview: the scenario's (settings.pack, the inspector) unless one is picked here
        auto* pack = new QComboBox(bar);
        pack->setObjectName(QStringLiteral("preview-pack"));
        pack->addItem(QStringLiteral("Pack: the scenario's"), QString());
        for (const auto& p : sdb::packs::List())
            pack->addItem(QStringLiteral("Pack: ") + QString::fromStdString(p.title.empty() ? p.name : p.title),
                          QString::fromStdString(p.name));
        pack->setToolTip(QStringLiteral("The icon pack the canvas draws the glyph faces from (the preview only; the "
                                        "scenario's own is settings.pack, a deck's its pack setting)"));
        bar->addWidget(pack);
        connect(pack, &QComboBox::currentIndexChanged, this,
                [this, pack](int i) { _canvas->SetPack(pack->itemData(i).toString().toStdString()); });
    }

    // ---- left: tree + palette
    _tree = new QTreeWidget(this);
    _tree->setHeaderLabels({QStringLiteral("Scenario"), QStringLiteral("Key"), QStringLiteral("Action")});
    _tree->header()->setSectionResizeMode(QHeaderView::Interactive);  // every divider can be dragged
    _tree->header()->setStretchLastSection(true);
    _tree->setColumnWidth(0, 190);
    _tree->setColumnWidth(1, 44);
    _palette = new PaletteTree(this);
    _palette->setHeaderLabel(QStringLiteral("Actions (drag onto a key)"));
    _palette->setDragEnabled(true);
    _palette->setDragDropMode(QAbstractItemView::DragOnly);
    QMap<QString, QTreeWidgetItem*> cats;
    for (const auto& p : Presets())
    {
        QTreeWidgetItem*& cat = cats[p.category];
        if (!cat)
        {
            cat = new QTreeWidgetItem(_palette, {p.category});
            cat->setFlags(Qt::ItemIsEnabled);
            cat->setExpanded(true);
        }
        auto* it = new QTreeWidgetItem(cat, {p.name});
        it->setData(0, Qt::UserRole, p.name);
        it->setToolTip(0, p.description);
    }
    auto* search = new QLineEdit(this);
    search->setPlaceholderText(QStringLiteral("search actions"));
    connect(search, &QLineEdit::textChanged, this, [this](const QString& t) {
        for (int i = 0; i < _palette->topLevelItemCount(); ++i)
        {
            auto* cat = _palette->topLevelItem(i);
            int shown = 0;
            for (int j = 0; j < cat->childCount(); ++j)
            {
                const bool hit = t.isEmpty() || cat->child(j)->text(0).contains(t, Qt::CaseInsensitive) ||
                                 cat->text(0).contains(t, Qt::CaseInsensitive);
                cat->child(j)->setHidden(!hit);
                shown += hit;
            }
            cat->setHidden(shown == 0);
        }
    });
    auto* left = new QSplitter(Qt::Vertical, this);
    left->addWidget(_tree);
    auto* palBox = new QWidget(this);
    auto* palCol = new QVBoxLayout(palBox);
    palCol->setContentsMargins(0, 0, 0, 0);
    palCol->addWidget(search);
    palCol->addWidget(_palette);
    left->addWidget(palBox);
    left->setStretchFactor(0, 3);
    left->setStretchFactor(1, 2);

    // ---- centre: breadcrumbs + canvas + bottom tabs
    _crumbs = new QWidget(this);
    _crumbsLayout = new QHBoxLayout(_crumbs);
    _crumbsLayout->setContentsMargins(4, 2, 4, 2);
    _crumbsLayout->setSpacing(2);
    _issues = new QListWidget(this);
    _output = new QPlainTextEdit(this);
    MakeSelectableLog(_output);
    _output->setMaximumBlockCount(500);
    _bottom = new QTabWidget(this);
    _bottom->addTab(_issues, QStringLiteral("Issues"));
    _bottom->addTab(_output, QStringLiteral("Runtime"));
    auto* centre = new QSplitter(Qt::Vertical, this);
    auto* top = new QWidget(this);
    auto* topCol = new QVBoxLayout(top);
    topCol->setContentsMargins(0, 0, 0, 0);
    topCol->setSpacing(0);
    topCol->addWidget(_crumbs);
    topCol->addWidget(_canvas, 1);
    centre->addWidget(top);
    centre->addWidget(_bottom);
    centre->setStretchFactor(0, 4);
    centre->setStretchFactor(1, 1);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left);
    split->addWidget(centre);
    split->addWidget(_inspector);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 5);
    split->setStretchFactor(2, 2);
    split->setSizes({320, 760, 340});
    _status = new QLabel(this);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(bar);
    col->addWidget(split, 1);
    col->addWidget(_status);

    // ---- wiring
    connect(_doc, &EditorDocument::Changed, this, [this] {
        RebuildTree();
        RebuildIssues();
        RebuildCrumbs();
        UpdateTitle();
    });
    connect(_doc, &EditorDocument::CanvasChanged, this, &EditorWidget::RebuildCrumbs);
    connect(_doc, &EditorDocument::PathChanged, this, &EditorWidget::UpdateTitle);
    connect(_doc, &EditorDocument::SelectionChanged, this, [this] {
        if (_syncing)
            return;
        _syncing = true;
        const QString sel = _doc->Selected();
        _tree->clearSelection();
        for (QTreeWidgetItemIterator it(_tree); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString() == sel)
            {
                _tree->setCurrentItem(*it);
                (*it)->setSelected(true);
                _tree->scrollToItem(*it);
            }
        _syncing = false;
    });
    connect(_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (_syncing)
            return;
        const auto items = _tree->selectedItems();
        _syncing = true;
        _doc->Select(items.isEmpty() ? QString() : items[0]->data(0, Qt::UserRole).toString());
        _syncing = false;
        _canvas->update();
    });
    connect(_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        const QString id = it->data(0, Qt::UserRole).toString();
        if (!_doc->Enter(id))
            _doc->Select(id);
    });
    connect(_issues, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* it) { _doc->Select(it->data(Qt::UserRole).toString()); });
    connect(_canvas, &DesignCanvas::Message, this, &EditorWidget::ShowMessage);
    connect(_canvas, &DesignCanvas::ExportRequested, this, [this](const QString& id) {
        const QString f = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export %1 (format by extension)").arg(id),
            _doc->BaseDir() + QLatin1Char('/') + id + QStringLiteral(".deck.yaml"), QString::fromLatin1(kFilter));
        QString err;
        QStringList warn;
        if (!f.isEmpty() && !ExportSubtree(id, f, false, &err, &warn))
            ShowMessage(err);
        else if (!f.isEmpty())
            ShowMessage(QStringLiteral("exported %1%2")
                            .arg(f, warn.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" - links out: ") + warn.join(QStringLiteral(", "))));
    });
    connect(_canvas, &DesignCanvas::LinkRequested, this, [this](const QString& id) {
        const QString f = QFileDialog::getSaveFileName(
            this, QStringLiteral("Move %1 to a file and link it").arg(id),
            _doc->BaseDir() + QLatin1Char('/') + id + QStringLiteral(".deck.yaml"), QString::fromLatin1(kFilter));
        QString err;
        if (!f.isEmpty() && !ExportSubtree(id, f, true, &err))
            ShowMessage(err);
    });
    connect(_canvas, &DesignCanvas::ImportRequested, this, [this](int key, const QString& mode) {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Import a scenario file"), _doc->BaseDir(),
                                                       QString::fromLatin1(kFilter));
        if (f.isEmpty())
            return;
        const int cols = _doc->GetModel().cols;
        QString err;
        std::string nid;
        if (!ImportFile(f, _doc->CanvasMenu(), Slot{key / cols, key % cols}, mode, {}, &err, &nid))
            ShowMessage(err);
        else
            _doc->Select(Qs(nid), false);
    });
    connect(_canvas, &DesignCanvas::InlineRequested, this, [this](const QString& id) {
        QString err;
        if (!InlineInclude(id, &err))
            ShowMessage(err);
    });
    connect(_inspector, &Inspector::Message, this, &EditorWidget::ShowMessage);
    connect(_inspector, &Inspector::InlineRequested, this, [this](const QString& id) {
        QString err;
        if (!InlineInclude(id, &err))
            ShowMessage(err);
    });
    connect(_inspector, &Inspector::OpenRequested, this, [this](const QString& file) {
        if (_doc->Dirty() && QMessageBox::question(this, QStringLiteral("Open"),
                                                   QStringLiteral("Discard the unsaved changes?")) != QMessageBox::Yes)
            return;
        QString err;
        if (!OpenFile(file, &err))
            ShowMessage(err);
    });
    connect(_run, &RunController::OutputLine, this, [this](const QString& l) { _output->appendPlainText(l); });
    connect(_run, &RunController::StateChanged, this, [this] {
        _runAct->setEnabled(!_run->Running());
        _stopAct->setEnabled(_run->Running());
        UpdateTitle();
        emit RunStateChanged();
    });

    // shortcuts on the canvas / tree
    auto shortcut = [this](const QKeySequence& k, std::function<void()> f) {
        auto* s = new QShortcut(k, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, std::move(f));
    };
    // in a text field (inspector, JSON, runtime log) the edit keys are the field's own
    shortcut(QKeySequence::Delete, [this] {
        if (TextCommand(QKeySequence::Delete))
            return;
        QString e;
        if (!DeleteSelected(&e))
            ShowMessage(e);
    });
    shortcut(QKeySequence(Qt::Key_Backspace), [this] {
        if (_canvas->hasFocus() || _tree->hasFocus())
        {
            QString e;
            if (!DeleteSelected(&e))
                ShowMessage(e);
        }
    });
    shortcut(QKeySequence::Copy, [this] {
        if (!TextCommand(QKeySequence::Copy) && !_inspector->isAncestorOf(focusWidget()))
            CopySelected();
    });
    shortcut(QKeySequence::Paste, [this] {
        if (TextCommand(QKeySequence::Paste) || _inspector->isAncestorOf(focusWidget()))
            return;
        QString e;
        if (!Paste(&e))
            ShowMessage(e);
    });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { CmdDuplicate(); });
    shortcut(QKeySequence(Qt::Key_Escape), [this] { _doc->Up(); });
    // Cmd+S is the studio's File > Save Scenario (docked and floating alike): a second binding here would make the
    // key ambiguous inside the editor, and Qt then runs neither

    RebuildTree();
    RebuildIssues();
    RebuildCrumbs();
    UpdateTitle();
}

void EditorWidget::CmdNew()
{
    bool ok;
    const QString name = QInputDialog::getText(this, QStringLiteral("New scenario"), QStringLiteral("Name"),
                                               QLineEdit::Normal, QStringLiteral("scenario"), &ok);
    if (ok)
        _doc->NewDocument(_doc->GetModel(), name);
}

void EditorWidget::CmdOpen()
{
    if (_doc->Dirty() && QMessageBox::question(this, QStringLiteral("Open"),
                                               QStringLiteral("Discard the unsaved changes?")) != QMessageBox::Yes)
        return;
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Open scenario"), _doc->BaseDir(),
                                                   QString::fromLatin1(kFilter));
    QString err;
    if (!f.isEmpty() && !OpenFile(f, &err))
        ShowMessage(err);
}

void EditorWidget::CmdSave()
{
    QString err;
    if (!SaveFile({}, &err))
        ShowMessage(err);
}

void EditorWidget::CmdSaveAs()
{
    const QString f =
        QFileDialog::getSaveFileName(this, QStringLiteral("Save scenario"), _doc->Path(), QString::fromLatin1(kFilter));
    QString err;
    if (!f.isEmpty() && !SaveFile(f, &err))
        ShowMessage(err);
}

void EditorWidget::CmdExportCopy()
{
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Export a copy (format by extension)"),
                                                   _doc->BaseDir(), QString::fromLatin1(kFilter));
    QString err;
    if (!f.isEmpty() && !_doc->ExportTo(f, &err))
        ShowMessage(err);
    else if (!f.isEmpty())
        ShowMessage(QStringLiteral("exported ") + f);
}

void EditorWidget::CmdImport(const QString& mode)
{
    const QString f =
        QFileDialog::getOpenFileName(this,
                                     mode == QLatin1String("link") ? QStringLiteral("Link a scenario file (include)")
                                                                   : QStringLiteral("Import a scenario file"),
                                     _doc->BaseDir(), QString::fromLatin1(kFilter));
    if (f.isEmpty())
        return;
    std::optional<Slot> at;
    if (const int k = _canvas->SelectedSlotKey(); k >= 0)
        at = Slot{k / _doc->GetModel().cols, k % _doc->GetModel().cols};
    QString err;
    std::string nid;
    if (!ImportFile(f, _doc->CanvasMenu(), mode == QLatin1String("items") ? std::nullopt : at, mode, {}, &err, &nid))
        ShowMessage(err);
    else
    {
        _doc->Select(Qs(nid), false);
        ShowMessage(QStringLiteral("imported ") + f);
    }
}

void EditorWidget::CmdExportSelected(bool link)
{
    const QString id = _doc->Selected();
    if (id.isEmpty())
    {
        ShowMessage(QStringLiteral("select a node first"));
        return;
    }
    const QString f = QFileDialog::getSaveFileName(
        this, link ? QStringLiteral("Move %1 to a file and link it").arg(id) : QStringLiteral("Export %1").arg(id),
        _doc->BaseDir() + QLatin1Char('/') + id + QStringLiteral(".deck.yaml"), QString::fromLatin1(kFilter));
    if (f.isEmpty())
        return;
    QString err;
    QStringList warn;
    if (!ExportSubtree(id, f, link, &err, &warn))
        ShowMessage(err);
    else
        ShowMessage(QStringLiteral("%1 %2%3").arg(
            link ? QStringLiteral("moved to") : QStringLiteral("exported"), f,
            warn.isEmpty() ? QString() : QStringLiteral(" - links out: ") + warn.join(QStringLiteral(", "))));
}

void EditorWidget::CmdDuplicate()
{
    const std::string id = _doc->Selected().toStdString();
    if (id.empty())
        return;
    QString err;
    std::string nid;
    if (_doc->Apply(
            QStringLiteral("Duplicate"), [&](json& d) { return ops::DuplicateNode(d, id, std::nullopt); }, &err, &nid))
        _doc->Select(Qs(nid), false);
    else
        ShowMessage(err);
}

void EditorWidget::CmdInlineSelected()
{
    QString err;
    if (!InlineInclude(_doc->Selected(), &err))
        ShowMessage(err);
}

void EditorWidget::CmdCut()
{
    CopySelected();
    QString err;
    if (!DeleteSelected(&err))
        ShowMessage(err);
}

void EditorWidget::ShowIssues()
{
    _bottom->setCurrentWidget(_issues);
}

void EditorWidget::ShowMessage(const QString& text)
{
    _status->setText(QStringLiteral("  ") + text);
}

void EditorWidget::UpdateTitle()
{
    const QString file = _doc->Path().isEmpty() ? QStringLiteral("untitled") : QFileInfo(_doc->Path()).fileName();
    const QString title = QStringLiteral("Scenario editor - %1%2%3")
                              .arg(file, _doc->Dirty() ? QStringLiteral(" *") : QString(),
                                   _run->Running() ? QStringLiteral("  [running: %1]").arg(_run->How()) : QString());
    setWindowTitle(title);
    if (QWidget* p = parentWidget())
        p->setWindowTitle(title);
}

void EditorWidget::RebuildTree()
{
    _syncing = true;
    QSet<QString> collapsed;
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
        if ((*it)->childCount() && !(*it)->isExpanded())
            collapsed.insert((*it)->data(0, Qt::UserRole).toString());
    _tree->clear();
    QSet<QString> errors;
    for (const auto& i : _doc->Issues())
        if (i.error)
            errors.insert(Qs(i.node));
    std::function<void(const json&, QTreeWidgetItem*)> add = [&](const json& n, QTreeWidgetItem* parent) {
        const QString id = Qs(n.value("id", ""));
        const bool menu = n.value("type", "") == "menu";
        QString title = Qs(n.value("title", n.value("id", "")));
        QString tags;
        for (const auto& t : n.value("tags", json::array()))
            if (t.is_string())
                tags += QStringLiteral(" #") + Qs(t.get<std::string>());
        const QString at = n.contains("at")
                               ? QStringLiteral("%1,%2").arg(n["at"][0].get<int>()).arg(n["at"][1].get<int>())
                               : QString();
        const QString kind = menu ? QStringLiteral("menu (%1)").arg(n.value("items", json::array()).size())
                                  : Qs(n.contains("do") ? n["do"].value("kind", "") : "");
        auto* it = parent ? new QTreeWidgetItem(parent, {title + tags, at, kind})
                          : new QTreeWidgetItem(_tree, {title + tags, at, kind});
        it->setData(0, Qt::UserRole, id);
        it->setToolTip(0, id);
        if (menu)
        {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
        }
        if (errors.contains(id))
            it->setForeground(0, QColor(220, 40, 40));
        if (n.contains("included_from"))
        {  // from another file: read-only here
            QFont f = it->font(0);
            f.setItalic(true);
            it->setFont(0, f);
            it->setForeground(0, QColor(120, 140, 170));
            const json* raw = ops::FindNode(_doc->Doc(), n.value("id", ""));
            if (raw && raw->value("type", "") == "include")
                it->setText(0, QStringLiteral("\u21AA ") + it->text(0));
            it->setToolTip(0, id + QStringLiteral("  (from ") + Qs(n.value("included_from", "")) + QLatin1Char(')'));
        }
        for (const auto& c : n.value("items", json::array()))
            add(c, it);
        it->setExpanded(!collapsed.contains(id));
    };
    add(_doc->Resolved()["root"], nullptr);
    for (QTreeWidgetItemIterator it(_tree); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == _doc->Selected())
        {
            (*it)->setSelected(true);
            _tree->setCurrentItem(*it);
        }
    _syncing = false;
}

void EditorWidget::RebuildCrumbs()
{
    while (QLayoutItem* i = _crumbsLayout->takeAt(0))
    {
        delete i->widget();
        delete i;
    }
    const auto path = ops::FindNode(_doc->Resolved(), _doc->CanvasMenu().toStdString())
                          ? [&] {
                                std::vector<std::pair<QString, QString>> v;
                                if (auto s = _doc->Loose())
                                    for (const Node* n : PathTo(s->root, _doc->CanvasMenu().toStdString()))
                                        v.emplace_back(Qs(n->id), Qs(n->title));
                                return v;
                            }()
                          : std::vector<std::pair<QString, QString>>{};
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (i)
            _crumbsLayout->addWidget(new QLabel(QStringLiteral("›")));
        auto* b = new QPushButton(path[i].second, _crumbs);
        b->setFlat(i + 1 == path.size());
        b->setEnabled(i + 1 != path.size());
        const QString id = path[i].first;
        connect(b, &QPushButton::clicked, this, [this, id] {
            _doc->Enter(id);
            _doc->Select(QString(), false);
        });
        _crumbsLayout->addWidget(b);
    }
    _crumbsLayout->addStretch(1);
    _crumbsLayout->addWidget(
        new QLabel(QStringLiteral("drop actions on keys, double-click a menu to enter, Esc = up")));
}

void EditorWidget::RebuildIssues()
{
    _issues->clear();
    for (const auto& i : _doc->Issues())
    {
        auto* it =
            new QListWidgetItem(QStringLiteral("%1 %2: %3")
                                    .arg(i.error ? QStringLiteral("✖") : QStringLiteral("⚠"),
                                         i.node.empty() ? QStringLiteral("(scenario)") : Qs(i.node), Qs(i.message)),
                                _issues);
        it->setData(Qt::UserRole, Qs(i.node));
        it->setForeground(i.error ? QColor(220, 60, 60) : QColor(200, 150, 0));
    }
    _bottom->setTabText(0, _doc->Issues().empty() ? QStringLiteral("Issues")
                                                  : QStringLiteral("Issues (%1)").arg(_doc->Issues().size()));
}

bool EditorWidget::OpenFile(const QString& path, QString* error)
{
    if (!_doc->Open(path, error))
        return false;
    ShowMessage(QStringLiteral("opened ") + path);
    return true;
}

bool EditorWidget::SaveFile(const QString& path, QString* error)
{
    QString target = path;
    if (target.isEmpty() && _doc->Path().isEmpty())
    {
        target = QFileDialog::getSaveFileName(this, QStringLiteral("Save scenario"),
                                              QStringLiteral("scenario.deck.json"), QString::fromLatin1(kFilter));
        if (target.isEmpty())
            return true;
    }
    if (!_doc->Save(target, error))
        return false;
    ShowMessage(_doc->LastNotes().isEmpty()
                    ? QStringLiteral("saved ") + _doc->Path()
                    : QStringLiteral("saved %1 as v2: %2").arg(_doc->Path(), _doc->LastNotes().join(QStringLiteral("; "))));
    return true;
}

bool EditorWidget::RunOn(const QString& serial, QString* error)
{
    if (!_run->Start(serial, error))
    {
        _bottom->setCurrentWidget(_doc->Valid() ? static_cast<QWidget*>(_output) : _issues);
        return false;
    }
    _bottom->setCurrentWidget(_output);
    ShowMessage(QStringLiteral("running on the deck (%1) - edits go live").arg(_run->How()));
    return true;
}

void EditorWidget::StopRun()
{
    _run->Stop();
    ShowMessage(QStringLiteral("stopped"));
}

bool EditorWidget::ExportSubtree(const QString& id, const QString& path, bool link, QString* error,
                                 QStringList* warnings)
{
    // from the resolved view, so an included subtree can be exported (a copy of it) too
    std::vector<std::string> outside;
    const json frag = ops::ExtractSubtree(_doc->Resolved(), id.toStdString(), &outside);
    if (frag.is_null())
    {
        if (error)
            *error = QStringLiteral("no node '%1'").arg(id);
        return false;
    }
    json clean = frag;
    io::StripIncludeMarks(clean["root"]);  // a copy of an included subtree is the file's own
    std::string err;
    if (!io::SaveFile(path.toStdString(), clean, &err))
    {
        if (error)
            *error = Qs(err);
        return false;
    }
    if (warnings)
        for (const auto& o : outside)
            warnings->append(Qs(o));
    if (!link)
        return true;
    const std::string rel = _doc->RelativePath(path).toStdString(), sid = id.toStdString();
    return _doc->Apply(
        QStringLiteral("Move %1 to %2").arg(id, path), [&](json& d) { return ops::LinkNode(d, sid, rel); }, error);
}

bool EditorWidget::ImportFile(const QString& path, const QString& parent, std::optional<Slot> at, const QString& mode,
                              const QString& node, QString* error, std::string* newId)
{
    if (!_doc->IncludedFrom(parent).isEmpty())
    {
        if (error)
            *error = QStringLiteral("'%1' comes from another file").arg(parent);
        return false;
    }
    const std::string p = parent.toStdString();
    if (mode == QLatin1String("link"))
    {
        json inc{{"type", "include"},
                 {"file", _doc->RelativePath(QFileInfo(path).absoluteFilePath()).toStdString()},
                 {"id", ops::Slug(QFileInfo(path).baseName().toStdString())}};
        if (!node.isEmpty())
            inc["node"] = node.toStdString();
        return _doc->Apply(
            QStringLiteral("Link %1").arg(path), [&](json& d) { return ops::InsertNode(d, p, at, inc); }, error, newId);
    }
    std::string err;
    auto frag = io::LoadResolved(path.toStdString(), &err);  // a copy: its own includes become part of it
    if (!frag)
    {
        if (error)
            *error = Qs(err);
        return false;
    }
    io::StripIncludeMarks((*frag)["root"]);
    const auto m = mode == QLatin1String("items") ? ops::ImportMode::Items : ops::ImportMode::Menu;
    return _doc->Apply(
        QStringLiteral("Import %1").arg(path),
        [&](json& d) { return ops::ImportSubtree(d, p, at, *frag, m, node.toStdString()); }, error, newId);
}

bool EditorWidget::InlineInclude(const QString& id, QString* error)
{
    const json* r = ops::FindNode(_doc->Resolved(), id.toStdString());
    const json* raw = ops::FindNode(_doc->Doc(), id.toStdString());
    if (!r || !raw || raw->value("type", "") != "include")
    {
        if (error)
            *error = QStringLiteral("'%1' is not an include").arg(id);
        return false;
    }
    const json copy = *r;
    const std::string sid = id.toStdString();
    return _doc->Apply(
        QStringLiteral("Inline %1").arg(id), [&](json& d) { return ops::InlineNode(d, sid, copy); }, error);
}

void EditorWidget::CopySelected()
{
    if (const json* n = ops::FindNode(_doc->Resolved(), _doc->Selected().toStdString()))
    {
        _clipboard = Qs(n->dump());
        ShowMessage(QStringLiteral("copied ") + _doc->Selected());
    }
}

bool EditorWidget::Paste(QString* error)
{
    if (_clipboard.isEmpty())
    {
        if (error)
            *error = QStringLiteral("nothing copied");
        return false;
    }
    json node = json::parse(_clipboard.toStdString());
    node.erase("at");
    io::StripIncludeMarks(node);  // a copy of an included node is the document's own (editable, saved as written)
    const std::string menu = _doc->CanvasMenu().toStdString();
    std::optional<Slot> at;
    if (const int k = _canvas->SelectedSlotKey(); k >= 0)
        at = Slot{k / _doc->GetModel().cols, k % _doc->GetModel().cols};
    std::string nid;
    const json resolved = _doc->Resolved();  // a copy from an include: its ids are taken there
    if (!_doc->Apply(
            QStringLiteral("Paste"), [&](json& d) { return ops::InsertNode(d, menu, at, node, &resolved); }, error,
            &nid))
        return false;
    _doc->Select(Qs(nid), false);
    return true;
}

bool EditorWidget::DeleteSelected(QString* error)
{
    const std::string id = _doc->Selected().toStdString();
    if (id.empty())
    {
        if (error)
            *error = QStringLiteral("nothing selected");
        return false;
    }
    return _doc->Apply(QStringLiteral("Delete"), [&](json& d) { return ops::RemoveNode(d, id); }, error);
}
