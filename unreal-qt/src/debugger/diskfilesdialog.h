#pragma once

#include <QDialog>

class Emulator;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// The files of the disk in a drive (unreal-asm A7): each with the source format detected; a source opens as text,
/// exports as text or converts to another dialect (sjasmplus, pasmo, z88dk ...). Everything goes through AsmControl,
/// as on the other surfaces (.recipe/analysis/asm-sources.md)
class DiskFilesDialog : public QDialog
{
    Q_OBJECT

public:
    DiskFilesDialog(Emulator* emulator, QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void openSource();
    void exportText();
    void convertTo();
    void importLabels();
    void updateButtons();

private:
    /// The disk:X/NAME.T path of the selected file ("" when none)
    QString selectedPath() const;

    Emulator* _emulator;
    QComboBox* _drive = nullptr;
    QTableWidget* _table = nullptr;
    QPushButton* _open = nullptr;
    QPushButton* _export = nullptr;
    QPushButton* _convert = nullptr;
    QPushButton* _labels = nullptr;
    QLabel* _status = nullptr;
};
