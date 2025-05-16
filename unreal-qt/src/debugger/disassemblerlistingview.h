#ifndef DISASSEMBLERLISTINGVIEW_H
#define DISASSEMBLERLISTINGVIEW_H

#include <QSplitter>

#include "debugger/disassembler/z80disasm.h"

class DisassemblerColumnView;
class DisassemblerTextView;

class DisassemblerListingView : public QSplitter
{
public:
    explicit DisassemblerListingView(QWidget *parent = nullptr);
    DisassemblerColumnView* columnView();
    DisassemblerTextView* textView();
    //void setDisassembler(const REDasm::DisassemblerPtr &disassembler);

private slots:
    void renderArrows();

private:
    Z80Disassembler* m_disassembler;
    DisassemblerColumnView* m_disassemblercolumnview;
    DisassemblerTextView* m_disassemblertextview;
};

#endif // DISASSEMBLERLISTINGVIEW_H
