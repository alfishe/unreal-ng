#pragma once

#include <QWidget>

class LabelManager;
class QLabel;
class QPushButton;
class QTableWidget;

/// The label editor's "Sets" tab: the symbol sets behind the labels (user, one per loaded file, bundles, live scans,
/// named sets) with their switch and priority; drop a set. Every change goes through SymbolControl, as on the other
/// surfaces (.recipe/analysis/symbols-import-export.md)
class SymbolSetsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SymbolSetsPanel(LabelManager* labelManager, QWidget* parent = nullptr);

public slots:
    void refresh();

signals:
    /// A set was switched, moved or dropped: the labels changed
    void setsChanged();

private slots:
    void onItemChanged(int row, int column);
    void dropSelected();

private:
    LabelManager* _labelManager = nullptr;
    QTableWidget* _table = nullptr;
    QPushButton* _dropButton = nullptr;
    QLabel* _summary = nullptr;
    bool _filling = false;
};
