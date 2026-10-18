#pragma once

/// @file controllersdialog.h
/// @brief Tools > Controllers (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §2): host devices on the left,
/// the selected device's controls with their live value and binding in the centre (a press selects the control:
/// press-to-bind), the machine's connectors with their retro device on the right, the applied profiles on top.
/// Above the controls, the device's picture with a callout per control (ControllerPicture, C5).

#include <QWidget>

#include <string>

class ControllerPicture;
class HostControllers;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;

class ControllersDialog : public QWidget
{
    Q_OBJECT

public:
    explicit ControllersDialog(HostControllers* host, QWidget* parent = nullptr);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void rebuild();
    void rebuildControls();
    void rebuildConnectors();
    void tick();
    void bindSelected();
    void plug(const std::string& connector, const QString& device);
    std::string selectedDevice() const;
    std::string selectedFamily() const;
    std::string selectedLayout() const;
    void selectControl(const QString& control);

    HostControllers* _host = nullptr;
    QLabel* _profileLabel = nullptr;
    QComboBox* _gameProfile = nullptr;  ///< ProfileContext::manual: by the loaded file, or one chosen here
    QCheckBox* _advanced = nullptr;
    QCheckBox* _soundHaptics = nullptr;
    QListWidget* _devices = nullptr;
    QLabel* _probeLabel = nullptr;
    QLabel* _deviceTitle = nullptr;
    ControllerPicture* _picture = nullptr;
    QString _pictureSet;
    QTableWidget* _controls = nullptr;
    QPushButton* _bindButton = nullptr;
    QPushButton* _identifyButton = nullptr;  ///< a 300 ms rumble on the selected device (ui-ux.md)
    QTableWidget* _connectors = nullptr;
    QLabel* _machineLabel = nullptr;
    QLabel* _hapticsLabel = nullptr;  ///< haptics.md §7: the live meters of the feed
    QTimer* _live = nullptr;
    uint64_t _machineRevision = ~0ull;
    bool _rebuildingConnectors = false;
};
