#include "ModeToolbar.h"

#include <QHBoxLayout>
#include <QButtonGroup>

ModeToolbar::ModeToolbar(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);

    // Single mode button [1▢]
    _singleButton = new QToolButton(this);
    _singleButton->setText("1");
    _singleButton->setToolTip(tr("Single screen mode"));
    _singleButton->setCheckable(true);
    _singleButton->setChecked(true);
    _singleButton->setFixedSize(28, 24);
    connect(_singleButton, &QToolButton::clicked, this, &ModeToolbar::onSingleClicked);

    // Dual mode button [▢▢]
    _dualButton = new QToolButton(this);
    _dualButton->setText("2");
    _dualButton->setToolTip(tr("Dual screen mode"));
    _dualButton->setCheckable(true);
    _dualButton->setFixedSize(28, 24);
    connect(_dualButton, &QToolButton::clicked, this, &ModeToolbar::onDualClicked);

    // Spacer
    auto* spacer = new QWidget(this);
    spacer->setFixedWidth(8);

    // Horizontal layout button ═
    _horizontalButton = new QToolButton(this);
    _horizontalButton->setText("═");
    _horizontalButton->setToolTip(tr("Horizontal layout (side-by-side)"));
    _horizontalButton->setCheckable(true);
    _horizontalButton->setChecked(true);
    _horizontalButton->setFixedSize(24, 24);
    _horizontalButton->setVisible(false);  // Hidden in single mode
    connect(_horizontalButton, &QToolButton::clicked, this, &ModeToolbar::onHorizontalClicked);

    // Vertical layout button ║
    _verticalButton = new QToolButton(this);
    _verticalButton->setText("║");
    _verticalButton->setToolTip(tr("Vertical layout (stacked)"));
    _verticalButton->setCheckable(true);
    _verticalButton->setFixedSize(24, 24);
    _verticalButton->setVisible(false);  // Hidden in single mode
    connect(_verticalButton, &QToolButton::clicked, this, &ModeToolbar::onVerticalClicked);

    layout->addWidget(_singleButton);
    layout->addWidget(_dualButton);
    layout->addWidget(spacer);
    layout->addWidget(_horizontalButton);
    layout->addWidget(_verticalButton);
    layout->addStretch();

    setLayout(layout);
}

void ModeToolbar::setViewMode(ViewMode mode)
{
    if (_viewMode != mode)
    {
        _viewMode = mode;
        updateButtonStates();
        emit viewModeChanged(mode);
    }
}

void ModeToolbar::setDualLayout(DualLayout layout)
{
    if (_dualLayout != layout)
    {
        _dualLayout = layout;
        updateButtonStates();
        emit dualLayoutChanged(layout);
    }
}

void ModeToolbar::onSingleClicked()
{
    setViewMode(ViewMode::Single);
}

void ModeToolbar::onDualClicked()
{
    setViewMode(ViewMode::Dual);
}

void ModeToolbar::onHorizontalClicked()
{
    setDualLayout(DualLayout::Horizontal);
}

void ModeToolbar::onVerticalClicked()
{
    setDualLayout(DualLayout::Vertical);
}

void ModeToolbar::updateButtonStates()
{
    // Update mode button checked states
    _singleButton->setChecked(_viewMode == ViewMode::Single);
    _dualButton->setChecked(_viewMode == ViewMode::Dual);

    // Show/hide layout buttons based on mode
    bool isDual = (_viewMode == ViewMode::Dual);
    _horizontalButton->setVisible(isDual);
    _verticalButton->setVisible(isDual);

    // Update layout button checked states
    _horizontalButton->setChecked(_dualLayout == DualLayout::Horizontal);
    _verticalButton->setChecked(_dualLayout == DualLayout::Vertical);
}
