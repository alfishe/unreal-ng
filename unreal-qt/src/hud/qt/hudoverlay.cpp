#include "hudoverlay.h"

#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

#include "hudanimator.h"
#include "hudtiming.h"

namespace {
QFont resolveIndicatorFont(bool monospace, const std::string& defaultFamily, float fontSize, float uiScale)
{
    if (monospace)
    {
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setPointSize(static_cast<int>(fontSize * uiScale));
        f.setBold(true);
        f.setStyleHint(QFont::Monospace);
        return f;
    }
    return QFont(QString::fromStdString(defaultFamily), static_cast<int>(fontSize * uiScale), QFont::Bold);
}
} // namespace

HudOverlay::HudOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);

    if (parent)
    {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }

    _animTimer = new QTimer(this);
    _animTimer->setInterval(16); // ~60 FPS
    connect(_animTimer, &QTimer::timeout, this, &HudOverlay::onAnimationTick);
}

HudOverlay::~HudOverlay()
{
    if (_animTimer)
    {
        _animTimer->stop();
    }
}

void HudOverlay::setModel(std::shared_ptr<HudModel> model)
{
    if (_model == model)
        return;

    _model = model;
    if (_model)
    {
        _model->setChangedCallback([this]() {
            QMetaObject::invokeMethod(this, &HudOverlay::onModelChanged, Qt::QueuedConnection);
        });
    }
    onModelChanged();
}

void HudOverlay::setTheme(HudThemeId themeId)
{
    if (_themeId == themeId)
        return;

    _themeId = themeId;
    if (_model)
    {
        _model->setTheme(themeId);
    }
    update();
}

void HudOverlay::setScaleFactor(float factor)
{
    float clamped = std::clamp(factor, 0.5f, 5.0f);
    if (std::abs(_scaleFactor - clamped) < 1e-3f)
        return;

    _scaleFactor = clamped;
    if (_model)
    {
        _model->setScaleFactor(clamped);
    }
    update();
}

void HudOverlay::setToastPosition(HudTilePosition position)
{
    if (_toastPosition == position)
        return;
    _toastPosition = position;
    if (_model)
    {
        _model->setToastPosition(position);
    }
    update();
}

void HudOverlay::setIndicatorPosition(HudTilePosition position)
{
    if (_indicatorPosition == position)
        return;
    _indicatorPosition = position;
    if (_model)
    {
        _model->setIndicatorPosition(position);
    }
    update();
}

void HudOverlay::syncGeometryWithParent()
{
    if (parentWidget())
    {
        setGeometry(parentWidget()->rect());
        raise();
    }
}

bool HudOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
    {
        syncGeometryWithParent();
    }
    return QWidget::eventFilter(watched, event);
}

void HudOverlay::onModelChanged()
{
    if (!_model || !_model->isEnabled())
    {
        if (_animTimer && _animTimer->isActive())
        {
            _animTimer->stop();
        }
        // Invalidate only the last known element bounds
        if (!_lastIndicatorBounds.isEmpty())
            update(_lastIndicatorBounds);
        if (!_lastToastBounds.isEmpty())
            update(_lastToastBounds);
        if (_lastIndicatorBounds.isEmpty() && _lastToastBounds.isEmpty())
            update();
        return;
    }

    auto snap = _model->snapshot();
    bool hasAnimations = false;
    if (snap)
    {
        for (const auto& el : snap->elements)
        {
            // Only toasts and elements with TTL need animation timer
            if (el.kind == HudKind::Toast || el.ttl.count() > 0)
            {
                hasAnimations = true;
                break;
            }
        }
    }

    _hasActiveAnimations = hasAnimations;
    if (_hasActiveAnimations && _animTimer && !_animTimer->isActive())
    {
        _animTimer->start();
    }
    else if (!_hasActiveAnimations && _animTimer && _animTimer->isActive())
    {
        _animTimer->stop();  // Stop timer when no animations needed
    }

    // Targeted update: only invalidate regions with elements
    if (!_lastIndicatorBounds.isEmpty())
        update(_lastIndicatorBounds);
    if (!_lastToastBounds.isEmpty())
        update(_lastToastBounds);
    if (_lastIndicatorBounds.isEmpty() && _lastToastBounds.isEmpty())
        update();  // First paint - need full update
}

void HudOverlay::onAnimationTick()
{
    if (!_model)
        return;

    size_t expiredCount = _model->expire(HudClock::now());
    auto snap = _model->snapshot();

    if (!snap || snap->elements.empty())
    {
        _hasActiveAnimations = false;
        _animTimer->stop();
        if (expiredCount > 0)
        {
            // Targeted update: only invalidate regions with elements
            if (!_lastIndicatorBounds.isEmpty())
                update(_lastIndicatorBounds);
            if (!_lastToastBounds.isEmpty())
                update(_lastToastBounds);
        }
        return;
    }

    // Repaint when elements expired, or while a toast is in its enter/exit animation
    // (opacity / slide change every tick, and the slide moves the tile - and its shadow -
    // outside the bounds recorded at the previous paint)
    bool toastAnimating = false;
    auto now = HudClock::now();
    for (const auto& el : snap->elements)
    {
        if (el.kind != HudKind::Toast)
            continue;
        auto elapsed = now - el.created;
        if (elapsed < HudTiming::AnimEnter || (el.ttl.count() > 0 && elapsed > (el.ttl - HudTiming::AnimExit)))
        {
            toastAnimating = true;
            break;
        }
    }

    if (expiredCount > 0 || toastAnimating)
    {
        if (!_lastIndicatorBounds.isEmpty())
            update(_lastIndicatorBounds);
        if (!_lastToastBounds.isEmpty())
        {
            // Slide animations move toasts by up to 0.75em vertically between paints
            float uiScale = snap->scaleFactor > 0.0f ? snap->scaleFactor : _scaleFactor;
            int slide = static_cast<int>(16.0f * uiScale) + 1;
            update(_lastToastBounds.adjusted(0, -slide, 0, slide));
        }
    }
}

QImage HudOverlay::getImageFromBuffer(const std::shared_ptr<const HudImageBuffer>& buf)
{
    if (!buf)
        return QImage();

    auto it = _imageCache.find(buf.get());
    if (it != _imageCache.end())
    {
        return it->second;
    }

    QImage::Format qfmt = QImage::Format_RGBA8888;
    if (buf->format() == HudPixelFormat::BGRA8888)
        qfmt = QImage::Format_ARGB32;
    else if (buf->format() == HudPixelFormat::ARGB8888_Premul)
        qfmt = QImage::Format_ARGB32_Premultiplied;

    QImage img(buf->data(), buf->width(), buf->height(), buf->stride(), qfmt);
    QImage copy = img.copy(); // Own memory
    _imageCache[buf.get()] = copy;
    return copy;
}

QPixmap HudOverlay::getCachedTileFrame(const QSize& size, const HudTileFrameStyle& frame, float uiScale)
{
    TileFrameCacheKey key{
        size.width(), size.height(),
        frame.backgroundColor, frame.backgroundGradientEnd,
        frame.borderColor, frame.shadowColor,
        frame.borderRadius * uiScale, frame.borderWidth * uiScale,
        frame.shadowBlur * uiScale, frame.glassEffect
    };

    auto it = _tileFrameCache.find(key);
    if (it != _tileFrameCache.end())
    {
        // Move to front of LRU list
        _tileFrameLruOrder.erase(it->second.second);
        _tileFrameLruOrder.push_front(key);
        it->second.second = _tileFrameLruOrder.begin();
        return it->second.first;
    }

    // Render new pixmap
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    renderTileFrameToPixmap(pixmap, frame, uiScale);

    // Evict oldest if at capacity
    if (_tileFrameCache.size() >= kTileFrameCacheMaxSize)
    {
        auto& oldest = _tileFrameLruOrder.back();
        _tileFrameCache.erase(oldest);
        _tileFrameLruOrder.pop_back();
    }

    // Insert into cache
    _tileFrameLruOrder.push_front(key);
    _tileFrameCache[key] = {pixmap, _tileFrameLruOrder.begin()};
    return pixmap;
}

void HudOverlay::renderTileFrameToPixmap(QPixmap& pixmap, const HudTileFrameStyle& frame, float uiScale)
{
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRect rect = pixmap.rect();
    float radius = frame.borderRadius * uiScale;
    float bWidth = std::max(1.0f, frame.borderWidth * uiScale);

    // Drop shadow / outer neon glow
    if (frame.shadowColor != 0 && frame.shadowBlur > 0.0f)
    {
        int spread = static_cast<int>(std::max(1.0f, 2.0f * uiScale));
        // The tile body ends 2 * spread above the pixmap bottom; stop the shadow one
        // spread short so it extends only `spread` below the tile
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(rect.adjusted(spread, spread, -spread, -spread), radius, radius);
        p.fillPath(shadowPath, QColor::fromRgba(frame.shadowColor));
    }

    // Inset rect to leave room for shadow
    int inset = static_cast<int>(std::max(1.0f, 2.0f * uiScale));
    QRect innerRect = rect.adjusted(inset, 0, -inset, -inset * 2);

    QPainterPath path;
    path.addRoundedRect(innerRect, radius, radius);

    // Background fill (solid or linear gradient)
    if (frame.backgroundGradientEnd != 0 && frame.backgroundGradientEnd != frame.backgroundColor)
    {
        QLinearGradient grad(innerRect.topLeft(), innerRect.bottomLeft());
        grad.setColorAt(0.0, QColor::fromRgba(frame.backgroundColor));
        grad.setColorAt(1.0, QColor::fromRgba(frame.backgroundGradientEnd));
        p.fillPath(path, grad);
    }
    else
    {
        p.fillPath(path, QColor::fromRgba(frame.backgroundColor));
    }

    // Border stroke
    if (frame.borderColor != 0 && bWidth > 0.0f)
    {
        p.strokePath(path, QPen(QColor::fromRgba(frame.borderColor), bWidth));
    }

    // Specular glass highlight at top
    if (frame.glassEffect && radius > 0.0f)
    {
        p.save();
        p.setClipPath(path);
        int hlHeight = std::max(2, static_cast<int>(2.5f * uiScale));
        QRect hlRect(innerRect.left(), innerRect.top(), innerRect.width(), hlHeight);
        QLinearGradient hlGrad(hlRect.topLeft(), hlRect.topRight());
        hlGrad.setColorAt(0.0, QColor(255, 255, 255, 0));
        hlGrad.setColorAt(0.5, QColor(255, 255, 255, 55));
        hlGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.fillRect(hlRect, hlGrad);
        p.restore();
    }
}

QRect HudOverlay::tileDirtyBounds(const QRect& rect, float uiScale)
{
    // Must match drawWholeTileFrame: the cached frame pixmap overhangs the tile rect by
    // `spread` on the left/right and 3 * `spread` below (drop shadow). One extra pixel on
    // every side covers antialiased edges.
    int spread = static_cast<int>(std::max(1.0f, 2.0f * uiScale));
    return rect.adjusted(-spread - 1, -1, spread + 1, spread * 3 + 1);
}

void HudOverlay::drawWholeTileFrame(QPainter& painter, const QRect& rect, const HudTileFrameStyle& frame, float uiScale)
{
    // Use cached pre-rendered pixmap
    int spread = static_cast<int>(std::max(1.0f, 2.0f * uiScale));
    QSize cacheSize(rect.width() + spread * 2, rect.height() + spread * 3);
    QPixmap cached = getCachedTileFrame(cacheSize, frame, uiScale);
    painter.drawPixmap(rect.left() - spread, rect.top(), cached);
}

void HudOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Clear to transparent before drawing HUD elements
    QPainter painter(this);
    painter.fillRect(rect(), Qt::transparent);

    if (!_model || !_model->isEnabled())
        return;

    auto snap = _model->snapshot();
    if (!snap || snap->elements.empty())
        return;

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    HudSurface surface;
    surface.outputRect = HudRect{0, 0, width(), height()};
    surface.imageRect = HudRect{0, 0, width(), height()};
    surface.dpr = devicePixelRatio();

    // Determine scale factor: at least twice bigger (default 2.0x)
    float uiScale = _scaleFactor;
    if (snap->scaleFactor > 0.0f)
    {
        uiScale = snap->scaleFactor;
    }

    // Resolve active theme
    HudThemeId activeThemeId = (_themeId != HudThemeId::DarkGlass) ? _themeId : snap->themeId;
    const HudTheme& theme = HudTheme::FromId(activeThemeId);

    auto now = HudClock::now();

    // Collect elements by category
    std::vector<const HudElement*> augmentations;
    std::vector<const HudElement*> indicators;
    std::vector<const HudElement*> toasts;

    for (const auto& el : snap->elements)
    {
        if (el.kind == HudKind::Indicator)
        {
            indicators.push_back(&el);
        }
        else if (el.kind == HudKind::Toast)
        {
            toasts.push_back(&el);
        }
        else
        {
            augmentations.push_back(&el);
        }
    }

    // 1. Draw picture augmentations (tiles, tilemaps, images, text)
    for (const auto* el : augmentations)
    {
        drawAugmentation(painter, *el, surface, theme, uiScale);
    }

    // 2. Draw persistent indicators with predefined display positions
    if (!indicators.empty())
    {
        HudTilePosition indPos = (_indicatorPosition != HudTilePosition::TopRight) ? _indicatorPosition : snap->indicatorPosition;
        int rightMargin = static_cast<int>(16 * uiScale);
        int leftMargin = static_cast<int>(16 * uiScale);
        int topMargin = static_cast<int>(14 * uiScale);
        int bottomMargin = static_cast<int>(14 * uiScale);
        int boxHeight = static_cast<int>(26 * uiScale);
        int gap = static_cast<int>(8 * uiScale);

        float pulseAlpha = HudAnimator::EvaluatePulse(now.time_since_epoch(), HudTiming::AnimPulse);

        struct PreparedIndicator {
            const HudElement* element;
            int width;
        };
        std::vector<PreparedIndicator> prepared;
        int totalWidth = 0;
        for (const auto* ind : indicators)
        {
            QFont indFont = resolveIndicatorFont(ind->monospace, theme.indicator.content.fontFamily,
                                                 theme.indicator.content.titleFontSize, uiScale);
            QFontMetrics fm(indFont);

            QString displayText;
            if (!ind->title.empty() && !ind->value.empty())
            {
                displayText = QString("%1: %2").arg(QString::fromStdString(ind->title), QString::fromStdString(ind->value));
            }
            else if (!ind->value.empty())
            {
                displayText = QString::fromStdString(ind->value);
            }
            else
            {
                displayText = QString::fromStdString(ind->title);
            }
            int textAdvance = fm.horizontalAdvance(displayText);
            int iconOrDotWidth = !ind->icon.empty() ? static_cast<int>(14 * uiScale) : static_cast<int>(8 * uiScale);
            int boxWidth = textAdvance + iconOrDotWidth + static_cast<int>(28 * uiScale);
            prepared.push_back({ind, boxWidth});
            totalWidth += boxWidth + gap;
        }
        if (!prepared.empty()) totalWidth -= gap;

        int currentX = width() - rightMargin;
        int currentY = topMargin;
        bool stackRightToLeft = true;

        switch (indPos)
        {
            case HudTilePosition::TopLeft:
                currentX = leftMargin;
                currentY = topMargin;
                stackRightToLeft = false;
                break;
            case HudTilePosition::BottomLeft:
                currentX = leftMargin;
                currentY = height() - bottomMargin - boxHeight;
                stackRightToLeft = false;
                break;
            case HudTilePosition::BottomRight:
                currentX = width() - rightMargin;
                currentY = height() - bottomMargin - boxHeight;
                stackRightToLeft = true;
                break;
            case HudTilePosition::MidTop:
                currentX = (width() - totalWidth) / 2;
                currentY = topMargin;
                stackRightToLeft = false;
                break;
            case HudTilePosition::MidBottom:
                currentX = (width() - totalWidth) / 2;
                currentY = height() - bottomMargin - boxHeight;
                stackRightToLeft = false;
                break;
            case HudTilePosition::Center:
                currentX = (width() - totalWidth) / 2;
                currentY = (height() - boxHeight) / 2;
                stackRightToLeft = false;
                break;
            case HudTilePosition::TopRight:
            default:
                currentX = width() - rightMargin;
                currentY = topMargin;
                stackRightToLeft = true;
                break;
        }

        QRect indicatorBounds;
        for (const auto& prep : prepared)
        {
            QRect indRect;
            if (stackRightToLeft)
            {
                currentX -= prep.width;
                indRect = QRect(currentX, currentY, prep.width, boxHeight);
                currentX -= gap;
            }
            else
            {
                indRect = QRect(currentX, currentY, prep.width, boxHeight);
                currentX += prep.width + gap;
            }

            drawIndicator(painter, *prep.element, indRect, pulseAlpha, theme, uiScale);

            // Track bounds (including frame shadow) for targeted updates
            indicatorBounds = indicatorBounds.united(tileDirtyBounds(indRect, uiScale));
        }
        _lastIndicatorBounds = indicatorBounds;
    }
    else
    {
        _lastIndicatorBounds = QRect();
    }

    // 3. Draw transient toasts with predefined display positions
    if (!toasts.empty())
    {
        HudTilePosition toastPos = (_toastPosition != HudTilePosition::MidBottom) ? _toastPosition : snap->toastPosition;
        int maxAvailableWidth = width() - static_cast<int>(40 * uiScale);
        int toastWidth = std::min(maxAvailableWidth, static_cast<int>(360 * uiScale));
        int toastHeight = static_cast<int>(56 * uiScale);
        int marginX = static_cast<int>(20 * uiScale);
        int marginY = static_cast<int>(24 * uiScale);
        int gap = static_cast<int>(10 * uiScale);

        int startX = (width() - toastWidth) / 2;
        int startY = height() - marginY - toastHeight;
        int stepY = -(toastHeight + gap);

        switch (toastPos)
        {
            case HudTilePosition::TopLeft:
                startX = marginX;
                startY = marginY;
                stepY = (toastHeight + gap);
                break;
            case HudTilePosition::TopRight:
                startX = width() - toastWidth - marginX;
                startY = marginY;
                stepY = (toastHeight + gap);
                break;
            case HudTilePosition::BottomLeft:
                startX = marginX;
                startY = height() - marginY - toastHeight;
                stepY = -(toastHeight + gap);
                break;
            case HudTilePosition::BottomRight:
                startX = width() - toastWidth - marginX;
                startY = height() - marginY - toastHeight;
                stepY = -(toastHeight + gap);
                break;
            case HudTilePosition::MidTop:
                startX = (width() - toastWidth) / 2;
                startY = marginY;
                stepY = (toastHeight + gap);
                break;
            case HudTilePosition::Center:
                startX = (width() - toastWidth) / 2;
                startY = (height() - toastHeight) / 2;
                stepY = (toastHeight + gap);
                break;
            case HudTilePosition::MidBottom:
            default:
                startX = (width() - toastWidth) / 2;
                startY = height() - marginY - toastHeight;
                stepY = -(toastHeight + gap);
                break;
        }

        int currentY = startY;
        int currentX = startX;

        QRect toastBounds;
        for (const auto* toast : toasts)
        {
            auto elapsed = now - toast->created;
            float opacity = 1.0f;
            float scale = 1.0f;
            float slideY = 0.0f;

            if (toast->ttl.count() > 0 && elapsed > (toast->ttl - HudTiming::AnimExit))
            {
                auto exitElapsed = elapsed - (toast->ttl - HudTiming::AnimExit);
                auto exitState = HudAnimator::EvaluateExit(exitElapsed, toast->exit);
                opacity = exitState.opacity;
                slideY = exitState.slideOffsetEm * 16.0f * uiScale;
            }
            else
            {
                auto enterState = HudAnimator::EvaluateEnter(elapsed, toast->enter);
                opacity = enterState.opacity;
                scale = enterState.scale;
                slideY = enterState.slideOffsetEm * 16.0f * uiScale;
            }

            QRect toastRect(currentX, currentY + static_cast<int>(slideY), toastWidth, toastHeight);

            drawToast(painter, *toast, toastRect, opacity, scale, theme, uiScale);

            // Track bounds (including frame shadow) for targeted updates
            toastBounds = toastBounds.united(tileDirtyBounds(toastRect, uiScale));

            currentY += stepY;
        }
        _lastToastBounds = toastBounds;
    }
    else
    {
        _lastToastBounds = QRect();
    }
}

void HudOverlay::drawAugmentation(QPainter& painter, const HudElement& el, const HudSurface& surface, const HudTheme& theme, float uiScale)
{
    HudRect mapped = MapToDeviceRect(el.rect, el.coordSpace, el.referenceSize, surface, el.position, el.margins, uiScale);
    QRect targetRect(mapped.x, mapped.y, mapped.w, mapped.h);

    painter.save();
    painter.setOpacity(el.opacity);

    const HudTileStyle& tileStyle = theme.resolveStyle(el.styleId);

    if (el.kind == HudKind::Tile)
    {
        // Whole tile framing
        drawWholeTileFrame(painter, targetRect, tileStyle.frame, uiScale);

        // Content area inset by tile padding
        int padL = static_cast<int>(tileStyle.frame.padding.left * uiScale);
        int padT = static_cast<int>(tileStyle.frame.padding.top * uiScale);
        int padR = static_cast<int>(tileStyle.frame.padding.right * uiScale);
        int padB = static_cast<int>(tileStyle.frame.padding.bottom * uiScale);
        QRect contentRect = targetRect.adjusted(padL, padT, -padR, -padB);

        if (el.image)
        {
            QImage qimg = getImageFromBuffer(el.image);
            if (!qimg.isNull())
            {
                if (!el.srcRect.empty())
                {
                    QRect src(el.srcRect.x, el.srcRect.y, el.srcRect.w, el.srcRect.h);
                    painter.drawImage(contentRect, qimg, src);
                }
                else
                {
                    painter.drawImage(contentRect, qimg);
                }
            }
        }
    }
    else if (el.kind == HudKind::Image && el.image)
    {
        if (!el.styleId.empty())
        {
            drawWholeTileFrame(painter, targetRect, tileStyle.frame, uiScale);
        }

        QImage qimg = getImageFromBuffer(el.image);
        if (!qimg.isNull())
        {
            if (!el.srcRect.empty())
            {
                QRect src(el.srcRect.x, el.srcRect.y, el.srcRect.w, el.srcRect.h);
                painter.drawImage(targetRect, qimg, src);
            }
            else
            {
                painter.drawImage(targetRect, qimg);
            }
        }
    }
    else if (el.kind == HudKind::Tilemap && el.tilemap && el.tilemap->tileset)
    {
        QImage tilesetImg = getImageFromBuffer(el.tilemap->tileset);
        if (!tilesetImg.isNull() && el.tilemap->cols > 0 && el.tilemap->rows > 0)
        {
            int tw = el.tilemap->tileWidth;
            int th = el.tilemap->tileHeight;
            int cellW = mapped.w / el.tilemap->cols;
            int cellH = mapped.h / el.tilemap->rows;

            int tilesPerRow = tilesetImg.width() / tw;

            for (int r = 0; r < el.tilemap->rows; ++r)
            {
                for (int c = 0; c < el.tilemap->cols; ++c)
                {
                    size_t idx = r * el.tilemap->cols + c;
                    if (idx < el.tilemap->tileIndices.size())
                    {
                        uint16_t tileId = el.tilemap->tileIndices[idx];
                        if (tileId != 0xFFFF)
                        {
                            int sx = (tileId % tilesPerRow) * tw;
                            int sy = (tileId / tilesPerRow) * th;
                            QRect src(sx, sy, tw, th);
                            QRect dst(mapped.x + c * cellW, mapped.y + r * cellH, cellW, cellH);
                            painter.drawImage(dst, tilesetImg, src);
                        }
                    }
                }
            }
        }
    }
    else if (el.kind == HudKind::Text && !el.title.empty())
    {
        // Render whole tile frame if style or background requested
        if (!el.styleId.empty() || el.bgColor != 0)
        {
            HudTileFrameStyle customFrame = tileStyle.frame;
            if (el.bgColor != 0)
            {
                customFrame.backgroundColor = el.bgColor;
                customFrame.backgroundGradientEnd = 0;
            }
            drawWholeTileFrame(painter, targetRect, customFrame, uiScale);
        }

        int padL = static_cast<int>(tileStyle.frame.padding.left * uiScale);
        int padR = static_cast<int>(tileStyle.frame.padding.right * uiScale);
        QRect textRect = targetRect.adjusted(padL, 0, -padR, 0);

        QFont font(QString::fromStdString(tileStyle.content.fontFamily));
        float baseSize = (el.fontSize > 0.0f) ? el.fontSize : tileStyle.content.titleFontSize;
        font.setPointSizeF(baseSize * uiScale);
        painter.setFont(font);

        uint32_t textColor = (el.color != 0xFFFFFFFF) ? el.color : tileStyle.content.titleColor;
        painter.setPen(QColor::fromRgba(textColor));

        int flags = Qt::AlignVCenter;
        if (el.textAlign == HudTextAlign::Center) flags |= Qt::AlignHCenter;
        else if (el.textAlign == HudTextAlign::Right) flags |= Qt::AlignRight;
        else flags |= Qt::AlignLeft;

        painter.drawText(textRect, flags, QString::fromStdString(el.title));
    }

    painter.restore();
}

void HudOverlay::drawIndicator(QPainter& painter, const HudElement& el, const QRect& rect, float pulseAlpha, const HudTheme& theme, float uiScale)
{
    painter.save();

    const HudTileStyle& style = (el.state == HudState::Alert) ? theme.alert : theme.indicator;
    drawWholeTileFrame(painter, rect, style.frame, uiScale);

    QColor itemColor(100, 110, 125);
    if (el.state == HudState::Active)
    {
        if (el.id == "ind/rec")
        {
            int alpha = static_cast<int>(120 + 135 * pulseAlpha);
            itemColor = QColor(255, 45, 55, alpha);
        }
        else if (el.id == "ind/pause")
        {
            if (el.value == "EXECUTE")
            {
                itemColor = QColor(50, 220, 110);
            }
            else if (el.value == "BREAKPOINT")
            {
                itemColor = QColor(255, 50, 60);
            }
            else
            {
                itemColor = QColor(255, 190, 45);
            }
        }
        else if (el.id.find("fdd") != std::string::npos)
        {
            itemColor = QColor(50, 220, 110);
        }
        else
        {
            itemColor = QColor::fromRgba(style.content.accentColor);
        }
    }
    else if (el.state == HudState::Alert)
    {
        itemColor = QColor(255, 50, 60);
    }

    int leftOffset = rect.left() + static_cast<int>(10 * uiScale);

    if (!el.icon.empty())
    {
        int iconSize = static_cast<int>(14 * uiScale);
        QRect iconRect(leftOffset, rect.center().y() - iconSize / 2, iconSize, iconSize);
        drawIcon(painter, QString::fromStdString(el.icon), iconRect, itemColor, 1.5f * uiScale);
        leftOffset += iconSize + static_cast<int>(8 * uiScale);
    }
    else
    {
        int dotDiameter = static_cast<int>(8 * uiScale);
        int dotY = rect.center().y() - dotDiameter / 2;
        painter.setBrush(itemColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(leftOffset, dotY, dotDiameter, dotDiameter);
        leftOffset += dotDiameter + static_cast<int>(8 * uiScale);
    }

    // Text label
    QFont font = resolveIndicatorFont(el.monospace, style.content.fontFamily,
                                      style.content.titleFontSize, uiScale);
    painter.setFont(font);
    painter.setPen(QColor::fromRgba(style.content.titleColor));

    QString displayText;
    if (!el.title.empty() && !el.value.empty())
    {
        displayText = QString("%1: %2").arg(QString::fromStdString(el.title), QString::fromStdString(el.value));
    }
    else if (!el.value.empty())
    {
        displayText = QString::fromStdString(el.value);
    }
    else
    {
        displayText = QString::fromStdString(el.title);
    }

    int textW = rect.width() - (leftOffset - rect.left()) - static_cast<int>(8 * uiScale);
    QRect textRect(leftOffset, rect.top(), textW, rect.height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayText);

    painter.restore();
}

void HudOverlay::drawToast(QPainter& painter, const HudElement& el, const QRect& rect, float opacity, float scale, const HudTheme& theme, float uiScale)
{
    painter.save();
    painter.setOpacity(std::clamp(opacity, 0.0f, 1.0f));

    if (scale != 1.0f)
    {
        painter.translate(rect.center());
        painter.scale(scale, scale);
        painter.translate(-rect.center());
    }

    // Whole tile framing using theme
    const HudTileStyle& style = (el.priority == HudPriority::High || el.priority == HudPriority::Critical) ? theme.alert : theme.toast;
    drawWholeTileFrame(painter, rect, style.frame, uiScale);

    // Icon area
    int iconSize = static_cast<int>(28 * uiScale);
    int padX = static_cast<int>(14 * uiScale);
    QRect iconRect(rect.left() + padX, rect.center().y() - iconSize / 2, iconSize, iconSize);

    QColor iconColor = QColor::fromRgba(style.content.iconColor);
    if (el.priority == HudPriority::High || el.priority == HudPriority::Critical)
    {
        iconColor = QColor::fromRgba(theme.alert.content.iconColor);
    }
    drawIcon(painter, QString::fromStdString(el.icon), iconRect, iconColor, std::max(1.8f, 1.8f * uiScale));

    // Text content
    int textLeft = iconRect.right() + static_cast<int>(14 * uiScale);
    int textWidth = rect.width() - (textLeft - rect.left()) - padX;

    QFont titleFont(QString::fromStdString(style.content.fontFamily),
                    static_cast<int>(style.content.titleFontSize * uiScale),
                    QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor::fromRgba(style.content.titleColor));

    if (el.body.empty())
    {
        QRect singleLineRect(textLeft, rect.top(), textWidth, rect.height());
        painter.drawText(singleLineRect, Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(el.title));
    }
    else
    {
        int titleHeight = static_cast<int>(22 * uiScale);
        QRect titleRect(textLeft, rect.top() + static_cast<int>(7 * uiScale), textWidth, titleHeight);
        painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(el.title));

        QFont bodyFont(QString::fromStdString(style.content.fontFamily),
                       static_cast<int>(style.content.bodyFontSize * uiScale),
                       QFont::Normal);
        painter.setFont(bodyFont);
        painter.setPen(QColor::fromRgba(style.content.bodyColor));

        QRect bodyRect(textLeft, rect.top() + static_cast<int>(27 * uiScale), textWidth, titleHeight);
        painter.drawText(bodyRect, Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(el.body));
    }

    // Coalesced counter badge e.g. "x3"
    if (el.coalesced > 1)
    {
        QString badge = QString("x%1").arg(el.coalesced);
        QFont badgeFont(QString::fromStdString(style.content.fontFamily),
                        static_cast<int>(8.5f * uiScale),
                        QFont::Bold);
        painter.setFont(badgeFont);

        QFontMetrics fm(badgeFont);
        int bw = fm.horizontalAdvance(badge) + static_cast<int>(12 * uiScale);
        int bh = static_cast<int>(18 * uiScale);
        QRect badgeRect(rect.right() - bw - static_cast<int>(10 * uiScale), rect.top() + static_cast<int>(10 * uiScale), bw, bh);

        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, bh / 2.0, bh / 2.0);
        painter.fillPath(badgePath, QColor::fromRgba(style.content.badgeBgColor));
        painter.setPen(QColor::fromRgba(style.content.badgeTextColor));
        painter.drawText(badgeRect, Qt::AlignCenter, badge);
    }

    painter.restore();
}

void HudOverlay::drawIcon(QPainter& painter, const QString& iconName, const QRect& r, const QColor& color, float strokeWidth)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (iconName == "floppy")
    {
        QRect disk = r.adjusted(2, 2, -2, -2);
        painter.drawRoundedRect(disk, 3, 3);
        int shutterH = std::max(4, disk.height() / 4);
        painter.fillRect(QRect(disk.left() + 4, disk.top() + 2, disk.width() - 8, shutterH), color);
        painter.drawRect(QRect(disk.left() + 5, disk.bottom() - shutterH, disk.width() - 10, shutterH - 1));
    }
    else if (iconName == "tape")
    {
        QRect tape = r.adjusted(1, 3, -1, -3);
        painter.drawRoundedRect(tape, 3, 3);
        int spoolRadius = std::max(2, tape.height() / 6);
        int spoolY = tape.center().y();
        painter.drawEllipse(QPoint(tape.left() + 7, spoolY), spoolRadius, spoolRadius);
        painter.drawEllipse(QPoint(tape.right() - 7, spoolY), spoolRadius, spoolRadius);
        int winW = tape.width() / 2;
        painter.drawRect(QRect(tape.center().x() - winW / 2, spoolY - 3, winW, 6));
    }
    else if (iconName == "breakpoint")
    {
        // Octagonal stop-sign badge with pause symbol
        QRect oct = r.adjusted(2, 2, -2, -2);
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 50));
        painter.drawEllipse(oct);

        int barW = std::max(2, oct.width() / 8);
        int barH = oct.height() / 2;
        int barY = oct.center().y() - barH / 2;
        int gap = std::max(2, barW);

        painter.fillRect(QRect(oct.center().x() - barW - gap / 2, barY, barW, barH), color);
        painter.fillRect(QRect(oct.center().x() + gap / 2, barY, barW, barH), color);
    }
    else if (iconName == "pause")
    {
        int barW = std::max(3, r.width() / 7);
        int barH = r.height() * 3 / 5;
        int barY = r.center().y() - barH / 2;
        int gap = std::max(3, barW);

        painter.fillRect(QRect(r.center().x() - barW - gap / 2, barY, barW, barH), color);
        painter.fillRect(QRect(r.center().x() + gap / 2, barY, barW, barH), color);
    }
    else if (iconName == "turbo")
    {
        // Lightning bolt icon
        QPainterPath bolt;
        int cx = r.center().x();
        int top = r.top() + 2;
        int bot = r.bottom() - 2;
        int midY = r.center().y();
        int w = r.width() / 3;

        bolt.moveTo(cx + 2, top);
        bolt.lineTo(cx - w, midY + 1);
        bolt.lineTo(cx, midY + 1);
        bolt.lineTo(cx - 2, bot);
        bolt.lineTo(cx + w, midY - 1);
        bolt.lineTo(cx, midY - 1);
        bolt.closeSubpath();

        painter.setBrush(color);
        painter.drawPath(bolt);
    }
    else if (iconName == "rec")
    {
        QRect circle = r.adjusted(3, 3, -3, -3);
        painter.setBrush(color);
        painter.drawEllipse(circle);
    }
    else if (iconName == "ram")
    {
        // Memory chip icon: rectangle with pins and memory grid, vertically centered
        int chipH = r.height() - 4;
        int chipW = r.width() - 8;  // Leave room for pins
        QRect chip(r.left() + 4, r.center().y() - chipH / 2, chipW, chipH);
        painter.drawRoundedRect(chip, 2, 2);
        // Draw pins on sides
        int pinLen = 3;
        int pinCount = 3;
        int pinStep = chip.height() / (pinCount + 1);
        for (int i = 1; i <= pinCount; ++i)
        {
            int y = chip.top() + i * pinStep;
            painter.drawLine(chip.left() - pinLen, y, chip.left(), y);
            painter.drawLine(chip.right(), y, chip.right() + pinLen, y);
        }
        // Draw memory grid inside (2x2)
        int gridSize = std::min(6, chip.width() / 2);
        int gridX = chip.center().x() - gridSize / 2;
        int gridY = chip.center().y() - gridSize / 2;
        painter.drawRect(QRect(gridX, gridY, gridSize, gridSize));
        painter.drawLine(gridX + gridSize / 2, gridY, gridX + gridSize / 2, gridY + gridSize);
        painter.drawLine(gridX, gridY + gridSize / 2, gridX + gridSize, gridY + gridSize / 2);
    }
    else if (iconName == "rom")
    {
        // ROM chip icon: rectangle with notch and pins, vertically centered
        int chipH = r.height() - 4;
        int chipW = r.width() - 8;
        QRect chip(r.left() + 4, r.center().y() - chipH / 2, chipW, chipH);
        // Draw chip body with notch at top
        QPainterPath chipPath;
        int notchR = std::min(3, chip.width() / 4);
        chipPath.moveTo(chip.left() + 2, chip.top());
        chipPath.lineTo(chip.center().x() - notchR, chip.top());
        chipPath.arcTo(chip.center().x() - notchR, chip.top() - notchR / 2, notchR * 2, notchR, 180, -180);
        chipPath.lineTo(chip.right() - 2, chip.top());
        chipPath.lineTo(chip.right(), chip.top() + 2);
        chipPath.lineTo(chip.right(), chip.bottom() - 2);
        chipPath.lineTo(chip.right() - 2, chip.bottom());
        chipPath.lineTo(chip.left() + 2, chip.bottom());
        chipPath.lineTo(chip.left(), chip.bottom() - 2);
        chipPath.lineTo(chip.left(), chip.top() + 2);
        chipPath.closeSubpath();
        painter.drawPath(chipPath);
        // Draw pins on sides
        int pinLen = 3;
        int pinCount = 3;
        int pinStep = chip.height() / (pinCount + 1);
        for (int i = 1; i <= pinCount; ++i)
        {
            int y = chip.top() + i * pinStep;
            painter.drawLine(chip.left() - pinLen, y, chip.left(), y);
            painter.drawLine(chip.right(), y, chip.right() + pinLen, y);
        }
    }
    else if (iconName == "screen")
    {
        // Monitor/screen icon
        QRect monitor = r.adjusted(2, 3, -2, -5);
        painter.drawRoundedRect(monitor, 2, 2);
        // Stand
        int standW = monitor.width() / 3;
        int standH = 3;
        painter.drawLine(monitor.center().x(), monitor.bottom(), monitor.center().x(), monitor.bottom() + standH);
        painter.drawLine(monitor.center().x() - standW / 2, r.bottom() - 1,
                         monitor.center().x() + standW / 2, r.bottom() - 1);
        // Screen content lines
        int lineY = monitor.top() + monitor.height() / 3;
        painter.drawLine(monitor.left() + 3, lineY, monitor.right() - 3, lineY);
        lineY += monitor.height() / 4;
        painter.drawLine(monitor.left() + 3, lineY, monitor.center().x(), lineY);
    }
    else if (iconName == "speaker")
    {
        // Speaker/beeper icon: speaker cone shape
        int cx = r.center().x();
        int cy = r.center().y();
        int h = r.height() - 4;
        int w = r.width() - 4;
        // Speaker body (trapezoid)
        QPainterPath speaker;
        speaker.moveTo(cx - w / 3, cy - h / 4);
        speaker.lineTo(cx - w / 3, cy + h / 4);
        speaker.lineTo(cx + w / 3, cy + h / 2);
        speaker.lineTo(cx + w / 3, cy - h / 2);
        speaker.closeSubpath();
        painter.drawPath(speaker);
        // Sound waves
        int waveX = cx + w / 3 + 3;
        painter.drawArc(QRect(waveX, cy - h / 4, 4, h / 2), -60 * 16, 120 * 16);
    }
    else if (iconName == "covox")
    {
        // DAC/Covox icon: audio waveform shape
        int cx = r.center().x();
        int cy = r.center().y();
        int w = r.width() - 6;
        int h = r.height() - 6;
        // Draw stepped waveform
        QPainterPath wave;
        wave.moveTo(cx - w / 2, cy);
        wave.lineTo(cx - w / 3, cy);
        wave.lineTo(cx - w / 3, cy - h / 3);
        wave.lineTo(cx - w / 6, cy - h / 3);
        wave.lineTo(cx - w / 6, cy + h / 3);
        wave.lineTo(cx + w / 6, cy + h / 3);
        wave.lineTo(cx + w / 6, cy - h / 4);
        wave.lineTo(cx + w / 3, cy - h / 4);
        wave.lineTo(cx + w / 3, cy);
        wave.lineTo(cx + w / 2, cy);
        painter.drawPath(wave);
    }
    else
    {
        // Generic document file shape
        QRect doc = r.adjusted(4, 2, -4, -2);
        painter.drawRoundedRect(doc, 2, 2);
        painter.drawLine(doc.left() + 4, doc.top() + 6, doc.right() - 4, doc.top() + 6);
        painter.drawLine(doc.left() + 4, doc.top() + 11, doc.right() - 4, doc.top() + 11);
        painter.drawLine(doc.left() + 4, doc.top() + 16, doc.center().x(), doc.top() + 16);
    }

    painter.restore();
}
