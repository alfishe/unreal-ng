#include "hudoverlay_gl.h"

#include <QDebug>
#include <QPainter>
#include <QResizeEvent>
#include <QSurfaceFormat>
#include <cmath>

#include "hudsnapshot.h"

HudOverlayGL::HudOverlayGL(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // Set up format with alpha channel for proper transparency
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(8);
    fmt.setSamples(0);  // No MSAA needed for HUD
    setFormat(fmt);

    // Enable true compositing transparency
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_AlwaysStackOnTop, true);  // Stay above device screen
    setAutoFillBackground(false);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);  // Better for transparent overlays

    _animTimer = new QTimer(this);
    _animTimer->setInterval(16); // ~60fps
    connect(_animTimer, &QTimer::timeout, this, &HudOverlayGL::onAnimationTick);
}

HudOverlayGL::~HudOverlayGL()
{
    if (_animTimer)
    {
        _animTimer->stop();
    }
}

void HudOverlayGL::setModel(std::shared_ptr<HudModel> model)
{
    if (_model == model)
        return;

    _model = model;

    if (_model)
    {
        _model->setChangedCallback([this]() { onModelChanged(); });
    }

    update();
}

void HudOverlayGL::setTheme(HudThemeId themeId)
{
    _themeId = themeId;
    if (_model)
    {
        _model->setTheme(themeId);
    }
    update();
}

void HudOverlayGL::setScaleFactor(float factor)
{
    _scaleFactor = factor;
    update();
}

void HudOverlayGL::setToastPosition(HudTilePosition position)
{
    _toastPosition = position;
    update();
}

void HudOverlayGL::setIndicatorPosition(HudTilePosition position)
{
    _indicatorPosition = position;
    update();
}

void HudOverlayGL::syncGeometryWithParent()
{
    if (parentWidget())
    {
        setGeometry(parentWidget()->rect());
    }
}

void HudOverlayGL::onModelChanged()
{
    bool needsAnimation = false;
    if (_model)
    {
        auto snap = _model->snapshot();
        if (snap)
        {
            for (const auto& el : snap->elements)
            {
                if (el.kind == HudKind::Toast || el.ttl.count() > 0)
                {
                    needsAnimation = true;
                    break;
                }
            }
        }
    }

    if (needsAnimation && !_hasActiveAnimations)
    {
        _lastTick = HudClock::now();
        _animTimer->start();
        _hasActiveAnimations = true;
    }

    update();
}

void HudOverlayGL::onAnimationTick()
{
    if (!_model)
    {
        _animTimer->stop();
        _hasActiveAnimations = false;
        return;
    }

    auto now = HudClock::now();
    _model->expire(now);
    _lastTick = now;

    bool stillAnimating = false;
    auto snap = _model->snapshot();
    if (snap)
    {
        for (const auto& el : snap->elements)
        {
            if (el.kind == HudKind::Toast || el.ttl.count() > 0)
            {
                stillAnimating = true;
                break;
            }
        }
    }

    if (!stillAnimating)
    {
        _animTimer->stop();
        _hasActiveAnimations = false;
    }

    update();
}

bool HudOverlayGL::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
    {
        syncGeometryWithParent();
    }
    return QOpenGLWidget::eventFilter(watched, event);
}

void HudOverlayGL::initializeGL()
{
    initializeOpenGLFunctions();

    // Enable blending for transparency compositing
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
}

void HudOverlayGL::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void HudOverlayGL::paintGL()
{
    // Clear with fully transparent background
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!_model)
        return;

    auto snap = _model->snapshot();
    if (!snap || snap->elements.empty())
        return;

    // Use QPainter for text rendering - must begin AFTER glClear
    // QPainter handles blending internally
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);

    const HudTheme& theme = HudTheme::FromId(_themeId);
    float uiScale = _scaleFactor;

    const auto& elements = snap->elements;
    auto now = HudClock::now();

    // Calculate layout positions
    QRect indicatorArea, toastArea;
    int margin = static_cast<int>(8 * uiScale);

    // Indicators area (top-right by default)
    {
        int x = 0, y = 0;
        switch (_indicatorPosition)
        {
            case HudTilePosition::TopLeft:
                x = margin;
                y = margin;
                break;
            case HudTilePosition::TopRight:
                x = width() - margin;
                y = margin;
                break;
            case HudTilePosition::BottomLeft:
                x = margin;
                y = height() - margin;
                break;
            case HudTilePosition::BottomRight:
                x = width() - margin;
                y = height() - margin;
                break;
            default:
                x = width() - margin;
                y = margin;
        }
        indicatorArea = QRect(x, y, 0, 0);
    }

    // Draw indicators
    int indicatorSpacing = static_cast<int>(4 * uiScale);
    int indicatorSize = static_cast<int>(24 * uiScale);
    int indicatorX = indicatorArea.x();
    int indicatorY = indicatorArea.y();

    for (const auto& el : elements)
    {
        if (el.kind != HudKind::Indicator)
            continue;

        const auto& style = theme.indicator;

        // Calculate position (flow from anchor)
        QRect rect;
        if (_indicatorPosition == HudTilePosition::TopRight ||
            _indicatorPosition == HudTilePosition::BottomRight)
        {
            indicatorX -= indicatorSize;
            rect = QRect(indicatorX, indicatorY, indicatorSize, indicatorSize);
            indicatorX -= indicatorSpacing;
        }
        else
        {
            rect = QRect(indicatorX, indicatorY, indicatorSize, indicatorSize);
            indicatorX += indicatorSize + indicatorSpacing;
        }

        // Draw indicator background
        QColor bgColor = QColor::fromRgba(style.frame.backgroundColor);
        bgColor.setAlphaF(bgColor.alphaF() * 0.85f);

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, style.frame.borderRadius * uiScale,
                                style.frame.borderRadius * uiScale);

        // Draw border
        if (style.frame.borderWidth > 0)
        {
            painter.setPen(QPen(QColor::fromRgba(style.frame.borderColor),
                               style.frame.borderWidth * uiScale));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect, style.frame.borderRadius * uiScale,
                                    style.frame.borderRadius * uiScale);
        }

        // Draw label/icon
        painter.setPen(QColor::fromRgba(style.content.titleColor));
        QFont font = painter.font();
        font.setPixelSize(static_cast<int>(10 * uiScale));
        font.setBold(true);
        painter.setFont(font);
        // Use title or value for the indicator text
        QString text = QString::fromStdString(el.value.empty() ? el.title : el.value);
        painter.drawText(rect, Qt::AlignCenter, text);
    }

    // Draw toasts
    int toastWidth = static_cast<int>(200 * uiScale);
    int toastHeight = static_cast<int>(40 * uiScale);
    int toastY = height() - margin - toastHeight;

    for (const auto& el : elements)
    {
        if (el.kind != HudKind::Toast)
            continue;

        const auto& style = theme.toast;

        float opacity = el.opacity;
        float scale = 1.0f;

        if (opacity <= 0.01f)
            continue;

        int scaledWidth = static_cast<int>(toastWidth * scale);
        int scaledHeight = static_cast<int>(toastHeight * scale);
        int x = (width() - scaledWidth) / 2;
        int y = toastY + (toastHeight - scaledHeight) / 2;

        QRect rect(x, y, scaledWidth, scaledHeight);

        // Draw toast background with opacity
        QColor bgColor = QColor::fromRgba(style.frame.backgroundColor);
        bgColor.setAlphaF(bgColor.alphaF() * opacity);

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, style.frame.borderRadius * uiScale * scale,
                                style.frame.borderRadius * uiScale * scale);

        // Draw border
        if (style.frame.borderWidth > 0)
        {
            QColor borderColor = QColor::fromRgba(style.frame.borderColor);
            borderColor.setAlphaF(borderColor.alphaF() * opacity);
            painter.setPen(QPen(borderColor, style.frame.borderWidth * uiScale));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect, style.frame.borderRadius * uiScale * scale,
                                    style.frame.borderRadius * uiScale * scale);
        }

        // Draw text (title or body)
        QColor textColor = QColor::fromRgba(style.content.titleColor);
        textColor.setAlphaF(textColor.alphaF() * opacity);
        painter.setPen(textColor);

        QFont font = painter.font();
        font.setPixelSize(static_cast<int>(12 * uiScale * scale));
        painter.setFont(font);
        QString text = QString::fromStdString(el.title.empty() ? el.body : el.title);
        painter.drawText(rect, Qt::AlignCenter, text);

        toastY -= toastHeight + static_cast<int>(8 * uiScale);
    }

    painter.end();
}
