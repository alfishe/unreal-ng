// The HUD drawn once per change (hud/qt/hudoverlay.h, renderHUD): the screen repaints the HUD with every frame, and an
// automation session keeps its indicator on screen for as long as it lasts. A HUD that does not move with time is laid
// out and drawn into a layer once per change and blitted from it on every paint.
// Draw calls are counted; no pixels are compared and no time is measured.

#include <gtest/gtest.h>

#include <QImage>
#include <QPaintEngine>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "hud/qt/hudoverlay.h"

namespace
{
/// A surface that only counts what is drawn on it: the text laid out and the images blitted per paint
class CountingEngine : public QPaintEngine
{
public:
    CountingEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}
    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    void updateState(const QPaintEngineState&) override {}
    void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
    void drawImage(const QRectF&, const QImage&, const QRectF&, Qt::ImageConversionFlags) override { ++images; }
    void drawTextItem(const QPointF&, const QTextItem&) override { ++texts; }
    void drawPath(const QPainterPath&) override {}
    void drawPolygon(const QPointF*, int, PolygonDrawMode) override {}
    Type type() const override { return QPaintEngine::User; }

    int texts = 0;
    int images = 0;
};

class CountingDevice : public QPaintDevice
{
public:
    QPaintEngine* paintEngine() const override { return &engine; }
    int metric(PaintDeviceMetric metric) const override
    {
        switch (metric)
        {
            case PdmWidth: return 352;
            case PdmHeight: return 288;
            case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 72;
            case PdmDevicePixelRatio: return 1;
            case PdmDevicePixelRatioScaled: return static_cast<int>(QPaintDevice::devicePixelRatioFScale());
            case PdmDepth: return 32;
            case PdmNumColors: return INT_MAX;
            default: return 0;
        }
    }

    mutable CountingEngine engine;
};
}  // namespace

class HudOverlay_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _model = std::make_shared<HudModel>();
        _model->setIndicator("remote", HudState::Active, "perf-probe via WebAPI", {}, "remote");
        _overlay.setModel(_model);
    }

    /// One paint of the screen: the picture under the HUD, then the HUD
    QImage Paint(qreal dpr)
    {
        QImage image(static_cast<int>(std::ceil(kWidth * dpr)), static_cast<int>(std::ceil(kHeight * dpr)),
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(kBackground);
        image.setDevicePixelRatio(dpr);
        QPainter painter(&image);
        _overlay.renderHUD(painter, kWidth, kHeight, static_cast<float>(dpr));
        return image;
    }

    static inline const QColor kBackground{32, 48, 64};
    static constexpr int kWidth = 352;
    static constexpr int kHeight = 288;
    std::shared_ptr<HudModel> _model;
    HudOverlay _overlay;
};

// The screen's paints with a session held: the indicator's text is laid out once, not on each of the frames (the code
// before the layer: 50 text items for 50 paints)
TEST_F(HudOverlay_Test, StillIndicatorIsNotLaidOutPerPaint)
{
    CountingDevice device;
    for (int frame = 0; frame < 50; ++frame)
    {
        QPainter painter(&device);
        _overlay.renderHUD(painter, kWidth, kHeight, 1.0f);
    }
    EXPECT_EQ(device.engine.texts, 0) << "the indicator's text was laid out on every paint";
    EXPECT_EQ(device.engine.images, 50) << "each paint blits the layer";
}

TEST_F(HudOverlay_Test, StillIndicatorIsDrawnOncePerChange)
{
    const quint64 before = _overlay.layerSerial();
    for (int frame = 0; frame < 50; ++frame)
        Paint(1.0);
    EXPECT_EQ(_overlay.layerSerial(), before + 1) << "a session's indicator is laid out again on every frame";

    // The indicator changes: drawn again, once
    _model->setIndicator("remote", HudState::Active, "another client via MCP", {}, "remote");
    for (int frame = 0; frame < 10; ++frame)
        Paint(1.0);
    EXPECT_EQ(_overlay.layerSerial(), before + 2);

    // Another surface (a resize, another display's ratio): drawn again for it
    Paint(2.0);
    Paint(2.0);
    EXPECT_EQ(_overlay.layerSerial(), before + 3);
}

TEST_F(HudOverlay_Test, ToastsAreDrawnOnEachPaint)
{
    HudToastRequest toast;
    toast.title = "Remote control";
    toast.body = "perf-probe controls the emulator";
    _model->notify(toast);
    EXPECT_FALSE(_overlay.prepareLayer(kWidth, kHeight, 1.0f)) << "a toast moves with time: no layer";
    const quint64 serial = _overlay.layerSerial();
    Paint(1.0);
    Paint(1.0);
    EXPECT_EQ(_overlay.layerSerial(), serial);
}

TEST_F(HudOverlay_Test, EmptyHudDrawsNothing)
{
    _model->clearIndicator("remote");
    EXPECT_TRUE(_overlay.prepareLayer(kWidth, kHeight, 1.0f));
    EXPECT_TRUE(_overlay.layerEmpty());
    QImage plain(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
    plain.fill(kBackground);
    EXPECT_EQ(Paint(1.0), plain);
}
