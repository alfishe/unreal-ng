#include "designcanvas.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

#include "../virtualdeck.h"
#include "editordocument.h"
#include "presets.h"
#include "sdb/navigator.h"
#include "sdb/scenariorender.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
QImage ToQImage(const Rgb& im)
{
    return QImage(im.px.data(), im.w, im.h, im.w * 3, QImage::Format_RGB888).copy();
}
QString Qs(const std::string& s)
{
    return QString::fromStdString(s);
}
}  // namespace

DesignCanvas::DesignCanvas(EditorDocument* doc, QWidget* parent) : DeckWidget(new VirtualDeck, parent), _doc(doc)
{
    Deck()->setParent(this);
    Deck()->SetBrightnessLevel(100);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    connect(_doc, &EditorDocument::Changed, this, &DesignCanvas::Refresh);
    connect(_doc, &EditorDocument::CanvasChanged, this, &DesignCanvas::Refresh);
    connect(_doc, &EditorDocument::SelectionChanged, this, qOverload<>(&QWidget::update));
    Refresh();
}

void DesignCanvas::Refresh()
{
    VirtualDeck* vd = Deck();
    const Model& m = _doc->GetModel();
    if (&vd->GetModel() != &m)
    {
        vd->SetModel(m);
        vd->SetBrightnessLevel(100);
    }
    vd->ClearScreen(Qt::black);
    _keyIds.clear();
    _keyErrors.clear();
    _backKey = -1;
    const auto& s = _doc->Loose();
    if (!s)
    {
        update();
        return;
    }
    Navigator nav(*s);
    nav.GoTo(_doc->CanvasMenu().toStdString());
    const Page page = Layout(*s, nav, m);
    // drawn at the key's own size, as the runtime does: what the deck will show, pixel for pixel
    const int face = m.key.w > 0 && m.key.w == m.key.h ? m.key.w : kFaceSize;
    const int pad = m.keySafeInset > 0 && m.key.w > 0 ? (m.keySafeInset * face + m.key.w / 2) / m.key.w : 0;
    _backKey = page.backKey;
    for (int k = 0; k < m.keys; ++k)
    {
        if (k == page.backKey)
        {
            const std::string parent = page.path.size() >= 2 ? page.path[page.path.size() - 2] : "";
            vd->DrawKey(k, ToQImage(RenderBackKey(parent, static_cast<int>(page.path.size()) - 1, false, pad, face)));
        }
        else if (const Node* n = page.keys[static_cast<size_t>(k)])
        {
            vd->DrawKey(k, ToQImage(RenderFace(n->face, n->title, false, n->IsMenu(), pad, face)));
            _keyIds[k] = n->id;
        }
    }
    for (const auto& i : _doc->Issues())
        for (const auto& [k, id] : _keyIds)
            if (i.error && i.node == id)
                _keyErrors[k] = true;
    if (m.HasLcd())
        vd->DrawStrip(ToQImage(RenderStrip(page, m.lcdLogicalW, m.lcdLogicalH, m.dialTileW)));
    update();
}

QImage DesignCanvas::RenderImage(double scale) const
{
    QImage img(size() * scale, QImage::Format_ARGB32);
    img.setDevicePixelRatio(scale);
    img.fill(Qt::black);
    const_cast<DesignCanvas*>(this)->render(&img);
    img.setDevicePixelRatio(1);
    return img;
}

std::string DesignCanvas::IdAt(int key) const
{
    auto it = _keyIds.find(key);
    return it == _keyIds.end() ? std::string() : it->second;
}

bool DesignCanvas::IsMenu(const std::string& id) const
{
    const auto* n = ops::FindNode(_doc->Resolved(), id);
    return n && n->value("type", "") == "menu";
}

void DesignCanvas::paintEvent(QPaintEvent* e)
{
    DeckWidget::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Model& m = Deck()->GetModel();
    const std::string sel = _doc->Selected().toStdString();
    for (int k = 0; k < m.keys; ++k)
    {
        const QRectF r = KeyRectWidget(k);
        if (r.isEmpty())
            continue;
        if (k == _backKey)
        {  // reserved by the runtime
            p.setPen(QPen(QColor(120, 160, 255, 200), 2, Qt::DashLine));
            p.setBrush(QBrush(QColor(120, 160, 255, 40), Qt::BDiagPattern));
            p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 6, 6);
        }
        if (_keyErrors.count(k))
        {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(230, 30, 30));
            p.drawEllipse(QRectF(r.right() - 14, r.top() + 4, 10, 10));
        }
        if (!sel.empty() && IdAt(k) == sel)
        {
            p.setPen(QPen(QColor(0, 200, 255), 3));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-3, -3, 3, 3), 8, 8);
        }
        else if (k == _slotKey && IdAt(k).empty())
        {
            p.setPen(QPen(QColor(0, 200, 255, 160), 2, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 8, 8);
        }
        if (k == _dropKey)
        {
            const bool menu = IsMenu(IdAt(k));
            p.setPen(QPen(k == _backKey ? QColor(230, 40, 40) : menu ? QColor(255, 180, 0) : QColor(40, 220, 90), 3));
            p.setBrush(QColor(255, 255, 255, 30));
            p.drawRoundedRect(r.adjusted(-3, -3, 3, 3), 8, 8);
        }
    }
    // the menu's own parts (strip, dials) select the menu itself
    if (sel == _doc->CanvasMenu().toStdString() && !StripRectWidget().isEmpty())
    {
        p.setPen(QPen(QColor(0, 200, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(StripRectWidget().adjusted(-3, -3, 3, 3), 6, 6);
    }
}

void DesignCanvas::mousePressEvent(QMouseEvent* e)
{
    _pressPos = e->position().toPoint();
    const Hit h = HitTest(e->position());
    _pressKey = h.kind == Hit::Key ? h.index : -1;
    if (e->button() != Qt::LeftButton)
        return;
    if (h.kind == Hit::Key)
    {
        const std::string id = IdAt(h.index);
        _slotKey = id.empty() ? h.index : -1;
        if (!id.empty())
            _doc->Select(Qs(id), false);
        else if (h.index == _backKey)
            emit Message(QStringLiteral("Back key (drawn by the runtime): double-click = up one level"));
        else
            _doc->Select(QString(), false);
    }
    else if (h.kind == Hit::Strip || h.kind == Hit::Dial)
    {
        _doc->Select(_doc->CanvasMenu(), false);  // strip content and dials belong to the menu
    }
    update();
}

void DesignCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (!(e->buttons() & Qt::LeftButton) || _pressKey < 0)
    {
        DeckWidget::mouseMoveEvent(e);
        return;
    }
    if ((e->position().toPoint() - _pressPos).manhattanLength() < QApplication::startDragDistance())
        return;
    const std::string id = IdAt(_pressKey);
    if (id.empty())
        return;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kNodeMime), QByteArray::fromStdString(id));
    mime->setText(Qs(id));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const QRectF r = KeyRectWidget(_pressKey);
    drag->setPixmap(grab(r.toRect()).scaled(r.size().toSize() * 0.8, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    drag->setHotSpot(QPoint(static_cast<int>(r.width() * 0.4), static_cast<int>(r.height() * 0.4)));
    _pressKey = -1;
    drag->exec(Qt::MoveAction);
}

void DesignCanvas::mouseReleaseEvent(QMouseEvent*)
{
    _pressKey = -1;
}

void DesignCanvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    const Hit h = HitTest(e->position());
    if (h.kind != Hit::Key)
        return;
    if (h.index == _backKey)
    {
        _doc->Up();
        return;
    }
    const std::string id = IdAt(h.index);
    if (!id.empty() && IsMenu(id))
    {
        _doc->Enter(Qs(id));
        _doc->Select(QString(), false);
    }
}

void DesignCanvas::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kPresetMime)) ||
        e->mimeData()->hasFormat(QString::fromLatin1(kNodeMime)))
        e->acceptProposedAction();
}

void DesignCanvas::dragMoveEvent(QDragMoveEvent* e)
{
    const Hit h = HitTest(e->position());
    const int k = h.kind == Hit::Key ? h.index : -1;
    if (k != _dropKey)
    {
        _dropKey = k;
        update();
    }
    if (k >= 0 && k != _backKey)
        e->acceptProposedAction();
    else
        e->ignore();
}

void DesignCanvas::dragLeaveEvent(QDragLeaveEvent*)
{
    _dropKey = -1;
    update();
}

void DesignCanvas::dropEvent(QDropEvent* e)
{
    const Hit h = HitTest(e->position());
    _dropKey = -1;
    if (h.kind != Hit::Key)
    {
        update();
        return;
    }
    DropOn(h.index, e->mimeData());
    e->acceptProposedAction();
    update();
}

void DesignCanvas::DropOn(int key, const QMimeData* mime)
{
    if (const QString f = _doc->IncludedFrom(_doc->CanvasMenu()); !f.isEmpty())
    {
        emit Message(
            QStringLiteral("This menu comes from %1: open that file to edit it, or Inline its include here").arg(f));
        return;
    }
    const Model& m = Deck()->GetModel();
    const Slot at{key / m.cols, key % m.cols};
    const std::string menu = _doc->CanvasMenu().toStdString();
    const std::string target = IdAt(key);
    QString err;
    std::string newId;
    if (mime->hasFormat(QString::fromLatin1(kPresetMime)))
    {
        const Preset* pr = FindPreset(QString::fromUtf8(mime->data(QString::fromLatin1(kPresetMime))));
        if (!pr)
            return;
        const json node = pr->node;
        bool ok = false;
        if (!target.empty() && IsMenu(target))  // onto a menu: add inside it
            ok = _doc->Apply(
                QStringLiteral("Add %1 to %2").arg(pr->name, Qs(target)),
                [&](json& d) { return ops::InsertNode(d, target, std::nullopt, node); }, &err, &newId);
        else if (!target.empty())
        {
            emit Message(QStringLiteral("Key [%1, %2] holds '%3': drop on an empty key or a menu")
                             .arg(at.row)
                             .arg(at.col)
                             .arg(Qs(target)));
            return;
        }
        else
            ok = _doc->Apply(
                QStringLiteral("Add %1").arg(pr->name), [&](json& d) { return ops::InsertNode(d, menu, at, node); },
                &err, &newId);
        if (!ok)
        {
            emit Message(err);
            return;
        }
        _doc->Select(Qs(newId), false);
        return;
    }
    const std::string id = mime->data(QString::fromLatin1(kNodeMime)).toStdString();
    if (id.empty() || id == target)
        return;
    bool ok = false;
    if (!target.empty() && IsMenu(target))
    {  // into the menu, first free key there
        const auto slot = ops::FreeSlot(_doc->Doc(), target);
        if (!slot)
        {
            emit Message(QStringLiteral("'%1' has no free key").arg(Qs(target)));
            return;
        }
        ok = _doc->Apply(
            QStringLiteral("Move %1 into %2").arg(Qs(id), Qs(target)),
            [&](json& d) { return ops::MoveNode(d, id, target, *slot, false); }, &err);
    }
    else
    {
        ok = _doc->Apply(
            target.empty() ? QStringLiteral("Move %1").arg(Qs(id))
                           : QStringLiteral("Swap %1 and %2").arg(Qs(id), Qs(target)),
            [&](json& d) { return ops::MoveNode(d, id, menu, at, true); }, &err);
    }
    if (!ok)
        emit Message(err);
}

void DesignCanvas::contextMenuEvent(QContextMenuEvent* e)
{
    const Hit h = HitTest(e->pos());
    if (h.kind != Hit::Key)
        return;
    const int key = h.index;
    const std::string id = IdAt(key);
    QMenu menu(this);
    if (key == _backKey)
    {
        menu.addAction(QStringLiteral("Up one level"), this, [this] { _doc->Up(); });
    }
    else if (!id.empty())
    {
        const QString qid = Qs(id);
        if (IsMenu(id))
            menu.addAction(QStringLiteral("Enter"), this, [this, qid] {
                _doc->Enter(qid);
                _doc->Select(QString(), false);
            });
        menu.addAction(QStringLiteral("Duplicate"), this, [this, id] {
            QString err;
            std::string nid;
            if (!_doc->Apply(
                    QStringLiteral("Duplicate"), [&](json& d) { return ops::DuplicateNode(d, id, std::nullopt); }, &err,
                    &nid))
                emit Message(err);
            else
                _doc->Select(Qs(nid), false);
        });
        const bool included = !_doc->IncludedFrom(qid).isEmpty();
        const json* raw = ops::FindNode(_doc->Doc(), id);
        if (raw && raw->value("type", "") == "include")
            menu.addAction(QStringLiteral("Inline (editable copy)"), this, [this, qid] { emit InlineRequested(qid); });
        menu.addAction(QStringLiteral("Export subtree..."), this, [this, qid] { emit ExportRequested(qid); });
        if (!included && raw && raw->value("type", "") != "include")
            menu.addAction(QStringLiteral("Move to file (link)..."), this, [this, qid] { emit LinkRequested(qid); });
        if (included)
        {
            menu.addSeparator();
            menu.addAction(QStringLiteral("(from %1)").arg(_doc->IncludedFrom(qid)))->setEnabled(false);
        }
        menu.addAction(QStringLiteral("Delete"), this, [this, id] {
            QString err;
            if (!_doc->Apply(QStringLiteral("Delete"), [&](json& d) { return ops::RemoveNode(d, id); }, &err))
                emit Message(err);
        });
    }
    else
    {
        menu.addAction(QStringLiteral("Import file as submenu..."), this,
                       [this, key] { emit ImportRequested(key, QStringLiteral("menu")); });
        menu.addAction(QStringLiteral("Link file here (include)..."), this,
                       [this, key] { emit ImportRequested(key, QStringLiteral("link")); });
        menu.addAction(QStringLiteral("Import file items into this menu..."), this,
                       [this, key] { emit ImportRequested(key, QStringLiteral("items")); });
        menu.addSeparator();
        // add a preset here, grouped by category
        QMap<QString, QMenu*> cats;
        for (const auto& pr : Presets())
        {
            QMenu*& sub = cats[pr.category];
            if (!sub)
                sub = menu.addMenu(QStringLiteral("Add ") + pr.category);
            const QString name = pr.name;
            sub->addAction(name, this, [this, key, name] {
                QMimeData mime;
                mime.setData(QString::fromLatin1(kPresetMime), name.toUtf8());
                DropOn(key, &mime);
            });
        }
    }
    if (_doc->CanvasMenu() != Qs(_doc->Doc()["root"].value("id", "home")))
    {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Up one level"), this, [this] { _doc->Up(); });
    }
    menu.exec(e->globalPos());
}
