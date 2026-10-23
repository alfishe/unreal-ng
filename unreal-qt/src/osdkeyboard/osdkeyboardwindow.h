#pragma once

/// @file osdkeyboardwindow.h
/// @brief The on-screen keyboard (controller manager `osd.keyboard`, POC 031): the bound machine's own keyboard in a
/// window of its own that never takes the focus from the screen. A click presses a key while the button is held; a
/// modifier (CS, SS, PC Shift / Ctrl / Alt / Win) latches until the next key goes up, a second click lets it go. The
/// machines with more than one keyboard (the PC clones: their PC keyboard and the 40-key ZX matrix; Pentagon,
/// Profi) get a switch, the choice kept per model. A ZX picture presses the matrix only, a PC picture the machine's
/// PC keyboard controller only (MC_KEY_* / MC_PCKEY_*, as the host keyboard's two halves); the line under the
/// picture says when the Host Keyboard route keeps them from the machine. Keys held on the host keyboard light too.
/// A second switch draws every picture in another style (photo, schematic, hybrid with the hints), kept for all models.

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

    const KeyboardPicture* _picture = nullptr;
    QSvgRenderer* _svg = nullptr;
    QPixmap _cache;  ///< the picture at the widget's size: the SVGs are large, a repaint draws only the lit keys
    QImage _recordBase;  ///< the picture at the recording's width, without lit keys
    std::set<std::string> _lit;
};

class OsdKeyboardWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OsdKeyboardWindow(QWidget* parent = nullptr);
    ~OsdKeyboardWindow() override;

    void setBinding(EmulatorBinding* binding);
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
    bool chooseOther(const std::string& id, std::string& error);
    /// The picture with its lit keys, @p width pixels wide; null without a picture (the window recording)
    QImage renderImage(int width) { return _view->renderImage(width); }
    /// Press / let go of a key by its id, as a click does (tests)
    void press(const std::string& id);
    void release();
    /// The keys the window holds: the one under the button and the latched modifiers
    std::set<std::string> held() const;
    /// The keys drawn lit: the window's own, and "host:<id>" for the host keyboard's
    const std::set<std::string>& lit() const { return _lit; }
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

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    EmulatorContext* context() const;
    std::string modelName() const;
    void loadModel(const std::string& model);
    void loadIndex();
    void onKeyPressed(const PictureKey* key);
    void post(const PictureKey& key, bool pressed);
    void releaseAll();
    void updateNotice();

    EmulatorBinding* _binding = nullptr;
    QString _root;
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

    OsdKeyboardView* _view = nullptr;
    QComboBox* _switch = nullptr;
    QComboBox* _styleSwitch = nullptr;
    QLabel* _notice = nullptr;
    QTimer* _timer = nullptr;
};
