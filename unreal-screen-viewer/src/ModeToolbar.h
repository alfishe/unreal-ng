#pragma once

#include <QWidget>
#include <QToolButton>

/**
 * @brief Compact toolbar for switching between single/dual screen modes
 * 
 * Positioned at the bottom of the emulator list panel.
 * Provides:
 * - Single/Dual mode toggle buttons ([1▢] [▢▢])
 * - Horizontal/Vertical layout buttons (═ ║) - visible in dual mode only
 */
class ModeToolbar : public QWidget
{
    Q_OBJECT

public:
    enum class ViewMode { Single, Dual };
    enum class DualLayout { Horizontal, Vertical };

    explicit ModeToolbar(QWidget* parent = nullptr);
    ~ModeToolbar() override = default;

    ViewMode viewMode() const { return _viewMode; }
    DualLayout dualLayout() const { return _dualLayout; }

public slots:
    void setViewMode(ViewMode mode);
    void setDualLayout(DualLayout layout);

signals:
    void viewModeChanged(ViewMode mode);
    void dualLayoutChanged(DualLayout layout);

private slots:
    void onSingleClicked();
    void onDualClicked();
    void onHorizontalClicked();
    void onVerticalClicked();

private:
    void updateButtonStates();

    QToolButton* _singleButton = nullptr;
    QToolButton* _dualButton = nullptr;
    QToolButton* _horizontalButton = nullptr;
    QToolButton* _verticalButton = nullptr;

    ViewMode _viewMode = ViewMode::Single;
    DualLayout _dualLayout = DualLayout::Horizontal;
};
