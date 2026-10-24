#pragma once

/// @file controllerpicture.h
/// @brief The picture of a host device with its callouts (ui-ux.md §2, device-database.md §5): the image set's
/// front.svg, every hotspot of front.hotspots.json lit while its control is pressed (a stick's dot follows the
/// stick, a trigger fills), and a callout per control on its side, sorted by the anchor's height, naming the
/// control and its binding. A click on a hotspot reports the control.

#include <QImage>
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

    /// Load the image set: the user's (<user dir>/images/<set>, a photo front.png / front.jpg or a front.svg) before
    /// the shipped data/controllers/images/<set>; false (and an empty picture) when the set is missing
    bool load(const QString& imageSet);
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
        bool circle = false;
        QRectF rect;  ///< the rect, or the circle's bounding box (image units)
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

    QSvgRenderer* _svg = nullptr;
    QImage _raster;  ///< a photo set's front.png / front.jpg (then _svg is empty)
    QSizeF _size;
    std::vector<Hotspot> _hotspots;
    std::map<std::string, QString> _captions;
    std::map<std::string, float> _live;
    std::string _selected;
};
