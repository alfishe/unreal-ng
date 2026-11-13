#include "osdkeyboardwindow.h"

#include <QComboBox>
#include <QFile>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "3rdparty/message-center/messagecenter.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/keyboardmanager.h"
#include "emulator/remotecontrolguard.h"

namespace
{
/// How long a pad's tap holds a key (four frames: every ROM's keyboard scan sees it)
constexpr int kTapMs = 80;
constexpr double kOverlayOpacity = 0.78;  ///< the handheld overlay: the game shows through
constexpr int kRefreshMs = 100;  // the model, the route and the host keys: polled, nothing to subscribe to
// Matte and dim (the owner on POC 031: no strong sheen): a light veil over a held key, a thin warm edge
const QColor kHeldFill(255, 255, 255, 46);
const QColor kHeldEdge(255, 214, 140, 150);
const QColor kHostFill(255, 255, 255, 26);

QString SettingsKey(const std::string& model)
{
    return QStringLiteral("OsdKeyboard/picture/") + QString::fromStdString(model);
}

const QString kStyleKey = QStringLiteral("OsdKeyboard/style");
const QString kForcedKey = QStringLiteral("OsdKeyboard/forced");

QString StyleName(const std::string& style)
{
    if (style == "photo")
        return QObject::tr("Photo");
    if (style == "schematic")
        return QObject::tr("Schematic");
    if (style == "hybrid")
        return QObject::tr("Hybrid (hints)");
    return QString::fromStdString(style);
}

/// The lit keys over a picture drawn at @p image: the window's own and the matrix's held keys with an edge, the
/// host keyboard's only veiled
void PaintLit(QPainter& painter, const KeyboardPicture& picture, const std::set<std::string>& lit, const QRectF& image)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(image.topLeft());
    const double scale = image.width() / picture.size.width();
    painter.scale(scale, scale);
    for (const PictureKey& key : picture.keys)
    {
        const bool own = lit.count(key.id) != 0 || lit.count("matrix:" + key.id) != 0;
        const bool host = lit.count("host:" + key.id) != 0;
        if (!own && !host)
            continue;
        QPainterPath path;
        if (key.shape.isEmpty())
            path.addRoundedRect(key.rect, 1.2, 1.2);
        else
            path.addPolygon(key.shape);
        path.closeSubpath();
        painter.fillPath(path, own ? kHeldFill : kHostFill);
        if (own)
        {
            painter.setPen(QPen(kHeldEdge, 0.5));
            painter.drawPath(path);
        }
    }
    painter.restore();
}
/// The minimal keyboard: a picture without an SVG, its keys drawn as plain rounded rectangles with their labels on
/// a dark plate (no case, no photo), at @p image
void PaintVectorKeys(QPainter& painter, const KeyboardPicture& picture, const QRectF& image)
{
    if (picture.size.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const double scale = image.width() / picture.size.width();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x1a, 0x1c, 0x21));
    painter.drawRoundedRect(image, 6, 6);
    QFont font = painter.font();
    font.setBold(true);
    for (const PictureKey& key : picture.keys)
    {
        const QRectF rect(image.left() + key.rect.left() * scale, image.top() + key.rect.top() * scale,
                          key.rect.width() * scale, key.rect.height() * scale);
        QPainterPath path;
        if (key.shape.isEmpty())
            path.addRoundedRect(rect, 1.2 * scale, 1.2 * scale);
        else
        {
            QPolygonF shape;
            for (const QPointF& point : key.shape)
                shape << QPointF(image.left() + point.x() * scale, image.top() + point.y() * scale);
            path.addPolygon(shape);
            path.closeSubpath();
        }
        painter.setPen(QPen(QColor(0x6b, 0x70, 0x78), std::max(1.0, 0.25 * scale)));
        painter.setBrush(key.inert() ? QColor(0x2b, 0x2d, 0x33) : QColor(0x3d, 0x40, 0x47));
        painter.drawPath(path);
        if (key.label.isEmpty())
            continue;
        // The label as large as fits: at most half the key's height, shrunk to its width
        const QRectF text = rect.adjusted(rect.width() * 0.08, 0, -rect.width() * 0.08, 0);
        int pixels = std::max(6, static_cast<int>(rect.height() * 0.42));
        font.setPixelSize(pixels);
        while (pixels > 6 && QFontMetricsF(font).horizontalAdvance(key.label) > text.width())
            font.setPixelSize(--pixels);
        painter.setFont(font);
        painter.setPen(key.inert() ? QColor(0x8b, 0x92, 0x9a) : QColor(0xdc, 0xde, 0xdf));
        painter.drawText(text, Qt::AlignCenter, key.label);
    }
    painter.restore();
}

/// The pad's cursor: the handheld menu's accent ring around the key @p id of a picture drawn at @p image
void PaintFocus(QPainter& painter, const KeyboardPicture& picture, const std::string& id, const QRectF& image)
{
    if (id.empty() || picture.size.isEmpty())
        return;
    for (const PictureKey& key : picture.keys)
        if (key.id == id)
        {
            const double scale = image.width() / picture.size.width();
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.translate(image.topLeft());
            painter.scale(scale, scale);
            QPainterPath path;
            if (key.shape.isEmpty())
                path.addRoundedRect(key.rect.adjusted(-0.6, -0.6, 0.6, 0.6), 1.6, 1.6);
            else
                path.addPolygon(key.shape);
            path.closeSubpath();
            painter.setPen(QPen(QColor(0x1a, 0x9f, 0xff), 3.0 / scale));
            painter.setBrush(QColor(0x1a, 0x9f, 0xff, 50));
            painter.drawPath(path);
            painter.restore();
            return;
        }
}
}  // namespace

// region <OsdKeyboardView>

OsdKeyboardView::OsdKeyboardView(QWidget* parent)
    : QWidget(parent), _svg(new QSvgRenderer(this)), _dimSvg(new QSvgRenderer(this))
{
    setFocusPolicy(Qt::NoFocus);
    setMinimumSize(320, 120);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void OsdKeyboardView::setPicture(const KeyboardPicture* picture)
{
    _picture = picture;
    _svgData.clear();
    if (QFile file(picture ? picture->svgPath : QString()); picture && file.open(QIODevice::ReadOnly))
        _svgData = file.readAll();
    _svg->load(_svgData);
    _dimSvg->load(QByteArray());
    _dimmed.clear();
    _cache[0] = _cache[1] = {};
    _recordBase[0] = _recordBase[1] = {};
    _lit.clear();
    _focusKey.clear();
    updateGeometry();
    update();
}

void OsdKeyboardView::setLit(std::set<std::string> lit)
{
    if (lit == _lit)
        return;
    _lit = std::move(lit);
    update();
}

void OsdKeyboardView::setDimmed(std::set<std::string> roles)
{
    if (roles == _dimmed)
        return;
    _dimmed = std::move(roles);
    if (!_dimmed.empty())
    {
        // A faint group, not a hidden one: the legend stays readable, the ones the shift brings forward stand out
        QByteArray data = _svgData;
        for (const std::string& role : _dimmed)
        {
            const QByteArray group = "<g id=\"legends-" + QByteArray::fromStdString(role) + "\"";
            data.replace(group, group + " opacity=\"0.3\"");
        }
        _dimSvg->load(data);
    }
    _cache[1] = {};
    _recordBase[1] = {};
    update();
}

void OsdKeyboardView::setFocusKey(const std::string& id)
{
    if (id == _focusKey)
        return;
    _focusKey = id;
    update();
}

QSvgRenderer* OsdKeyboardView::renderer() const
{
    return _dimmed.empty() ? _svg : _dimSvg;
}

bool OsdKeyboardView::vector() const
{
    return _picture != nullptr && _picture->svgPath.isEmpty() && !_picture->keys.empty();
}

bool OsdKeyboardView::drawable() const
{
    return vector() || renderer()->isValid();
}

QSize OsdKeyboardView::sizeHint() const
{
    return {900, heightForWidth(900)};
}

int OsdKeyboardView::heightForWidth(int width) const
{
    if (!_picture || _picture->size.isEmpty())
        return width / 3;
    return static_cast<int>(width * _picture->size.height() / _picture->size.width());
}

QRectF OsdKeyboardView::imageRect() const
{
    if (!_picture || _picture->size.isEmpty())
        return {};
    QSizeF size = _picture->size;
    size.scale(QSizeF(this->size()), Qt::KeepAspectRatio);
    return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
}

QPointF OsdKeyboardView::toPicture(const QPointF& point) const
{
    const QRectF image = imageRect();
    if (image.isEmpty())
        return {-1, -1};
    const double scale = _picture->size.width() / image.width();
    return (point - image.topLeft()) * scale;
}

void OsdKeyboardView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRectF image = imageRect();
    if (image.isEmpty() || !drawable())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No keyboard picture for this machine"));
        return;
    }
    const qreal ratio = devicePixelRatioF();
    const QSize pixels = (image.size() * ratio).toSize();
    QPixmap& picture = _cache[_dimmed.empty() ? 0 : 1];
    if (picture.size() != pixels)
    {
        picture = QPixmap(pixels);
        picture.fill(Qt::transparent);
        QPainter cache(&picture);
        if (vector())
            PaintVectorKeys(cache, *_picture, QRectF(QPointF(), QSizeF(pixels)));
        else
            renderer()->render(&cache, QRectF(QPointF(), QSizeF(pixels)));
        picture.setDevicePixelRatio(ratio);
    }
    painter.drawPixmap(image.topLeft(), picture);
    PaintLit(painter, *_picture, _lit, image);
    PaintFocus(painter, *_picture, _focusKey, image);
}

QImage OsdKeyboardView::renderImage(int width)
{
    if (!_picture || _picture->size.isEmpty() || !drawable() || width <= 0)
        return {};
    const QSize pixels(width, static_cast<int>(width * _picture->size.height() / _picture->size.width()));
    QImage& recordBase = _recordBase[_dimmed.empty() ? 0 : 1];
    if (recordBase.size() != pixels)
    {
        recordBase = QImage(pixels, QImage::Format_RGBA8888_Premultiplied);
        recordBase.fill(Qt::transparent);
        QPainter base(&recordBase);
        if (vector())
            PaintVectorKeys(base, *_picture, QRectF(QPointF(), QSizeF(pixels)));
        else
            renderer()->render(&base, QRectF(QPointF(), QSizeF(pixels)));
    }
    QImage image = recordBase.copy();
    QPainter painter(&image);
    PaintLit(painter, *_picture, _lit, QRectF(QPointF(), QSizeF(pixels)));
    return image;
}

void OsdKeyboardView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !_picture)
        return;
    if (const PictureKey* key = _picture->keyAt(toPicture(event->position())))
        emit keyPressed(key);
}

void OsdKeyboardView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit keyReleased();
}

// endregion </OsdKeyboardView>

// region <OsdKeyboardWindow>

OsdKeyboardWindow::OsdKeyboardWindow(QWidget* parent, bool overlay)
    : QWidget(parent, overlay ? Qt::Widget : Qt::Tool | Qt::WindowDoesNotAcceptFocus),
      _root(QString::fromStdString(FileHelper::GetResourcesPath()) + "/controllers/keyboards"), _overlay(overlay)
{
    setWindowTitle(tr("On-screen Keyboard"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    // A macOS tool window is a panel the system hides while the app is in the background: the keyboard stays
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* switches = new QHBoxLayout();
    _switch = new QComboBox(this);
    _switch->setFocusPolicy(Qt::NoFocus);
    _switch->setToolTip(tr("This machine's keyboards"));
    switches->addWidget(_switch);
    _styleSwitch = new QComboBox(this);
    _styleSwitch->setFocusPolicy(Qt::NoFocus);
    _styleSwitch->setToolTip(tr("How the keyboard is drawn; the hybrid shows what a combination key presses"));
    switches->addWidget(_styleSwitch);
    switches->addStretch(1);
    layout->addLayout(switches);
    _view = new OsdKeyboardView(this);
    layout->addWidget(_view, 1);
    _notice = new QLabel(this);
    _notice->setWordWrap(true);
    _notice->setStyleSheet("color: palette(placeholder-text);");
    layout->addWidget(_notice);

    connect(_switch, &QComboBox::activated, this,
            [this](int index) { choose(_switch->itemData(index).toString().toStdString()); });
    connect(_styleSwitch, &QComboBox::activated, this,
            [this](int index) { chooseStyle(_styleSwitch->itemData(index).toString().toStdString()); });
    connect(_view, &OsdKeyboardView::keyPressed, this, &OsdKeyboardWindow::onKeyPressed);
    connect(_view, &OsdKeyboardView::keyReleased, this, &OsdKeyboardWindow::release);

    _timer = new QTimer(this);
    _timer->setInterval(kRefreshMs);
    connect(_timer, &QTimer::timeout, this, &OsdKeyboardWindow::refresh);
    if (overlay)
    {
        // Drawn by the screen over the game (paintOverlay), never a widget on screen; Settings chooses the keyboard
        _switch->hide();
        _styleSwitch->hide();
        QWidget::setVisible(false);
        connect(this, &OsdKeyboardWindow::pictureChanged, this, &OsdKeyboardWindow::overlayDirty);
    }
    else
        resize(960, 400);
    _forced = QSettings().value(kForcedKey).toString().toStdString();
}

OsdKeyboardWindow::~OsdKeyboardWindow()
{
    releaseAll();
}

void OsdKeyboardWindow::setBinding(EmulatorBinding* binding)
{
    _binding = binding;
    refresh();
}

void OsdKeyboardWindow::setRoot(const QString& root)
{
    _root = root;
    _indexLoaded = false;
    _model.clear();
    refresh();
}

EmulatorContext* OsdKeyboardWindow::context() const
{
    return _binding ? _binding->context() : nullptr;  // for this call only, never stored
}

std::string OsdKeyboardWindow::modelName() const
{
    EmulatorContext* ctx = context();
    if (!ctx)
        return {};
    if (EmulatorManager* manager = EmulatorManager::GetInstance())
        for (const TMemModel& model : manager->GetAvailableModels())
            if (model.Model == ctx->config.mem_model)
                return model.ShortName;
    return {};
}

void OsdKeyboardWindow::loadIndex()
{
    _indexLoaded = _pictures.load(_root);
    const std::vector<std::string>& styles = _pictures.styles();
    const std::string kept = QSettings().value(kStyleKey).toString().toStdString();
    _style = std::find(styles.begin(), styles.end(), kept) != styles.end() ? kept : styles.front();
    _styleSwitch->clear();
    for (const std::string& style : styles)
        _styleSwitch->addItem(StyleName(style), QString::fromStdString(style));
    _styleSwitch->setCurrentIndex(_styleSwitch->findData(QString::fromStdString(_style)));
    _styleSwitch->setVisible(!_overlay && styles.size() > 1);
}

void OsdKeyboardWindow::followModel()
{
    if (!_indexLoaded)
        loadIndex();
    const std::string model = modelName();
    if (model != _model)
        loadModel(model);
}

void OsdKeyboardWindow::refresh()
{
    followModel();

    std::set<std::string> lit = held();
    for (const PictureKey& key : _picture.keys)
        if (key.pc != PcKey::None && KeyboardManager::pcKeyHeld(key.pc))
            lit.insert("host:" + key.id);
    // A host key held on the matrix may be an extended key (host Left: Caps Shift + 5): the picture's key printed
    // with that combination lights (the 128's LEFT), or, where there is none (the 48K), its keys one by one
    EmulatorContext* ctx = context();
    Keyboard* keyboard = ctx ? ctx->pKeyboard : nullptr;
    for (uint8_t held : keyboard ? KeyboardManager::heldMatrixKeys() : std::multiset<uint8_t>{})
    {
        const auto zx = static_cast<ZXKeysEnum>(held);
        const ZXKeysEnum base = keyboard->GetMatrixKey(zx);
        const ZXKeysEnum modifier = keyboard->GetMatrixModifier(zx);
        std::vector<ZXKeysEnum> parts{base};
        if (modifier != ZXKEY_NONE && modifier != base)
            parts.insert(parts.begin(), modifier);
        // The key printed with it: the extended key itself (the Next's LEFT) or its combination (the 128's)
        const auto combo = std::find_if(_picture.keys.begin(), _picture.keys.end(), [&](const PictureKey& key) {
            return key.zx == parts || (key.zx.size() == 1 && key.zx[0] == zx);
        });
        if (combo != _picture.keys.end())
        {
            lit.insert("host:" + combo->id);
            continue;
        }
        for (const PictureKey& key : _picture.keys)
            if (key.zx.size() == 1 && std::find(parts.begin(), parts.end(), key.zx[0]) != parts.end())
                lit.insert("host:" + key.id);
    }
    // Every key the matrix holds, whoever pressed it (automation, the host, this window): a key printed with exactly
    // the held combination lights instead of its parts (Caps Shift + 5 is the Pentagon's LEFT)
    std::set<ZXKeysEnum> down;
    if (keyboard)
    {
        for (const PictureKey& key : _picture.keys)
        {
            uint8_t row = 0;
            uint8_t bit = 0;
            if (key.zx.size() == 1 && Keyboard::MatrixPosition(key.zx[0], row, bit) &&
                (keyboard->MatrixRow(row) & (1u << bit)) == 0)
                down.insert(key.zx[0]);
        }
        bool combo = false;
        for (const PictureKey& key : _picture.keys)
            if (key.zx.size() > 1 && key.zx.size() == down.size() &&
                std::all_of(key.zx.begin(), key.zx.end(), [&](ZXKeysEnum zx) { return down.count(zx) != 0; }))
            {
                lit.insert("matrix:" + key.id);
                combo = true;
            }
        if (!combo)
            for (const PictureKey& key : _picture.keys)
                if (key.zx.size() == 1 && down.count(key.zx[0]) != 0)
                    lit.insert("matrix:" + key.id);
    }
    const bool litChanged = lit != _lit;
    _lit = lit;
    _view->setLit(std::move(lit));
    // Symbol Shift held alone: what it types is the symbols, the other mode legends go faint. Both shifts are the
    // Extend Mode switch: nothing to bring forward yet (the mode itself lives in the ROM, not on the matrix)
    static const std::set<std::string> notSymbols{"keyword", "extend", "extend-shift", "graphic", "colour", "cyrillic"};
    const bool symbols = down.count(ZXKEY_SYM_SHIFT) != 0 && down.count(ZXKEY_CAPS_SHIFT) == 0;
    const std::set<std::string> dimmed = symbols ? notSymbols : std::set<std::string>{};
    const bool dimmedChanged = dimmed != _view->dimmed();
    _view->setDimmed(dimmed);
    updateNotice();
    if (litChanged || dimmedChanged)
        overlayDirty();
}

void OsdKeyboardWindow::loadModel(const std::string& model)
{
    releaseAll();
    _model = model;
    _choices = _pictures.forModel(model);
    _switch->clear();
    for (const std::string& id : _choices)
        _switch->addItem(_pictures.name(id), QString::fromStdString(id));
    _switch->setVisible(!_overlay && _choices.size() > 1);
    std::string chosen = _choices.empty() ? std::string() : _choices.front();
    const std::string kept = QSettings().value(SettingsKey(model)).toString().toStdString();
    if (std::find(_choices.begin(), _choices.end(), kept) != _choices.end())
        chosen = kept;
    _picture = {};
    _view->setPicture(nullptr);
    if (!chosen.empty())
        choose(chosen);
    else
        emit pictureChanged();
    applyForced();
    setWindowTitle(model.empty() ? tr("On-screen Keyboard") : tr("On-screen Keyboard: %1").arg(QString::fromStdString(model)));
}

void OsdKeyboardWindow::choose(const std::string& id)
{
    if (std::find(_choices.begin(), _choices.end(), id) == _choices.end())
        return;
    releaseAll();
    QString error;
    if (!_pictures.picture(id, _picture, error, _style))
    {
        _picture = {};
        _view->setPicture(nullptr);
        _keyNotice = error;
        updateNotice();
        return;
    }
    _keyNotice.clear();
    _view->setPicture(&_picture);
    _switch->setCurrentIndex(_switch->findData(QString::fromStdString(id)));
    if (!_model.empty() && id != _choices.front())
        QSettings().setValue(SettingsKey(_model), QString::fromStdString(id));
    else if (!_model.empty())
        QSettings().remove(SettingsKey(_model));
    updateNotice();
    emit pictureChanged();
}

void OsdKeyboardWindow::chooseStyle(const std::string& style)
{
    if (!_indexLoaded)
        loadIndex();
    const std::vector<std::string>& styles = _pictures.styles();
    if (std::find(styles.begin(), styles.end(), style) == styles.end())
        return;
    _style = style;
    _styleSwitch->setCurrentIndex(_styleSwitch->findData(QString::fromStdString(style)));
    if (style != styles.front())
        QSettings().setValue(kStyleKey, QString::fromStdString(style));
    else
        QSettings().remove(kStyleKey);
    if (_picture.id.empty() || _picture.style == style)
        return;
    // The same picture (a chosen one or another model's), only drawn anew: the key map stays, the held keys go up
    releaseAll();
    KeyboardPicture picture;
    QString error;
    if (!_pictures.picture(_picture.id, picture, error, style))
        return;
    _picture = std::move(picture);
    _view->setPicture(&_picture);
    updateNotice();
    emit pictureChanged();
}

bool OsdKeyboardWindow::chooseOther(const std::string& id, std::string& error, bool vectorOnly)
{
    if (!vectorOnly && std::find(_choices.begin(), _choices.end(), id) != _choices.end())
    {
        choose(id);
        return true;
    }
    // A keyboard of another machine fits when it drives one of this machine's matrices (the Sprinter has both the
    // PC keyboard and the ZX matrix)
    const std::string matrix = _pictures.matrix(id);
    std::string own;
    bool fits = false;
    for (const std::string& choice : _choices)
    {
        const std::string theirs = _pictures.matrix(choice);
        fits = fits || (!matrix.empty() && theirs == matrix);
        if (own.find(theirs) == std::string::npos)
            own += (own.empty() ? "" : " / ") + theirs;
    }
    if (!fits)
    {
        error = matrix.empty() ? "no keyboard picture '" + id + "'"
                               : "keyboard '" + id + "' drives the " + matrix + " matrix, this machine the " +
                                     (own.empty() ? std::string("none") : own) + " one";
        return false;
    }
    releaseAll();
    QString loadError;
    KeyboardPicture picture;
    if (!_pictures.picture(id, picture, loadError, _style))
    {
        error = loadError.toStdString();
        return false;
    }
    if (vectorOnly)
    {
        // The minimal keyboard: the layout's keys drawn as plain rectangles, no picture
        picture.svgPath.clear();
        picture.name = tr("Minimal");
    }
    _picture = std::move(picture);
    _keyNotice.clear();
    _view->setPicture(&_picture);
    updateNotice();
    emit pictureChanged();
    return true;
}

void OsdKeyboardWindow::press(const std::string& id)
{
    for (const PictureKey& key : _picture.keys)
        if (key.id == id)
            return onKeyPressed(&key);
}

bool OsdKeyboardWindow::setForced(const std::string& id, std::string& error)
{
    if (!_indexLoaded)
        loadIndex();
    if (!id.empty() && id != "minimal" && id != "steam" && _pictures.matrix(id).empty())
    {
        error = "no keyboard picture '" + id + "'";
        return false;
    }
    _forced = id;
    if (id.empty())
        QSettings().remove(kForcedKey);
    else
        QSettings().setValue(kForcedKey, QString::fromStdString(id));
    if (id.empty() || id == "steam")
        loadModel(_model);  // the machine's own again
    else
        applyForced();
    return true;
}

std::vector<std::pair<std::string, QString>> OsdKeyboardWindow::allPictures()
{
    if (!_indexLoaded)
        loadIndex();
    std::vector<std::pair<std::string, QString>> list;
    for (const std::string& id : _pictures.ids())
        list.emplace_back(id, _pictures.name(id));
    return list;
}

std::vector<std::pair<std::string, QString>> OsdKeyboardWindow::fittingPictures()
{
    std::vector<std::pair<std::string, QString>> list;
    for (auto& [id, name] : allPictures())
    {
        const std::string matrix = _pictures.matrix(id);
        for (const std::string& choice : _choices)
            if (!matrix.empty() && _pictures.matrix(choice) == matrix)
            {
                list.emplace_back(id, name);
                break;
            }
    }
    return list;
}

void OsdKeyboardWindow::applyForced()
{
    if (_forced.empty() || _forced == "steam" || _choices.empty())
        return;
    std::string error;
    // Minimal: the 40-key ZX layout where the machine has the ZX matrix, else the machine's own layout
    std::string id = _forced;
    if (_forced == "minimal")
    {
        id = _choices.front();
        for (const std::string& choice : _choices)
            if (_pictures.matrix(choice) == "zx")
                id = "zxmatrix";
    }
    if (!chooseOther(id, error, _forced == "minimal"))
    {
        _keyNotice = tr("The chosen keyboard does not fit this machine: %1").arg(QString::fromStdString(error));
        updateNotice();
    }
}

void OsdKeyboardWindow::moveFocus(int dx, int dy)
{
    if (_picture.keys.empty())
        return;
    const PictureKey* from = nullptr;
    for (const PictureKey& key : _picture.keys)
        if (key.id == _focusKey)
            from = &key;
    const PictureKey* best = nullptr;
    if (from == nullptr)
    {
        // No cursor yet: the key nearest the picture's middle
        const QPointF middle(_picture.size.width() / 2, _picture.size.height() / 2);
        double nearest = 0;
        for (const PictureKey& key : _picture.keys)
        {
            const QPointF d = key.rect.center() - middle;
            const double distance = d.x() * d.x() + d.y() * d.y();
            if (best == nullptr || distance < nearest)
            {
                best = &key;
                nearest = distance;
            }
        }
    }
    else
    {
        // The nearest key whose centre lies in the direction; across the direction counts three times as much, so a
        // row's next key wins over the key diagonally below it
        const QPointF origin = from->rect.center();
        double nearest = 0;
        for (const PictureKey& key : _picture.keys)
        {
            if (&key == from)
                continue;
            const QPointF d = key.rect.center() - origin;
            const double along = d.x() * dx + d.y() * dy;
            if (along <= 0.5)
                continue;
            const double across = std::abs(dx != 0 ? d.y() : d.x());
            const double cost = along + 3 * across;
            if (best == nullptr || cost < nearest)
            {
                best = &key;
                nearest = cost;
            }
        }
    }
    if (best == nullptr)
        return;
    _focusKey = best->id;
    _view->setFocusKey(_focusKey);
    if (_overlay)
        emit overlayChanged();
}

bool OsdKeyboardWindow::tapFocus()
{
    const PictureKey* focused = nullptr;
    for (const PictureKey& key : _picture.keys)
        if (key.id == _focusKey)
            focused = &key;
    if (focused == nullptr)
        return false;
    onKeyPressed(focused);
    // A modifier stays latched for the next key; any other key goes up after a tap long enough for every ROM's scan
    if (!focused->modifier())
        QTimer::singleShot(kTapMs, this, [this] { release(); });
    return true;
}

void OsdKeyboardWindow::onKeyPressed(const PictureKey* key)
{
    _keyNotice.clear();
    if (key->inert())
    {
        // A key whose wiring nobody knows (the Forum's extra keys) is silent; a known line not taken yet says so
        if (key->line.empty())
        {
            updateNotice();
            return;
        }
        _keyNotice = tr("%1 is wired to %2, which the emulator does not take yet")
                         .arg(key->label, QString::fromStdString(key->line));
        updateNotice();
        return;
    }
    if (key->modifier())
    {
        auto it = std::find(_latched.begin(), _latched.end(), key);
        if (it != _latched.end())
        {
            post(*key, false);
            _latched.erase(it);
        }
        else
        {
            post(*key, true);
            _latched.push_back(key);
        }
    }
    else
    {
        if (_down)
            post(*_down, false);
        _down = key;
        post(*key, true);
    }
    refresh();
}

void OsdKeyboardWindow::release()
{
    if (!_down)
        return;
    post(*_down, false);
    _down = nullptr;
    // The latched modifiers went with the key: let them go, the last latched first
    for (auto it = _latched.rbegin(); it != _latched.rend(); ++it)
        post(**it, false);
    _latched.clear();
    refresh();
}

void OsdKeyboardWindow::releaseAll()
{
    if (_down)
        post(*_down, false);
    _down = nullptr;
    for (auto it = _latched.rbegin(); it != _latched.rend(); ++it)
        post(**it, false);
    _latched.clear();
}

std::set<std::string> OsdKeyboardWindow::held() const
{
    std::set<std::string> ids;
    if (_down)
        ids.insert(_down->id);
    for (const PictureKey* key : _latched)
        ids.insert(key->id);
    return ids;
}

void OsdKeyboardWindow::post(const PictureKey& key, bool pressed)
{
    if (pressed)
    {
        Emulator* emulator = _binding ? _binding->emulator() : nullptr;
        // An automation session holds the machine: no keys from the GUI (releases still go: nothing stays held)
        if (!emulator || RemoteControlGuard::InputBlocked(emulator))
            return;
        _target = emulator->GetUUID().toString();
    }
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    const KeyEventEnum type = pressed ? KEY_PRESSED : KEY_RELEASED;
    if (key.pc != PcKey::None)
        messageCenter.Post(pressed ? MC_PCKEY_PRESSED : MC_PCKEY_RELEASED,
                           new PcKeyEvent(static_cast<uint8_t>(key.pc), type, _target));
    // A combination (True Video: CS + 3) goes down in order and up in reverse, as fingers do
    const auto send = [&](ZXKeysEnum zx) {
        messageCenter.Post(pressed ? MC_KEY_PRESSED : MC_KEY_RELEASED, new KeyboardEvent(zx, type, _target));
    };
    if (pressed)
        std::for_each(key.zx.begin(), key.zx.end(), send);
    else
        std::for_each(key.zx.rbegin(), key.zx.rend(), send);
}

QString OsdKeyboardWindow::notice() const
{
    return _notice->text();
}

void OsdKeyboardWindow::updateNotice()
{
    QString text = _keyNotice;
    EmulatorContext* ctx = context();
    Keyboard* keyboard = ctx ? ctx->pKeyboard : nullptr;
    if (text.isEmpty() && !ctx)
        text = tr("No machine");
    else if (text.isEmpty() && keyboard && !_picture.keys.empty())
    {
        if (_picture.pc && !keyboard->HasPs2Sink())
            text = tr("This machine has no PC keyboard controller in the emulator yet: the PC keys reach nothing");
        else if (_picture.pc && !keyboard->RoutesToPs2())
            text = tr("Host Keyboard is set to the matrix: the PC keys do not reach the keyboard controller");
        else if (!_picture.pc && !keyboard->RoutesToMatrix())
            text = tr("Host Keyboard is set to PS/2: the matrix keys do not reach the machine");
    }
    if (_notice->text() != text)
        _notice->setText(text);
    _notice->setVisible(!text.isEmpty());
}

void OsdKeyboardWindow::setVisible(bool visible)
{
    if (!_overlay)
    {
        QWidget::setVisible(visible);
        return;
    }
    if (visible == _overlayShown)
        return;
    _overlayShown = visible;
    if (visible)
    {
        _timer->start();
        refresh();
    }
    else
    {
        _timer->stop();
        releaseAll();
        // The pad's cursor starts afresh next time: no key highlighted until the first move
        _focusKey.clear();
        _view->setFocusKey({});
    }
    overlayDirty();
    emit visibilityChanged(visible);
}

void OsdKeyboardWindow::overlayDirty()
{
    if (!_overlay)
        return;
    _overlayImage = {};
    emit overlayChanged();
}

QRectF OsdKeyboardWindow::overlayTarget(int width, int height) const
{
    // The whole width of the screen by the picture's ratio, at the bottom: on the Deck's small screen the keys are
    // big enough to hit with a finger and to read. TUNING: a smaller keyboard - scale w here (e.g. width * 0.8)
    const double aspect = _picture.size.width() / _picture.size.height();
    double w = width;
    double h = w / aspect;
    if (h > height)
    {
        h = height;
        w = h * aspect;
    }
    return QRectF((width - w) / 2, height - h, w, h);
}

bool OsdKeyboardWindow::overlayPointer(QEvent::Type type, const QPointF& at, int width, int height)
{
    if (!_overlay || !_overlayShown || _picture.keys.empty() || _picture.size.isEmpty() || width <= 0 || height <= 0)
        return false;
    if (type == QEvent::MouseButtonPress)
    {
        const QRectF target = overlayTarget(width, height);
        const QPointF mm((at.x() - target.left()) * _picture.size.width() / target.width(),
                         (at.y() - target.top()) * _picture.size.height() / target.height());
        ++_pointerPress;
        _pointerDown.start();
        if (const PictureKey* key = target.contains(at) ? _picture.keyAt(mm) : nullptr)
            onKeyPressed(key);
    }
    else if (type == QEvent::MouseButtonRelease)
    {
        // A quick tap still holds the key long enough for every ROM's scan
        const qint64 held = _pointerDown.isValid() ? _pointerDown.elapsed() : kTapMs;
        if (held >= kTapMs)
            release();
        else
            QTimer::singleShot(static_cast<int>(kTapMs - held), this, [this, press = _pointerPress] {
                if (press == _pointerPress)
                    release();
            });
    }
    return true;
}

void OsdKeyboardWindow::paintOverlay(QPainter& painter, int width, int height)
{
    if (!_overlayShown || _picture.keys.empty() || _picture.size.isEmpty() || width <= 0 || height <= 0)
        return;
    const QRectF target = overlayTarget(width, height);
    const double w = target.width();
    if (_overlayImage.isNull() || _overlayImage.width() != static_cast<int>(w))
        _overlayImage = _view->renderImage(static_cast<int>(w));
    if (_overlayImage.isNull())
        return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Translucent: the game shows through the keyboard
    painter.setOpacity(kOverlayOpacity);
    painter.drawImage(target, _overlayImage);
    painter.setOpacity(1.0);
    PaintFocus(painter, _picture, _focusKey, target);
    painter.restore();
}

void OsdKeyboardWindow::tapMatrixKey(ZXKeysEnum zx)
{
    PictureKey key;
    key.zx = {zx};
    post(key, true);
    QTimer::singleShot(kTapMs, this, [this, key] { post(key, false); });
}

void OsdKeyboardWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    _timer->start();
    refresh();
    emit visibilityChanged(true);
}

void OsdKeyboardWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (_overlay)
        return;  // the overlay's own hide is setVisible's
    _timer->stop();
    releaseAll();
    // The pad's cursor starts afresh next time: no key highlighted until the first move
    _focusKey.clear();
    _view->setFocusKey({});
    emit visibilityChanged(false);
}

// endregion </OsdKeyboardWindow>
