#pragma once

#include <QListWidget>
#include <QJsonArray>

class EmulatorList : public QListWidget
{
    Q_OBJECT

public:
    explicit EmulatorList(QWidget* parent = nullptr);
    ~EmulatorList() override = default;

public slots:
    void updateEmulatorList(const QJsonArray& emulators);

signals:
    void emulatorSelected(const QString& emulatorId);
    void emulatorDeselected();

private slots:
    void onSelectionChanged();

private:
    void addEmulatorItem(const QString& id, const QString& model,
                         const QString& state, bool shmActive);

    QString _lastSelectedId;
};
