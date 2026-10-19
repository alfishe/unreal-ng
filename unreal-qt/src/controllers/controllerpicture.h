#pragma once

/// @file controllerpicture.h
/// @brief The picture of a host device with its callouts (ui-ux.md §2, device-database.md §5): the image set's
/// front.svg, every hotspot of front.hotspots.json lit while its control is pressed (a stick's dot follows the
/// stick, a trigger fills), and a callout per control on its side, sorted by the anchor's height, naming the
/// control and its binding. A click on a hotspot reports the control.

#include <QString>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

class QSvgRenderer;

class ControllerPicture : public QWidget
{
    Q_OBJECT

public:
    explicit ControllerPicture(QWidget* parent = nullptr);

    /// Load data/controllers/images/<imageSet>; false (and an empty picture) when the set is missing
    bool load(const QString& imageSet);
    bool empty() const
    {
        return _hotspots.empty();
    }
    /// The callout text per control ("A  kempston:fire")
    void setCaptions(const std::map<std::string, QString>& captions);
    /// The live values per control; repaints only on a change
    void setLive(const std::map<std::string, float>& live);
    /// The control drawn as selected
    void setSelected(const std::string& control);

    QSize sizeHint() const override;

signals:
    void controlClicked(const QString& control);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Hotspot
    {
        std::string control;
        QString label;
        bool circle = false;
        QRectF rect;  ///< the rect, or the circle's bounding box (image units)
        QPointF anchor;
        bool left = true;
        QString analog;  ///< "", "trigger", "stick", "encoder"
        std::string pair;  ///< a stick's other axis
        double radius = 0;
        double steps = 0;  ///< an encoder's steps per turn
    };

    QRectF imageRect() const;
    QPointF toWidget(const QPointF& point) const;

    QSvgRenderer* _svg = nullptr;
    QSizeF _size;
    std::vector<Hotspot> _hotspots;
    std::map<std::string, QString> _captions;
    std::map<std::string, float> _live;
    std::string _selected;
};
