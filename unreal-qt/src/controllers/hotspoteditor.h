#pragma once

/// @file hotspoteditor.h
/// @brief The hotspot editor (docs/inprogress/2026-10-17-controller-manager/device-database.md §8, §9, ui-ux.md §3.6):
/// photos of the user's own pad as the device's picture. Per view (front, back, top) pick a file or paste one,
/// straighten it (90° turns and a fine angle) and crop it, then place the device's controls on it: choose a control
/// in the list (or press it on the pad: the list follows the pad), choose its shape (a circle, an ellipse, a
/// rectangle, or a polygon drawn point by point) and click its spot on the photo, set its size and the side of its
/// callout (proposed from where it lies). Save writes each view's photo and map into
/// `<user config>/unreal-ng/controllers/images/<device id>/` (`<view>.png` + `<view>.hotspots.json`, coordinates in
/// the 2048 x 1536 design space the photo is letterboxed into; the front is required) and records the image set in
/// devices.user.json through the `database_save` host verb, the path WebAPI, MCP, CLI and Python take. The device id
/// is the database entry's: a known family keeps its own (the user's set is read before the shipped one of the same
/// name), a device the database does not know gets an entry of its own by its USB ids (`user-045e0b22`) that takes
/// effect when the device connects again. Share… exports the same set as a folder with a provenance stub to fill in
/// and the entry, ready for a pull request.

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <string>
#include <vector>

class HotspotCanvas;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTimer;

class HotspotEditor : public QDialog
{
    Q_OBJECT

public:
    struct Target
    {
        std::string device;    ///< the host device's id ("bt:045e:0b22:..."): its live controls, its USB ids
        std::string family;    ///< "xbox-elite-2"; "generic" for a device the database does not know
        std::string name;      ///< its display name
        std::string imageSet;  ///< the picture shown now: its maps are the starting point
        std::vector<std::string> controls;  ///< the database's `controls` (`dpad.*` as written); empty: a pad's set
    };

    enum class Shape
    {
        Circle,
        Ellipse,
        Rect,
        Polygon,
    };

    struct Hotspot
    {
        std::string control;
        QString label;
        Shape shape = Shape::Circle;
        QPointF centre;     ///< design space (a polygon: its bounding box's centre)
        double size = 0;    ///< a circle's radius; an ellipse's x radius; a rect's half width
        double aspect = 1;  ///< an ellipse's or a rect's height / width
        QPolygonF polygon;  ///< a polygon's points (design space)
        bool left = true;   ///< the callout's side
        bool placed = false;
    };

    /// What the editor needs from the host: the pad's live controls (press-to-select), the user's controllers folder
    /// and the `database_save` of the entry. The dialog wires them to HostControllers; the tests to their own
    struct Hooks
    {
        std::function<std::map<std::string, float>()> live;
        std::function<bool(const std::string& entry, QString& error)> save;
        QString userDir;
    };

    HotspotEditor(const Target& target, Hooks hooks, QWidget* parent = nullptr);

    /// The views a set may have, front first
    static const QStringList& Views();
    /// The view edited now; switching keeps each view's photo and spots
    void setView(const QString& view);
    const QString& view() const
    {
        return _view;
    }

    /// The current view's photo (as straightened and cropped); a new one drops the view's turn, angle and crop
    void setPhoto(const QImage& photo);
    const QImage& photo() const;
    /// Turn the current view's photo by quarter turns (+1 clockwise); the placed spots of the view go
    void rotateQuarter(int turns);
    /// Straighten by @p degrees (-15..15, over the quarter turns); the placed spots of the view go
    void setStraighten(double degrees);
    /// Crop the current view to @p design (design space, inside the letterboxed photo); the placed spots go
    void cropTo(const QRectF& design);
    /// Back to the whole photo, turned and straightened as it is
    void resetCrop();

    /// The control the list has selected
    std::string selectedControl() const;
    void selectControl(const std::string& control);
    /// The shape the next placement takes (the selected spot's, changed here)
    void setShape(Shape shape);
    Shape shape() const;
    /// Place the selected control at @p design (design space): the callout side follows the x. A polygon: adds a point
    void placeAt(const QPointF& design);
    /// Place the selected control as the polygon @p points (design space; 3 points at least)
    bool placePolygon(const QPolygonF& points);
    /// Close the polygon being drawn; false with fewer than 3 points
    bool finishPolygon();
    /// The pad's live controls: a control that goes pressed selects its row
    void setLive(const std::map<std::string, float>& live);
    /// The current view's spots
    const std::vector<Hotspot>& hotspots() const;
    /// The image set Save writes (the database device id), and the family its devices.user.json entry names
    std::string imageSet() const;
    std::string entryFamily() const;
    /// The devices.user.json entry Save records (JSON text)
    std::string entryText() const;
    /// A view's `<view>.hotspots.json` (JSON text)
    QByteArray mapText(const QString& view = QStringLiteral("front")) const;
    /// Write the views' photos and maps under the user's folder and record the entry; false with the reason
    bool save(QString& error);
    /// Share…: the same set into `<parent>/<image set>/` with `provenance.json` (a stub to fill in) and `entry.json`;
    /// false with the reason
    bool exportBundle(const QString& parent, QString& error) const;

private:
    struct ViewData
    {
        QImage source;  ///< as picked
        int quarter = 0;
        double angle = 0;
        QRectF crop{0, 0, 1, 1};  ///< share of the turned image
        QImage photo;             ///< derived: turned, straightened, cropped
        std::vector<Hotspot> spots;
        bool loaded = false;  ///< from an existing set: written back even when untouched
    };

    void rebuildList();
    void showSelected();
    void loadView(const QString& setDir, const QString& view);
    void derive(ViewData& data);
    /// After a turn, an angle or a crop: the placed spots of the view no longer fit it
    void dropPlaced(ViewData& data);
    /// Validate and write the views into @p dir; false with the reason
    bool writeViews(const QString& dir, QString& error) const;
    Hotspot* find(const std::string& control);
    ViewData& current();
    const ViewData& current() const;

    Target _target;
    Hooks _hooks;
    std::vector<std::string> _controls;
    std::map<QString, ViewData> _views;
    QString _view = QStringLiteral("front");
    QPolygonF _pending;  ///< the polygon being drawn
    Shape _shape = Shape::Circle;
    std::map<std::string, float> _lastLive;
    HotspotCanvas* _canvas = nullptr;
    QComboBox* _viewBox = nullptr;
    QListWidget* _list = nullptr;
    QComboBox* _shapeBox = nullptr;
    QComboBox* _callout = nullptr;
    QSpinBox* _size = nullptr;
    QSpinBox* _aspect = nullptr;
    QDoubleSpinBox* _angle = nullptr;
    QPushButton* _crop = nullptr;
    QPushButton* _finish = nullptr;
    QLabel* _status = nullptr;
    QTimer* _live = nullptr;
    bool _updating = false;
};
