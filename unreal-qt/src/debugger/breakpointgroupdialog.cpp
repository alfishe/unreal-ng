#include "breakpointgroupdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>

BreakpointGroupDialog::BreakpointGroupDialog(Emulator* emulator, QWidget* parent)
    : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Manage Breakpoint Groups");
    setupUI();
    populateGroupList();
    onGroupSelectionChanged();
}

BreakpointGroupDialog::~BreakpointGroupDialog()
{
}

void BreakpointGroupDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Group list
    _groupList = new QListWidget();
    _groupList->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(_groupList);
    
    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    
    _addButton = new QPushButton("Add");
    _renameButton = new QPushButton("Rename");
    _deleteButton = new QPushButton("Delete");
    
    buttonLayout->addWidget(_addButton);
    buttonLayout->addWidget(_renameButton);
    buttonLayout->addWidget(_deleteButton);
    buttonLayout->addStretch();
    
    mainLayout->addLayout(buttonLayout);
    
    // Status label
    _statusLabel = new QLabel();
    mainLayout->addWidget(_statusLabel);
    
    // Close button
    QHBoxLayout* closeLayout = new QHBoxLayout();
    _closeButton = new QPushButton("Close");
    closeLayout->addStretch();
    closeLayout->addWidget(_closeButton);
    
    mainLayout->addLayout(closeLayout);
    
    // Connect signals
    connect(_groupList, &QListWidget::itemSelectionChanged, this, &BreakpointGroupDialog::onGroupSelectionChanged);
    connect(_addButton, &QPushButton::clicked, this, &BreakpointGroupDialog::addGroup);
    connect(_renameButton, &QPushButton::clicked, this, &BreakpointGroupDialog::renameGroup);
    connect(_deleteButton, &QPushButton::clicked, this, &BreakpointGroupDialog::deleteGroup);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    
    // Set initial size
    resize(300, 400);
}

void BreakpointGroupDialog::populateGroupList()
{
    _groupList->clear();
    
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
        return;
        
    std::vector<std::string> groups = bpManager->GetBreakpointGroups();
    for (const auto& group : groups)
    {
        QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(group));
        
        // Get breakpoint count for this group
        std::vector<uint16_t> breakpoints = bpManager->GetBreakpointsByGroup(group);
        item->setData(Qt::UserRole, static_cast<int>(breakpoints.size()));
        
        // Display group name with breakpoint count
        item->setText(QString("%1 (%2)").arg(QString::fromStdString(group)).arg(breakpoints.size()));
        
        _groupList->addItem(item);
    }
    
    updateStatusBar();
}

void BreakpointGroupDialog::addGroup()
{
    bool ok;
    QString groupName = QInputDialog::getText(this, "Add Group", 
                                             "Enter new group name:", 
                                             QLineEdit::Normal, 
                                             "", &ok);
    if (ok && !groupName.isEmpty())
    {
        // Check if group already exists
        for (int i = 0; i < _groupList->count(); ++i)
        {
            QListWidgetItem* item = _groupList->item(i);
            QString existingName = item->text().split(" (").first();
            if (existingName == groupName)
            {
                QMessageBox::warning(this, "Duplicate Group", 
                                    "A group with this name already exists.");
                return;
            }
        }
        
        // Add new group (by creating a dummy breakpoint in this group)
        BreakpointManager* bpManager = _emulator->GetBreakpointManager();
        if (bpManager)
        {
            // We need to create a temporary breakpoint to establish the group
            uint16_t tempId = bpManager->AddExecutionBreakpoint(0);
            if (tempId != BRK_INVALID)
            {
                bpManager->SetBreakpointGroup(tempId, groupName.toStdString());
                
                // Now remove the temporary breakpoint
                bpManager->RemoveBreakpointByID(tempId);
                
                // Refresh the list
                populateGroupList();
            }
        }
    }
}

void BreakpointGroupDialog::renameGroup()
{
    QListWidgetItem* currentItem = _groupList->currentItem();
    if (!currentItem)
        return;
        
    QString oldName = currentItem->text().split(" (").first();
    
    // Don't allow renaming the default group
    if (oldName == "default")
    {
        QMessageBox::warning(this, "Cannot Rename", 
                            "The 'default' group cannot be renamed.");
        return;
    }
    
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Group", 
                                           "Enter new name for group:", 
                                           QLineEdit::Normal, 
                                           oldName, &ok);
    if (ok && !newName.isEmpty() && newName != oldName)
    {
        // Check if new name already exists
        for (int i = 0; i < _groupList->count(); ++i)
        {
            QListWidgetItem* item = _groupList->item(i);
            QString existingName = item->text().split(" (").first();
            if (existingName == newName)
            {
                QMessageBox::warning(this, "Duplicate Group", 
                                    "A group with this name already exists.");
                return;
            }
        }
        
        // Rename group (by reassigning all breakpoints in this group)
        BreakpointManager* bpManager = _emulator->GetBreakpointManager();
        if (bpManager)
        {
            std::vector<uint16_t> breakpoints = bpManager->GetBreakpointsByGroup(oldName.toStdString());
            for (uint16_t id : breakpoints)
            {
                bpManager->SetBreakpointGroup(id, newName.toStdString());
            }
            
            // Refresh the list
            populateGroupList();
        }
    }
}

void BreakpointGroupDialog::deleteGroup()
{
    QListWidgetItem* currentItem = _groupList->currentItem();
    if (!currentItem)
        return;
        
    QString groupName = currentItem->text().split(" (").first();
    
    // Don't allow deleting the default group
    if (groupName == "default")
    {
        QMessageBox::warning(this, "Cannot Delete", 
                            "The 'default' group cannot be deleted.");
        return;
    }
    
    int breakpointCount = currentItem->data(Qt::UserRole).toInt();
    
    QMessageBox::StandardButton reply;
    if (breakpointCount > 0)
    {
        reply = QMessageBox::question(this, "Confirm Delete", 
                                     QString("Group '%1' contains %2 breakpoints. These will be moved to the 'default' group. Continue?")
                                     .arg(groupName).arg(breakpointCount),
                                     QMessageBox::Yes | QMessageBox::No);
    }
    else
    {
        reply = QMessageBox::question(this, "Confirm Delete", 
                                     QString("Delete group '%1'?").arg(groupName),
                                     QMessageBox::Yes | QMessageBox::No);
    }
    
    if (reply == QMessageBox::Yes)
    {
        BreakpointManager* bpManager = _emulator->GetBreakpointManager();
        if (bpManager)
        {
            bpManager->RemoveBreakpointGroup(groupName.toStdString());
            
            // Refresh the list
            populateGroupList();
        }
    }
}

void BreakpointGroupDialog::onGroupSelectionChanged()
{
    bool hasSelection = (_groupList->currentItem() != nullptr);
    
    _renameButton->setEnabled(hasSelection);
    _deleteButton->setEnabled(hasSelection);
    
    updateStatusBar();
}

void BreakpointGroupDialog::updateStatusBar()
{
    int totalGroups = _groupList->count();
    _statusLabel->setText(QString("Total groups: %1").arg(totalGroups));
}
