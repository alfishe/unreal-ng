#ifndef DISASSEMBLERVIEW_H
#define DISASSEMBLERVIEW_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class DisassemblerView; }
QT_END_NAMESPACE

class DisassemblerListingView;

class DisassemblerView : public QWidget
{
    Q_OBJECT
public:
    explicit DisassemblerView(QWidget *parent = nullptr);

signals:

    // UI fields
private:
    Ui::DisassemblerView* ui;

    DisassemblerListingView* m_listingview;

};

#endif // DISASSEMBLERVIEW_H
