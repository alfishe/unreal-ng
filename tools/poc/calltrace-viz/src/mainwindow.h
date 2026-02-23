#pragma once

#include <QCheckBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QString>

#include "memory16kbwidget.h"
#include "syntheticdata.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void applyTheme(bool dark);
    void buildControlPanel(QWidget* parent);
    void buildLegend(QWidget* parent);

    void loadDataFile();

    SyntheticData _data;
    Memory16KBWidget* _bankWidgets[4] = {};

    // Overlay controls
    QCheckBox* _cbRead = nullptr;
    QCheckBox* _cbWrite = nullptr;
    QCheckBox* _cbExecute = nullptr;
    QCheckBox* _cbOpcodeTrace = nullptr;
    QCheckBox* _cbEntropy = nullptr;
    QCheckBox* _cbFreshness = nullptr;
    QCheckBox* _cbRegion = nullptr;
    QCheckBox* _cbHideValues = nullptr;
    QCheckBox* _cbDarkTheme = nullptr;
    QPushButton* _btnLoadData = nullptr;
    QLabel* _lblDataSource = nullptr;

    // CF overlay — individual checkboxes (from Python prototype)
    QCheckBox* _cbCFHeatmap = nullptr;
    QCheckBox* _cbCFSources = nullptr;
    QCheckBox* _cbCFTargets = nullptr;
    QCheckBox* _cbCFArcs = nullptr;

    // Glow radius slider
    QSlider* _glowSlider = nullptr;
    QLabel* _glowValueLabel = nullptr;

    bool _darkTheme = true;

    // Legend color swatches — updated by applyTheme() on every theme switch
    QLabel* _legendSwatches[7] = {};
};
