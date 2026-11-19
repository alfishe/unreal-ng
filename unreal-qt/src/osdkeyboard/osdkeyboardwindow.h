#pragma once

/// @file osdkeyboardwindow.h
/// @brief The on-screen keyboard (`unrealqt.osd_keyboard`, POC 031): the bound machine's own keyboard in a
/// window of its own that never takes the focus from the screen. A click presses a key while the button is held; a
/// modifier (CS, SS, PC Shift / Ctrl / Alt / Win) latches until the next key goes up, a second click lets it go. The
/// machines with more than one keyboard (the PC clones: their PC keyboard and the 40-key ZX matrix; Pentagon,
/// Profi) get a switch, the choice kept per model. A ZX picture presses the matrix only, a PC picture the machine's
/// PC keyboard controller only (MC_KEY_* / MC_PCKEY_*, as the host keyboard's two halves); the line under the
/// picture says when the Host Keyboard route keeps them from the machine. Keys held on the host keyboard light too.
/// A second switch draws every picture in another style (photo, schematic, hybrid with the hints), kept for all models.

#include <QElapsedTimer>
#include <QEvent>
#include <QImage>
#include <QWidget>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "osdkeyboard/keyboardpicture.h"

class EmulatorBinding;
class EmulatorContext;
class QComboBox;
class QLabel;
class QPainter;
class QSvgRenderer;
class QTimer;

class OsdKeyboardView : public QWidget
{
    Q_OBJECT

public:
    explicit OsdKeyboardView(QWidget* parent = nullptr);

    void setPicture(const KeyboardPicture* picture);
    /// The keys drawn as held (the window's own presses and latches, and the host keyboard's)
    void setLit(std::set<std::string> lit);
    /// Draw the legends of @p roles faint (`legends-<role>` groups of the SVG: "keyword", "extend"...): the legends
    /// that do not matter while a shift is held; empty: every legend as printed
    void setDimmed(std::set<std::string> roles);
    const std::set<std::string>& dimmed() const { return _dimmed; }
    /// The key a pad's cursor is on, drawn with a ring ("" none)
    void setFocusKey(const std::string& id);
    /// The picture's point (mm) under a widget point
    QPointF toPicture(const QPointF& point) const;

    /// The picture with its lit keys, @p width pixels wide (the window recording)
    QImage renderImage(int width);

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

signals:
    void keyPressed(const PictureKey* key);
    void keyReleased();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QRectF imageRect() const;
    /// The renderer of the picture as drawn now: the printed one, or the one with the dimmed roles faint
    QSvgRenderer* renderer() const;
    /// The minimal keyboard: no SVG, the keys drawn as rectangles (PaintVectorKeys)
    bool vector() const;
    bool drawable() const;

    const KeyboardPicture* _picture = nullptr;
    QSvgRenderer* _svg = nullptr;
    QSvgRenderer* _dimSvg = nullptr;  ///< the picture with the dimmed roles faint
    QByteArray _svgData;              ///< the SVG as read, the source of the dimmed one
    QPixmap _cache[2];  ///< the picture at the widget's size, printed and dimmed: a repaint draws only the lit keys
    QImage _recordBase[2];  ///< the picture at the recording's width, without lit keys
    std::set<std::string> _lit;
    std::set<std::string> _dimmed;
    std::string _focusKey;
};

class OsdKeyboardWindow : public QWidget
{
    Q_OBJECT

public:
    /// @p overlay (handheld mode: gamescope shows one window of the app, a tool window would take the whole screen
    /// from the game): never on screen as a widget; setVisible shows it as a translucent picture the screen draws
    /// over the game (paintOverlay), without the picture and style switches
    explicit OsdKeyboardWindow(QWidget* parent = nullptr, bool overlay = false);
    ~OsdKeyboardWindow() override;

    void setBinding(EmulatorBinding* binding);

    /// Overlay mode: shown or not without a widget on screen
    void setVisible(bool visible) override;
    /// Shown: the window, or the overlay
    bool shown() const { return _overlay ? _overlayShown : isVisible(); }
    bool overlay() const { return _overlay; }
    /// The overlay at the bottom of the screen's @p width x @p height, fitted by the picture's ratio: the picture
    /// with its lit keys, translucent, and the pad's cursor
    void paintOverlay(QPainter& painter, int width, int height);
    /// A pointer on the screen of @p width x @p height (the Deck's touchscreen comes as one): a press on a key of the
    /// overlay presses it, the release lets it go. True while the overlay shows: it takes every pointer event, so the
    /// machine's mouse gets none and no cursor shows
    bool overlayPointer(QEvent::Type type, const QPointF& at, int width, int height);
    /// Tap one matrix key (the pad's SPACE / ENTER), as a tap of a key of the picture
    void tapMatrixKey(ZXKeysEnum key);
    /// Read the pictures from @p root instead of the resources (tests)
    void setRoot(const QString& root);

    /// The model shown (its short name) and the picture of it; empty without a machine or a picture
    const std::string& model() const { return _model; }
    const KeyboardPicture& picture() const { return _picture; }
    /// The pictures of the model the switch offers
    const std::vector<std::string>& choices() const { return _choices; }
    void choose(const std::string& id);
    /// The style the pictures are drawn in and the styles offered (the default first)
    const std::string& style() const { return _style; }
    const std::vector<std::string>& styles() const { return _pictures.styles(); }
    /// Draw the picture shown, and every one after it, in @p style; an unknown style changes nothing
    void chooseStyle(const std::string& style);
    /// Show a picture of another model whose keys drive the same matrix (the ZX Next's on a Pentagon), not kept in
    /// the settings and not offered by the switch; false (and @p error) for an unknown id or another matrix
    bool chooseOther(const std::string& id, std::string& error, bool vectorOnly = false);
    /// The picture with its lit keys, @p width pixels wide; null without a picture (the window recording)
    QImage renderImage(int width) { return _view->renderImage(width); }
    /// Press / let go of a key by its id, as a click does (tests)
    void press(const std::string& id);
    void release();
    /// The keyboard shown whatever the model (Settings: the handheld menu, `unrealqt.osd_keyboard_force`): "" the
    /// machine's own, "minimal" the bare ZX matrix (zxmatrix), "steam" the system keyboard of Steam (MainWindow opens
    /// it in place of this window), or a picture id of the same matrix as the machine's. Kept in the settings
    const std::string& forced() const { return _forced; }
    /// False (and @p error) for an id the index does not know
    bool setForced(const std::string& id, std::string& error);
    /// Every picture of the index with its name, for a choice of the forced one
    std::vector<std::pair<std::string, QString>> allPictures();
    /// The pictures whose keys drive one of this machine's matrices (what the handheld menu offers)
    std::vector<std::pair<std::string, QString>> fittingPictures();
    /// The pad's cursor (the handheld pad, `unrealqt.osd_keyboard_nav` / `_tap`): the key it is on ("" none, the
    /// first move puts it on the picture's middle key), moved to the nearest key in a direction
    const std::string& focusKey() const { return _focusKey; }
    void moveFocus(int dx, int dy);
    /// Press the key under the cursor for a tap (a modifier latches, as a click); false without one
    bool tapFocus();
    /// The keys the window holds: the one under the button and the latched modifiers
    std::set<std::string> held() const;
    /// The keys drawn lit: the window's own, and "host:<id>" for the host keyboard's
    const std::set<std::string>& lit() const { return _lit; }
    /// The legend roles drawn faint now (Symbol Shift held: every mode legend but the symbols)
    const std::set<std::string>& dimmed() const { return _view->dimmed(); }
    /// The line under the picture
    QString notice() const;
    /// Follow the bound machine: its model, the route, the host keys held
    void refresh();
    /// Follow the bound machine's model only (its pictures and the one chosen), for a report while hidden
    void followModel();

signals:
    void visibilityChanged(bool visible);
    /// Another picture is shown (chosen, or the machine's model changed)
    void pictureChanged();
    /// The overlay looks different now (shown, hidden, keys lit, the cursor moved)
    void overlayChanged();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    EmulatorContext* context() const;
    std::string modelName() const;
    void loadModel(const std::string& model);
    void loadIndex();
    void onKeyPressed(const PictureKey* key);
    /// Where the overlay goes on a screen of @p width x @p height
    QRectF overlayTarget(int width, int height) const;
    void post(const PictureKey& key, bool pressed);
    void releaseAll();
    void updateNotice();
    /// The overlay is out of date: draw it again
    void overlayDirty();

    EmulatorBinding* _binding = nullptr;
    QString _root;
    bool _overlay = false;
    bool _overlayShown = false;
    /// The finger's press on the overlay: when, and which press (a later one cancels an earlier deferred release)
    QElapsedTimer _pointerDown;
    unsigned _pointerPress = 0;
    QImage _overlayImage;  ///< the picture with its lit keys at the overlay's width; null: draw it again
    KeyboardPictures _pictures;
    bool _indexLoaded = false;
    std::string _model;
    std::string _style;
    std::vector<std::string> _choices;
    KeyboardPicture _picture;
    const PictureKey* _down = nullptr;
    std::vector<const PictureKey*> _latched;
    QString _keyNotice;  ///< why the last key pressed nothing
    std::set<std::string> _lit;
    std::string _target;  ///< the machine the held keys went to (the releases go there too)
    std::string _focusKey;
    std::string _forced;
    /// The forced keyboard on the picture, when the matrix allows it
    void applyForced();

    OsdKeyboardView* _view = nullptr;
    QComboBox* _switch = nullptr;
    QComboBox* _styleSwitch = nullptr;
    QLabel* _notice = nullptr;
    QTimer* _timer = nullptr;
};
