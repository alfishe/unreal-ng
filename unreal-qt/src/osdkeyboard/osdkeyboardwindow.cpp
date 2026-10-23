#include "osdkeyboardwindow.h"

#include <QComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QSvgRenderer>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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
constexpr int kRefreshMs = 100;  // the model, the route and the host keys: polled, nothing to subscribe to
// Matte and dim (the owner on POC 031: no strong sheen): a light veil over a held key, a thin warm edge
const QColor kHeldFill(255, 255, 255, 46);
const QColor kHeldEdge(255, 214, 140, 150);
const QColor kHostFill(255, 255, 255, 26);

QString SettingsKey(const std::string& model)
{
    return QStringLiteral("OsdKeyboard/picture/") + QString::fromStdString(model);
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
}  // namespace

// region <OsdKeyboardView>

OsdKeyboardView::OsdKeyboardView(QWidget* parent) : QWidget(parent), _svg(new QSvgRenderer(this))
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
    _svg->load(picture ? picture->svgPath : QString());
    _cache = {};
    _recordBase = {};
    _lit.clear();
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
    if (image.isEmpty() || !_svg->isValid())
    {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("No keyboard picture for this machine"));
        return;
    }
    const qreal ratio = devicePixelRatioF();
    const QSize pixels = (image.size() * ratio).toSize();
    if (_cache.size() != pixels)
    {
        _cache = QPixmap(pixels);
        _cache.fill(Qt::transparent);
        QPainter cache(&_cache);
        _svg->render(&cache, QRectF(QPointF(), QSizeF(pixels)));
        _cache.setDevicePixelRatio(ratio);
    }
    painter.drawPixmap(image.topLeft(), _cache);
    PaintLit(painter, *_picture, _lit, image);
}

QImage OsdKeyboardView::renderImage(int width)
{
    if (!_picture || _picture->size.isEmpty() || !_svg->isValid() || width <= 0)
        return {};
    const QSize pixels(width, static_cast<int>(width * _picture->size.height() / _picture->size.width()));
    if (_recordBase.size() != pixels)
    {
        _recordBase = QImage(pixels, QImage::Format_RGBA8888_Premultiplied);
        _recordBase.fill(Qt::transparent);
        QPainter base(&_recordBase);
        _svg->render(&base, QRectF(QPointF(), QSizeF(pixels)));
    }
    QImage image = _recordBase.copy();
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

OsdKeyboardWindow::OsdKeyboardWindow(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::WindowDoesNotAcceptFocus),
      _root(QString::fromStdString(FileHelper::GetResourcesPath()) + "/controllers/keyboards")
{
    setWindowTitle(tr("On-screen Keyboard"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    // A macOS tool window is a panel the system hides while the app is in the background: the keyboard stays
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFocusPolicy(Qt::NoFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    _switch = new QComboBox(this);
    _switch->setFocusPolicy(Qt::NoFocus);
    _switch->setToolTip(tr("This machine's keyboards"));
    layout->addWidget(_switch, 0, Qt::AlignLeft);
    _view = new OsdKeyboardView(this);
    layout->addWidget(_view, 1);
    _notice = new QLabel(this);
    _notice->setWordWrap(true);
    _notice->setStyleSheet("color: palette(placeholder-text);");
    layout->addWidget(_notice);

    connect(_switch, &QComboBox::activated, this,
            [this](int index) { choose(_switch->itemData(index).toString().toStdString()); });
    connect(_view, &OsdKeyboardView::keyPressed, this, &OsdKeyboardWindow::onKeyPressed);
    connect(_view, &OsdKeyboardView::keyReleased, this, &OsdKeyboardWindow::release);

    _timer = new QTimer(this);
    _timer->setInterval(kRefreshMs);
    connect(_timer, &QTimer::timeout, this, &OsdKeyboardWindow::refresh);
    resize(960, 400);
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

void OsdKeyboardWindow::followModel()
{
    if (!_indexLoaded)
        _indexLoaded = _pictures.load(_root);
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
    if (keyboard)
    {
        std::set<ZXKeysEnum> down;
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
    _lit = lit;
    _view->setLit(std::move(lit));
    updateNotice();
}

void OsdKeyboardWindow::loadModel(const std::string& model)
{
    releaseAll();
    _model = model;
    _choices = _pictures.forModel(model);
    _switch->clear();
    for (const std::string& id : _choices)
        _switch->addItem(_pictures.name(id), QString::fromStdString(id));
    _switch->setVisible(_choices.size() > 1);
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
    setWindowTitle(model.empty() ? tr("On-screen Keyboard") : tr("On-screen Keyboard: %1").arg(QString::fromStdString(model)));
}

void OsdKeyboardWindow::choose(const std::string& id)
{
    if (std::find(_choices.begin(), _choices.end(), id) == _choices.end())
        return;
    releaseAll();
    QString error;
    if (!_pictures.picture(id, _picture, error))
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

bool OsdKeyboardWindow::chooseOther(const std::string& id, std::string& error)
{
    if (std::find(_choices.begin(), _choices.end(), id) != _choices.end())
    {
        choose(id);
        return true;
    }
    const std::string matrix = _pictures.matrix(id);
    const std::string own = _choices.empty() ? std::string() : _pictures.matrix(_choices.front());
    if (matrix.empty() || matrix != own)
    {
        error = matrix.empty() ? "no keyboard picture '" + id + "'"
                               : "keyboard '" + id + "' drives the " + matrix + " matrix, this machine the " +
                                     (own.empty() ? std::string("none") : own) + " one";
        return false;
    }
    releaseAll();
    QString loadError;
    KeyboardPicture picture;
    if (!_pictures.picture(id, picture, loadError))
    {
        error = loadError.toStdString();
        return false;
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
    _timer->stop();
    releaseAll();
    emit visibilityChanged(false);
}

// endregion </OsdKeyboardWindow>
