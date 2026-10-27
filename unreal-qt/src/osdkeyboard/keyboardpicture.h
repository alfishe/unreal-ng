#pragma once

/// @file keyboardpicture.h
/// @brief The machines' own keyboards for the on-screen keyboard (POC 031, tools/poc/031-authentic-osd-keyboards):
/// data/controllers/keyboards/index.json names the pictures of each model (the first its default), each picture is an
/// SVG with its key map (<id>.keys.json: every key's rectangle in the picture's mm, an L key's outline, and what it
/// presses: ZX matrix keys, a PC key of the machine's keyboard controller, or a line not wired yet). Every picture comes
/// in each style of the index (<id>.<style>.svg: photo, schematic, hybrid), the key map the same for all of them.

#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <map>
#include <string>
#include <vector>

#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"

struct PictureKey
{
    std::string id;
    QString label;
    QRectF rect;                  ///< mm, the picture's coordinates
    QPolygonF shape;              ///< an L key's outline; empty: the rect
    std::vector<ZXKeysEnum> zx;   ///< matrix keys pressed together (True Video: CS + 3), in press order
    PcKey pc = PcKey::None;       ///< the PC key (a "pc" picture)
    std::string line;             ///< a line the core does not take yet ("JOY:UP", "RESET")

    bool contains(const QPointF& point) const;
    /// Nothing to press: a line not wired yet, a key of unknown wiring (no line), or a name the core does not know
    bool inert() const { return zx.empty() && pc == PcKey::None; }
    /// A modifier: a click latches it until the next key goes up (CS, SS, the Orel BK-08's РЕГ and Р/А; PC Shift,
    /// Ctrl, Alt, Win)
    bool modifier() const;
};

struct KeyboardPicture
{
    std::string id;
    QString name;
    bool pc = false;              ///< the keys press the machine's PC keyboard controller, not the matrix
    QSizeF size;                  ///< mm
    std::string style;            ///< the style it is drawn in ("photo")
    QString svgPath;
    std::vector<PictureKey> keys;

    /// The key under a point (mm), nullptr outside every key
    const PictureKey* keyAt(const QPointF& point) const;
};

class KeyboardPictures
{
public:
    /// Read the index of @p root (data/controllers/keyboards); false when it is missing or unreadable
    bool load(const QString& root);
    /// The pictures of a model by its short name ("48K", "ATM710"), the default first; empty for an unknown model
    std::vector<std::string> forModel(const std::string& model) const;
    QString name(const std::string& id) const;
    /// The styles the pictures are drawn in, the default first ("photo", "schematic", "hybrid")
    const std::vector<std::string>& styles() const { return _styles; }
    /// Load one picture's key map and its SVG in @p style (an unknown style: the default); false (and @p error)
    /// when it is missing or a key names nothing the core knows
    bool picture(const std::string& id, KeyboardPicture& out, QString& error, const std::string& style = {}) const;
    /// The matrix a picture's keys drive ("zx", "pc"); empty for an unknown id
    std::string matrix(const std::string& id) const;

private:
    QString _root;
    std::map<std::string, std::vector<std::string>> _models;
    std::map<std::string, QString> _names;
    std::map<std::string, std::string> _matrices;
    std::vector<std::string> _styles;
};
