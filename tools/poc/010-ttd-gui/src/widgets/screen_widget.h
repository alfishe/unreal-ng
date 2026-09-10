#pragma once
//
// screen_widget.h — QWidget wrapping a QImage, scaled to fit.
//

#include <QWidget>
#include <QImage>

namespace ttd {

class ScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit ScreenWidget(QWidget* parent = nullptr);

    void setImage(const QImage& img);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {256, 192}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage _img;
};

} // namespace ttd
