#include "logstatusbar.h"
#include "ui_logstatusbar.h"

LogStatusBar::LogStatusBar(QWidget *parent) : QWidget(parent), ui(new Ui::LogStatusBar)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);

    labelStatusText = ui->labelStatusText;
    labelCounter1 = ui->labelCounter1;
    labelCounter2 = ui->labelCounter2;
}

LogStatusBar::~LogStatusBar()
{
    delete ui;
}
