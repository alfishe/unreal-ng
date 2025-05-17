#include "disassemblerview.h"

#include "ui_disassemblerview.h"

DisassemblerView::DisassemblerView(QWidget *parent) : QWidget(parent), ui(new Ui::DisassemblerView)
{
    // Instantiate all child widgets (UI form auto-generated)
    ui->setupUi(this);
}
