#define GL_SILENCE_DEPRECATION
#include "TileGridGL.h"

#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QOpenGLContext>
#include <QOpenGLPixelTransferOptions>
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
    state.emulatorId = emulator->GetId();  // Must match what mainloop.cpp posts
    state.latchedFrame = QImage(FB_WIDTH, FB_HEIGHT, QImage::Format_RGBA8888);
    state.latchedFrame.fill(Qt::black);

    bool wasEmpty = _emulators.empty();
    _emulators.push_back(std::move(state));

    // Subscribe to frame notifications on first emulator
    if (wasEmpty && _videoFrameObserverId == 0)
    {
        subscribeToNotifications();
    }

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

QRect TileGridGL::calculateTileRect(int index) const
{
    if (index < 0 || index >= static_cast<int>(_emulators.size()) || _currentCols <= 0 || _currentRows <= 0)
        return QRect();

    int winW = width();
    int winH = height();
    if (winW <= 0 || winH <= 0)
        return QRect();

    int col = index % _currentCols;
    int row = index / _currentCols;

    float cellW = static_cast<float>(winW) / static_cast<float>(_currentCols);
    float cellH = static_cast<float>(winH) / static_cast<float>(_currentRows);

    float cellLeft = col * cellW;
    float cellTop = row * cellH;

    // ZX Spectrum active resolution 256x192 has 4:3 aspect ratio
    constexpr float targetRatio = 4.0f / 3.0f;
    float cellRatio = cellW / cellH;

    float tileW, tileH;
    if (cellRatio > targetRatio)
    {
        // Cell is wider than 4:3 -> pillarbox
        tileH = cellH;
        tileW = cellH * targetRatio;
    }
    else
    {
        // Cell is taller than 4:3 -> letterbox
        tileW = cellW;
        tileH = cellW / targetRatio;
    }

    float tileLeft = cellLeft + (cellW - tileW) * 0.5f;
    float tileTop = cellTop + (cellH - tileH) * 0.5f;

    return QRect(static_cast<int>(tileLeft), static_cast<int>(tileTop),
                 static_cast<int>(tileW), static_cast<int>(tileH));
}

int TileGridGL::emulatorIndexAt(const QPoint& pos) const
{
    if (_emulators.empty() || _currentCols <= 0 || _currentRows <= 0)
        return -1;

    int winW = width();
    int winH = height();
    if (winW <= 0 || winH <= 0)
        return -1;

    int col = pos.x() * _currentCols / winW;
    int row = pos.y() * _currentRows / winH;

    if (col < 0 || col >= _currentCols || row < 0 || row >= _currentRows)
        return -1;

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
    if (!_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, TILE_VERTEX_SHADER))
    {
        qWarning() << "TileGridGL vertex shader error:" << _shader->log();
    }
    if (!_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, TILE_FRAGMENT_SHADER))
    {
        qWarning() << "TileGridGL fragment shader error:" << _shader->log();
    }
    if (!_shader->link())
    {
        qWarning() << "TileGridGL shader link error:" << _shader->log();
    }
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
            tile.texture->create();
            tile.texture->setSize(static_cast<int>(desc.width), static_cast<int>(desc.height));
            tile.texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            tile.texture->setMipLevels(1);
            tile.texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
            tile.texture->setMinificationFilter(QOpenGLTexture::Nearest);
            tile.texture->setMagnificationFilter(QOpenGLTexture::Nearest);
            tile.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }

        tile.texture->bind();
        QOpenGLPixelTransferOptions transfer;
        transfer.setAlignment(1);
        transfer.setRowLength(static_cast<int>(tile.latchedFrame.bytesPerLine() / 4));
        tile.texture->setData(0, QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, tile.latchedFrame.constBits(), &transfer);

        tile.needsUpdate = false;
    }
}

void TileGridGL::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    if (_emulators.empty() || _currentCols <= 0 || _currentRows <= 0)
        return;

    updateTextures();

    float winW = static_cast<float>(width());
    float winH = static_cast<float>(height());
    if (winW <= 0.0f || winH <= 0.0f)
        return;

    if (_shader && _shader->isLinked())
    {
        _shader->bind();
        _shader->setUniformValue("tileTexture", 0);

        int posAttr = _shader->attributeLocation("position");
        int texAttr = _shader->attributeLocation("texCoord");

        _shader->enableAttributeArray(posAttr);
        _shader->enableAttributeArray(texAttr);

        constexpr float targetRatio = 4.0f / 3.0f;
        float cellNDC_W = 2.0f / static_cast<float>(_currentCols);
        float cellNDC_H = 2.0f / static_cast<float>(_currentRows);

        float cellPixelW = winW / static_cast<float>(_currentCols);
        float cellPixelH = winH / static_cast<float>(_currentRows);
        float cellRatio = cellPixelW / cellPixelH;

        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (cellRatio > targetRatio)
        {
            // Cell is wider than 4:3 -> pillarbox
            scaleX = targetRatio / cellRatio;
        }
        else
        {
            // Cell is taller than 4:3 -> letterbox
            scaleY = cellRatio / targetRatio;
        }

        float quadHalfW = (cellNDC_W * 0.5f) * scaleX;
        float quadHalfH = (cellNDC_H * 0.5f) * scaleY;

        // Extract 256x192 active screen area from 352x288 framebuffer
        float srcX0 = 48.0f / FB_WIDTH;
        float srcY0 = 48.0f / FB_HEIGHT;
        float srcX1 = (48.0f + 256.0f) / FB_WIDTH;
        float srcY1 = (48.0f + 192.0f) / FB_HEIGHT;

        int index = 0;
        for (int row = 0; row < _currentRows && index < static_cast<int>(_emulators.size()); ++row)
        {
            for (int col = 0; col < _currentCols && index < static_cast<int>(_emulators.size()); ++col, ++index)
            {
                auto& tile = _emulators[index];
                if (!tile.texture || !tile.texture->isCreated())
                    continue;

                float cellCenterX = -1.0f + (col + 0.5f) * cellNDC_W;
                float cellCenterY = 1.0f - (row + 0.5f) * cellNDC_H;

                float x0 = cellCenterX - quadHalfW;
                float x1 = cellCenterX + quadHalfW;
                float y0 = cellCenterY + quadHalfH; // Top in NDC
                float y1 = cellCenterY - quadHalfH; // Bottom in NDC

                GLfloat vertices[] = {
                    x0, y0,
                    x1, y0,
                    x1, y1,
                    x0, y1
                };

                GLfloat texCoords[] = {
                    srcX0, srcY0,
                    srcX1, srcY0,
                    srcX1, srcY1,
                    srcX0, srcY1
                };

                glActiveTexture(GL_TEXTURE0);
                tile.texture->bind();

                _shader->setAttributeArray(posAttr, GL_FLOAT, vertices, 2);
                _shader->setAttributeArray(texAttr, GL_FLOAT, texCoords, 2);

                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
        }

        _shader->disableAttributeArray(posAttr);
        _shader->disableAttributeArray(texAttr);
        _shader->release();
    }

    // Draw focus and drag-and-drop hover borders using QPainter overlay
    if (_focusedIndex >= 0 && _focusedIndex < static_cast<int>(_emulators.size()))
    {
        QRect tileRect = calculateTileRect(_focusedIndex);
        if (!tileRect.isEmpty())
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, false);

            QPen pen(QColor(120, 160, 255), 2);
            painter.setPen(pen);
            painter.drawRect(tileRect.adjusted(1, 1, -2, -2));
        }
    }

    if (_dragHoverIndex >= 0 && _dragHoverIndex < static_cast<int>(_emulators.size()))
    {
        QRect tileRect = calculateTileRect(_dragHoverIndex);
        if (!tileRect.isEmpty())
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, false);

            QPen pen(QColor(80, 120, 255), 5);
            painter.setPen(pen);
            painter.drawRect(tileRect.adjusted(2, 2, -4, -4));
        }
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

                    QMetaObject::invokeMethod(this, [this, frameEmulatorId]() {
                        if (_singleSyncMode)
                        {
                            if (frameEmulatorId == _syncEmulatorId)
                            {
                                for (auto& tile : _emulators)
                                    tile.needsUpdate = true;
                                update();
                            }
                        }
                        else
                        {
                            for (auto& tile : _emulators)
                            {
                                if (tile.emulatorId == frameEmulatorId)
                                {
                                    tile.needsUpdate = true;
                                    update();
                                    break;
                                }
                            }
                        }
                    }, Qt::QueuedConnection);
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
