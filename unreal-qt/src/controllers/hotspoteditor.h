#pragma once

/// @file hotspoteditor.h
/// @brief The hotspot editor (docs/inprogress/2026-10-17-controller-manager/device-database.md §9, ui-ux.md §2): a
/// photo of the user's own pad as the device's picture. Pick a file or paste one, then place the device's controls on
/// it: choose a control in the list (or press it on the pad: the list follows the pad), click its spot on the photo,
/// set its size and the side of its callout (proposed from where it lies). Save copies the photo and the map into
/// `<user config>/unreal-ng/controllers/images/user-<family>/` (front.png + front.hotspots.json, coordinates in the
/// 2048 x 1536 design space the photo is letterboxed into) and records the image set in devices.user.json through the
/// `database-save` host verb, the path WebAPI, MCP, CLI and Python take. A device the database does not know gets an
/// entry of its own by its USB ids (`user-045e0b22`): it takes effect when the device connects again.

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QString>

#include <functional>
#include <map>
#include <string>
#include <vector>

class HotspotCanvas;
class QComboBox;
class QLabel;
class QListWidget;
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
        std::string imageSet;  ///< the picture shown now: its map is the starting point
        std::vector<std::string> controls;  ///< the database's `controls` (`dpad.*` as written); empty: a pad's set
    };

    struct Hotspot
    {
        std::string control;
        QString label;
        bool circle = true;
        QPointF centre;   ///< design space
        double size = 0;  ///< a circle's radius; a rect's half width (its height keeps the shape's aspect)
        double aspect = 1;  ///< a rect's height / width
        bool left = true;   ///< the callout's side
        bool placed = false;
    };

    /// What the editor needs from the host: the pad's live controls (press-to-select), the user's controllers folder
    /// and the `database-save` of the entry. The dialog wires them to HostControllers; the tests to their own
    struct Hooks
    {
        std::function<std::map<std::string, float>()> live;
        std::function<bool(const std::string& entry, QString& error)> save;
        QString userDir;
    };

    HotspotEditor(const Target& target, Hooks hooks, QWidget* parent = nullptr);

    void setPhoto(const QImage& photo);
    const QImage& photo() const
    {
        return _photo;
    }
    /// The control the list has selected
    std::string selectedControl() const;
    void selectControl(const std::string& control);
    /// Place the selected control at @p design (design space): the callout side follows the x
    void placeAt(const QPointF& design);
    /// The pad's live controls: a control that goes pressed selects its row
    void setLive(const std::map<std::string, float>& live);
    const std::vector<Hotspot>& hotspots() const
    {
        return _hotspots;
    }
    /// The image set Save writes, and the family its devices.user.json entry names
    std::string imageSet() const;
    std::string entryFamily() const;
    /// The devices.user.json entry Save records (JSON text)
    std::string entryText() const;
    /// The front.hotspots.json Save writes (JSON text)
    QByteArray mapText() const;
    /// Write the photo and the map under the user's folder and record the entry; false with the reason
    bool save(QString& error);

private:
    void rebuildList();
    void showSelected();
    void loadMap(const QString& setDir);
    Hotspot* find(const std::string& control);

    Target _target;
    Hooks _hooks;
    QImage _photo;
    std::vector<Hotspot> _hotspots;
    std::map<std::string, float> _lastLive;
    HotspotCanvas* _canvas = nullptr;
    QListWidget* _list = nullptr;
    QComboBox* _callout = nullptr;
    QSpinBox* _size = nullptr;
    QLabel* _status = nullptr;
    QTimer* _live = nullptr;
    bool _updating = false;
};
