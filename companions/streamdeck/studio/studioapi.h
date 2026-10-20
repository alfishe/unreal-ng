// The studio's automation planes (WebAPI + MCP on one port, 37052 by default): everything the window offers a
// person, plus the editor's routes (editor/editorapi.h).
#pragma once
#include <QImage>
#include <QString>

#include "sdb/automation.h"

class MainWindow;

class StudioApi
{
public:
    explicit StudioApi(MainWindow* w);
    ~StudioApi();
    StudioApi(const StudioApi&) = delete;
    StudioApi& operator=(const StudioApi&) = delete;
    // Routes of other modules (the editor) are added before Start.
    sdb::automation::Api& GetApi()
    {
        return _api;
    }
    bool Start(int port, QString* error);

    // PNG bytes of a QImage
    [[nodiscard]] static std::string Png(const QImage& img);

private:
    void AddDeviceRoutes();
    void AddInputRoutes();
    void AddViewRoutes();

    MainWindow* _w;
    sdb::automation::Api _api;
    sdb::automation::Server _server;
};
