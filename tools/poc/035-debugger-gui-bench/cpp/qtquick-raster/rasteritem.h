#pragma once

// Qt Quick front end for the shared CPU raster views (qtcommon/rasterviews.h): each view is one QSGImageNode whose
// texture is the view's image, re-made only when the view reports a change. The same item runs on the RHI and on the
// software adaptation (QT_QUICK_BACKEND=software).

#include "../qtcommon/rasterviews.h"

#include <QQuickItem>

#include <memory>

class RasterItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString view READ view WRITE setView)
public:
    explicit RasterItem(QQuickItem* parent = nullptr);
    static void SetWorkload(bench::Workload* w) { s_w = w; }
    QString view() const { return _view; }
    void setView(const QString& v) { _view = v; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    static bench::Workload* s_w;
    QString _view;
    std::unique_ptr<raster::RasterView> _rv;
};
