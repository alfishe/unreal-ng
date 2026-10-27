#pragma once

/// @file controllerpicture.h
/// @brief The picture of a host device with its callouts (ui-ux.md §2, device-database.md §5, §8): a view of the
/// image set (`<view>.svg`, or a photo `<view>.png` / `.jpg`), every hotspot of `<view>.hotspots.json` (a circle,
/// an ellipse, a rectangle or a polygon) lit while its control is pressed (a stick's dot follows the stick, a
/// trigger fills), and a callout per control on its side, sorted by the anchor's height, naming the control and its
/// binding. A click on a hotspot reports the control. Compact (a connector card's retro device, ui-ux.md §4): the
/// picture alone, no callouts, the hotspots lit in the card's colour.

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QString>
#include <QStringList>
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

    /// Load the image set: the user's (<user dir>/images/<set>, a photo front.png / front.jpg or a front.svg) before
    /// the shipped data/controllers/images/<set>; false (and an empty picture) when the set is missing
    bool load(const QString& imageSet, const QString& view = QStringLiteral("front"));
    /// The views an image set has (`front`, `back`, `top`: each with its hotspot map), front first
    static QStringList Views(const QString& imageSet);
    const QString& view() const
    {
        return _view;
    }
    /// No callout columns: the picture fills the widget (a connector card)
    void setCompact(bool compact);
    /// The colour a pressed control lights in (default amber)
    void setLitColour(const QColor& colour);
    /// The user's controllers folder (where devices.user.json lives); set once at start
    static void SetUserDir(const QString& dir);
    static QString UserDirectory();
    /// The folder of an image set with its trailing '/', the user's first; empty when there is none
    static QString ImageSetDir(const QString& imageSet);
    /// The drawn picture's rectangle (widget coordinates); empty without a picture
    QRectF pictureRect() const
    {
        return imageRect();
    }
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
        bool circle = false;  ///< a circle or an ellipse: drawn as the ellipse in rect
        QRectF rect;          ///< the shape's bounding box (image units)
        QPainterPath path;    ///< the shape (image units): hit test and highlight
        QPointF anchor;
        bool left = true;
        QString analog;  ///< "", "trigger", "stick", "encoder"
        std::string pair;  ///< a stick's other axis
        double radius = 0;
        double steps = 0;  ///< an encoder's steps per turn
        bool quiet = false;  ///< a callout only while bound, held or selected (a keyboard's hundred keys)
    };

    QRectF imageRect() const;
    QPointF toWidget(const QPointF& point) const;
    /// A hotspot's shape in widget coordinates
    QPainterPath shapeOf(const Hotspot& spot) const;
    int calloutWidth() const;

    QSvgRenderer* _svg = nullptr;
    QImage _raster;  ///< a photo set's front.png / front.jpg (then _svg is empty)
    QSizeF _size;
    std::vector<Hotspot> _hotspots;
    std::map<std::string, QString> _captions;
    std::map<std::string, float> _live;
    std::string _selected;
    QString _view = QStringLiteral("front");
    bool _compact = false;
    QColor _lit{255, 170, 0, 150};
};
