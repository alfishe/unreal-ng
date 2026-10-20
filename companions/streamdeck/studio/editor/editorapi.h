// Automation routes of the scenario editor (registered on the studio's WebAPI / MCP).
#pragma once
#include <functional>

#include "sdb/automation.h"

class EditorWidget;
class QWidget;

// `host` = the window the editor lives in (dock or floating), for window screenshots.
void AddEditorRoutes(sdb::automation::Api& api, EditorWidget* editor, std::function<QWidget*()> host);
