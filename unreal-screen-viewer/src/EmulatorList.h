#pragma once

#include <QListWidget>
#include <QJsonArray>

/**
 * @brief Widget displaying list of discovered emulator instances
 * 
 * Shows emulator ID, model, and state. Supports selection
 * and auto-selection when only one emulator is available.
 */
class EmulatorList : public QListWidget
{
    Q_OBJECT

public:
    explicit EmulatorList(QWidget* parent = nullptr);
    ~EmulatorList() override = default;

public slots:
    /// Update the list with emulators from WebAPI response
    void updateEmulatorList(const QJsonArray& emulators);

signals:
    /// Emitted when an emulator is selected
    void emulatorSelected(const QString& emulatorId);
    
    /// Emitted when selection is cleared
    void emulatorDeselected();

private slots:
    void onSelectionChanged();

private:
    void addEmulatorItem(const QString& id, const QString& model, const QString& state, bool shmActive);
    
    QString _lastSelectedId;
};
