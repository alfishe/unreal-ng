#define GL_SILENCE_DEPRECATION
#include "TileGridGL.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QOpenGLContext>
#include <QPainter>
#include <QUrl>
#include <cmath>

#include <3rdparty/message-center/messagecenter.h>
#include <emulator/emulatorcontext.h>
#include <emulator/notifications.h>
#include <emulator/video/screen.h>
#include <emulatormanager.h>
#include "keyboard/keyboardmanager.h"

static const char* TILE_VERTEX_SHADER = R"(
#version 120
attribute vec2 position;
attribute vec2 texCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    vTexCoord = texCoord;
}
)";

static const char* TILE_FRAGMENT_SHADER = R"(
#version 120
varying vec2 vTexCoord;
uniform sampler2D tileTexture;

void main() {
    gl_FragColor = texture2D(tileTexture, vTexCoord);
}
)";

TileGridGL::TileGridGL(QWindow* parent)
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate, parent)
{
    QSurfaceFormat fmt;
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setSwapInterval(1);
    setFormat(fmt);
}

TileGridGL::~TileGridGL()
{
    unsubscribeFromNotifications();

    makeCurrent();
    for (auto& tile : _emulators)
    {
        delete tile.texture;
    }
    delete _shader;
    doneCurrent();
}

void TileGridGL::addEmulator(std::shared_ptr<Emulator> emulator)
{
    if (!emulator)
        return;

    TileState state;
    state.emulator = emulator;
    state.emulatorId = emulator->GetUUID().toString();
    state.latchedFrame = QImage(FB_WIDTH, FB_HEIGHT, QImage::Format_RGBA8888);
    state.latchedFrame.fill(Qt::black);

    _emulators.push_back(std::move(state));
    updateLayout();
    update();
}

void TileGridGL::removeEmulator(const std::string& emulatorId)
{
    auto it = std::find_if(_emulators.begin(), _emulators.end(),
        [&](const TileState& s) { return s.emulatorId == emulatorId; });

    if (it != _emulators.end())
    {
        makeCurrent();
        delete it->texture;
        doneCurrent();

        int index = static_cast<int>(std::distance(_emulators.begin(), it));
        _emulators.erase(it);

        if (_focusedIndex == index)
            _focusedIndex = -1;
        else if (_focusedIndex > index)
            _focusedIndex--;

        updateLayout();
        update();
    }
}

void TileGridGL::clearAllEmulators()
{
    EmulatorManager* manager = EmulatorManager::GetInstance();

    for (auto& tile : _emulators)
    {
        if (manager && tile.emulator)
        {
            manager->StopEmulator(tile.emulatorId);
        }
    }

    makeCurrent();
    for (auto& tile : _emulators)
    {
        delete tile.texture;
        if (manager && !tile.emulatorId.empty())
        {
            manager->RemoveEmulator(tile.emulatorId);
        }
    }
    doneCurrent();

    _emulators.clear();
    _focusedIndex = -1;
    update();
}

std::shared_ptr<Emulator> TileGridGL::emulatorAt(int index) const
{
    if (index >= 0 && index < static_cast<int>(_emulators.size()))
        return _emulators[index].emulator;
    return nullptr;
}

std::vector<std::string> TileGridGL::emulatorIds() const
{
    std::vector<std::string> ids;
    ids.reserve(_emulators.size());
    for (const auto& tile : _emulators)
        ids.push_back(tile.emulatorId);
    return ids;
}

void TileGridGL::setGridDimensions(int cols, int rows)
{
    _explicitCols = cols;
    _explicitRows = rows;
    updateLayout();
    update();
}

void TileGridGL::setSingleSyncMode(bool enable)
{
    _singleSyncMode = enable;
    update();
}

void TileGridGL::setSyncEmulatorId(const std::string& emulatorId)
{
    _syncEmulatorId = emulatorId;
    unsubscribeFromNotifications();
    if (!_syncEmulatorId.empty())
    {
        subscribeToNotifications();
    }
}

void TileGridGL::repaintAllTiles()
{
    for (auto& tile : _emulators)
        tile.needsUpdate = true;
    update();
}

void TileGridGL::setFocusedIndex(int index)
{
    if (index >= -1 && index < static_cast<int>(_emulators.size()))
    {
        _focusedIndex = index;
        update();
    }
}

int TileGridGL::emulatorIndexAt(const QPoint& pos) const
{
    if (_emulators.empty() || _currentCols == 0)
        return -1;

    int col = pos.x() / TILE_WIDTH;
    int row = pos.y() / TILE_HEIGHT;
    int index = row * _currentCols + col;

    if (index >= 0 && index < static_cast<int>(_emulators.size()))
        return index;
    return -1;
}

void TileGridGL::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    _shader = new QOpenGLShaderProgram();
    _shader->addShaderFromSourceCode(QOpenGLShader::Vertex, TILE_VERTEX_SHADER);
    _shader->addShaderFromSourceCode(QOpenGLShader::Fragment, TILE_FRAGMENT_SHADER);
    _shader->link();
}

void TileGridGL::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    updateLayout();
}

void TileGridGL::updateLayout()
{
    if (_emulators.empty())
    {
        _currentCols = 0;
        _currentRows = 0;
        return;
    }

    if (_explicitCols > 0 && _explicitRows > 0)
    {
        _currentCols = _explicitCols;
        _currentRows = _explicitRows;
    }
    else
    {
        TileLayoutManager::GridLayout layout = TileLayoutManager::calculateLayout(_emulators.size());
        _currentCols = layout.cols;
        _currentRows = layout.rows;
    }
}

void TileGridGL::updateTextures()
{
    for (auto& tile : _emulators)
    {
        if (!tile.emulator || !tile.needsUpdate)
            continue;

        EmulatorContext* ctx = tile.emulator->GetContext();
        Screen* screen = ctx ? ctx->pScreen : nullptr;
        if (!screen)
            continue;

        auto& desc = screen->GetFramebufferDescriptor();
        if (desc.width == 0 || desc.height == 0)
            continue;

        if (tile.latchedFrame.width() != static_cast<int>(desc.width) ||
            tile.latchedFrame.height() != static_cast<int>(desc.height))
        {
            tile.latchedFrame = QImage(desc.width, desc.height, QImage::Format_RGBA8888);
        }

        if (!screen->CopyPresentedFramebuffer(tile.latchedFrame.bits(),
                                               static_cast<size_t>(tile.latchedFrame.sizeInBytes())))
        {
            continue;
        }

        if (!tile.texture)
        {
            tile.texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            tile.texture->setMinificationFilter(QOpenGLTexture::Nearest);
            tile.texture->setMagnificationFilter(QOpenGLTexture::Nearest);
            tile.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }

        if (!tile.texture->isCreated())
        {
            tile.texture->setData(tile.latchedFrame.flipped(Qt::Vertical));
        }
        else
        {
            tile.texture->bind();
            QImage flipped = tile.latchedFrame.flipped(Qt::Vertical);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                flipped.width(), flipped.height(),
                GL_RGBA, GL_UNSIGNED_BYTE, flipped.constBits());
        }

        tile.needsUpdate = false;
    }
}

void TileGridGL::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (_emulators.empty() || _currentCols == 0)
        return;

    updateTextures();

    _shader->bind();

    float winW = static_cast<float>(width());
    float winH = static_cast<float>(height());

    int index = 0;
    for (int row = 0; row < _currentRows && index < static_cast<int>(_emulators.size()); ++row)
    {
        for (int col = 0; col < _currentCols && index < static_cast<int>(_emulators.size()); ++col, ++index)
        {
            auto& tile = _emulators[index];
            if (!tile.texture || !tile.texture->isCreated())
                continue;

            float x0 = (col * TILE_WIDTH) / winW * 2.0f - 1.0f;
            float y0 = 1.0f - (row * TILE_HEIGHT) / winH * 2.0f;
            float x1 = ((col + 1) * TILE_WIDTH) / winW * 2.0f - 1.0f;
            float y1 = 1.0f - ((row + 1) * TILE_HEIGHT) / winH * 2.0f;

            // Extract 256x192 from 352x288 framebuffer
            float srcX0 = 48.0f / FB_WIDTH;
            float srcY0 = 48.0f / FB_HEIGHT;
            float srcX1 = (48.0f + 256.0f) / FB_WIDTH;
            float srcY1 = (48.0f + 192.0f) / FB_HEIGHT;

            tile.texture->bind();

            glBegin(GL_QUADS);
            glTexCoord2f(srcX0, srcY0); glVertex2f(x0, y0);
            glTexCoord2f(srcX1, srcY0); glVertex2f(x1, y0);
            glTexCoord2f(srcX1, srcY1); glVertex2f(x1, y1);
            glTexCoord2f(srcX0, srcY1); glVertex2f(x0, y1);
            glEnd();
        }
    }

    _shader->release();

    // Draw focus border using QPainter overlay
    if (_focusedIndex >= 0 && _focusedIndex < static_cast<int>(_emulators.size()))
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        int col = _focusedIndex % _currentCols;
        int row = _focusedIndex / _currentCols;
        QRect tileRect(col * TILE_WIDTH, row * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT);

        QPen pen(QColor(120, 160, 255), 2);
        painter.setPen(pen);
        painter.drawRect(tileRect.adjusted(1, 1, -2, -2));
    }

    if (_dragHoverIndex >= 0 && _dragHoverIndex < static_cast<int>(_emulators.size()))
    {
        QPainter painter(this);
        int col = _dragHoverIndex % _currentCols;
        int row = _dragHoverIndex / _currentCols;
        QRect tileRect(col * TILE_WIDTH, row * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT);

        QPen pen(QColor(80, 120, 255), 5);
        painter.setPen(pen);
        painter.drawRect(tileRect.adjusted(2, 2, -4, -4));
    }
}

void TileGridGL::keyPressEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat() || _focusedIndex < 0)
        return;

    auto emulator = emulatorAt(_focusedIndex);
    if (!emulator)
        return;

    quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());
    if (zxKey != 0)
    {
        std::string targetId = emulator->GetUUID();
        KeyboardEvent* keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_PRESSED, targetId);
        MessageCenter::DefaultMessageCenter().Post(MC_KEY_PRESSED, keyEvent);
    }
    event->accept();
}

void TileGridGL::keyReleaseEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat() || _focusedIndex < 0)
        return;

    auto emulator = emulatorAt(_focusedIndex);
    if (!emulator)
        return;

    quint8 zxKey = KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());
    if (zxKey != 0)
    {
        std::string targetId = emulator->GetUUID();
        KeyboardEvent* keyEvent = new KeyboardEvent(static_cast<uint8_t>(zxKey), KEY_RELEASED, targetId);
        MessageCenter::DefaultMessageCenter().Post(MC_KEY_RELEASED, keyEvent);
    }
    event->accept();
}

void TileGridGL::mousePressEvent(QMouseEvent* event)
{
    int index = emulatorIndexAt(event->pos());
    if (index >= 0)
    {
        setFocusedIndex(index);
        emit tileClicked(index);
    }
}

bool TileGridGL::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::DragEnter:
        {
            QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls())
            {
                int index = emulatorIndexAt(dragEvent->position().toPoint());
                if (index >= 0)
                {
                    _dragHoverIndex = index;
                    dragEvent->acceptProposedAction();
                    update();
                    return true;
                }
            }
            break;
        }
        case QEvent::DragMove:
        {
            QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
            int index = emulatorIndexAt(dragEvent->position().toPoint());
            if (index != _dragHoverIndex)
            {
                _dragHoverIndex = index;
                update();
            }
            if (index >= 0)
            {
                dragEvent->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::DragLeave:
            _dragHoverIndex = -1;
            update();
            return true;

        case QEvent::Drop:
        {
            QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
            int index = emulatorIndexAt(dropEvent->position().toPoint());
            _dragHoverIndex = -1;

            if (index >= 0 && dropEvent->mimeData()->hasUrls())
            {
                QList<QUrl> urls = dropEvent->mimeData()->urls();
                if (!urls.isEmpty())
                {
                    QString filePath = urls.first().toLocalFile();
                    emit fileDropped(index, filePath);
                }
                update();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return QOpenGLWindow::event(event);
}

void TileGridGL::subscribeToNotifications()
{
    _videoFrameObserverId = MessageCenter::DefaultMessageCenter().AddObserver(NC_VIDEO_FRAME_REFRESH,
        [this](int id, Message* message) {
            if (message && message->obj)
            {
                auto* payload = dynamic_cast<EmulatorFramePayload*>(message->obj);
                if (payload)
                {
                    std::string frameEmulatorId = payload->_emulatorId.toString();

                    if (_singleSyncMode)
                    {
                        if (frameEmulatorId == _syncEmulatorId)
                        {
                            bool expected = false;
                            if (_isRepaintPending.compare_exchange_strong(expected, true))
                            {
                                for (auto& tile : _emulators)
                                    tile.needsUpdate = true;

                                QMetaObject::invokeMethod(this, [this]() {
                                    update();
                                    _isRepaintPending = false;
                                }, Qt::QueuedConnection);
                            }
                        }
                    }
                    else
                    {
                        for (auto& tile : _emulators)
                        {
                            if (tile.emulatorId == frameEmulatorId)
                            {
                                tile.needsUpdate = true;
                                QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
                                break;
                            }
                        }
                    }
                }
            }
        });
}

void TileGridGL::unsubscribeFromNotifications()
{
    if (_videoFrameObserverId != 0)
    {
        MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_VIDEO_FRAME_REFRESH, _videoFrameObserverId);
        _videoFrameObserverId = 0;
    }
}
