#include "disassemblerlistingview.h"

#include <QScrollBar>

#include "disassemblercolumnview.h"
#include "disassemblertextview.h"

DisassemblerListingView::DisassemblerListingView(QWidget *parent): QSplitter(parent), m_disassembler(nullptr)
{
    this->setOrientation(Qt::Horizontal);
    this->setStyleSheet("QSplitter::handle { background-color: gray; }");

    m_disassemblertextview = new DisassemblerTextView(this);
    m_disassemblercolumnview = new DisassemblerColumnView(this);

    //m_disassemblertextview->setFont(REDasmSettings::font());
    //m_disassemblercolumnview->setFont(m_disassemblertextview->font()); // Apply same font

    connect(m_disassemblertextview->verticalScrollBar(), &QScrollBar::valueChanged, this, [&](int) { this->renderArrows(); });

    this->addWidget(m_disassemblercolumnview);
    this->addWidget(m_disassemblertextview);

    this->setStretchFactor(0, 2);
    this->setStretchFactor(1, 10);
    this->setHandleWidth(4);
}

void DisassemblerListingView::renderArrows()
{
    //if (m_disassembler->busy())
    //    return;

    m_disassemblercolumnview->renderArrows(m_disassemblertextview->firstVisibleLine(), m_disassemblertextview->visibleLines());
}
