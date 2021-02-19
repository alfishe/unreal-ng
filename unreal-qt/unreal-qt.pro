QT += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
QMAKE_CXXFLAGS += -std=c++17

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    3rdparty/qhexview/document/buffer/qfilebuffer.cpp \
    3rdparty/qhexview/document/buffer/qhexbuffer.cpp \
    3rdparty/qhexview/document/buffer/qmemorybuffer.cpp \
    3rdparty/qhexview/document/buffer/qmemoryrefbuffer.cpp \
    3rdparty/qhexview/document/commands/hexcommand.cpp \
    3rdparty/qhexview/document/commands/insertcommand.cpp \
    3rdparty/qhexview/document/commands/removecommand.cpp \
    3rdparty/qhexview/document/commands/replacecommand.cpp \
    3rdparty/qhexview/document/qhexcursor.cpp \
    3rdparty/qhexview/document/qhexdocument.cpp \
    3rdparty/qhexview/document/qhexmetadata.cpp \
    3rdparty/qhexview/document/qhexrenderer.cpp \
    3rdparty/qhexview/qhexview.cpp \
    common/widgets/qclickablelabel.cpp \
    debugger/debuggerwindow.cpp \
    debugger/disassemblercolumnview.cpp \
    debugger/disassemblerlistingview.cpp \
    debugger/disassemblertextview.cpp \
    debugger/disassemblerview.cpp \
    debugger/disassemblerwidget.cpp \
    debugger/memorypageswidget.cpp \
    debugger/registerswidget.cpp \
    debugger/stackwidget.cpp \
    emulator/emulatormanager.cpp \
    emulator/filemanager.cpp \
    emulator/guiemulatorcontext.cpp \
    emulator/keyboardmanager.cpp \
    emulator/soundmanager.cpp \
    logviewer/logstatusbar.cpp \
    logviewer/logviewer.cpp \
    logviewer/logwindow.cpp \
    main.cpp \
    mainwindow.cpp \
    widgets/devicescreen.cpp \
    widgets/verticallabel.cpp

HEADERS += \
    3rdparty/qhexview/document/buffer/qfilebuffer.h \
    3rdparty/qhexview/document/buffer/qhexbuffer.h \
    3rdparty/qhexview/document/buffer/qmemorybuffer.h \
    3rdparty/qhexview/document/buffer/qmemoryrefbuffer.h \
    3rdparty/qhexview/document/commands/hexcommand.h \
    3rdparty/qhexview/document/commands/insertcommand.h \
    3rdparty/qhexview/document/commands/removecommand.h \
    3rdparty/qhexview/document/commands/replacecommand.h \
    3rdparty/qhexview/document/qhexcursor.h \
    3rdparty/qhexview/document/qhexdocument.h \
    3rdparty/qhexview/document/qhexmetadata.h \
    3rdparty/qhexview/document/qhexrenderer.h \
    3rdparty/qhexview/qhexview.h \
    common/widgets/qclickablelabel.h \
    debugger/debuggerwindow.h \
    debugger/disassemblercolumnview.h \
    debugger/disassemblerlistingview.h \
    debugger/disassemblertextview.h \
    debugger/disassemblerview.h \
    debugger/disassemblerwidget.h \
    debugger/memorypageswidget.h \
    debugger/registerswidget.h \
    debugger/stackwidget.h \
    emulator/emulatormanager.h \
    emulator/filemanager.h \
    emulator/guiemulatorcontext.h \
    emulator/keyboardmanager.h \
    emulator/soundmanager.h \
    logviewer/logstatusbar.h \
    logviewer/logviewer.h \
    logviewer/logwindow.h \
    mainwindow.h \
    widgets/devicescreen.h \
    widgets/verticallabel.h

FORMS += \
    mainwindow.ui \
    ui/debuggerwindow.ui \
    ui/devicescreen.ui \
    ui/disassemblerview.ui \
    ui/hexview.ui \
    ui/logstatusbar.ui \
    ui/logwindow.ui \
    ui/memoryblock.ui \
    ui/memorymap.ui \
    ui/ula.ui \
    ui/widgets/disassemblerwidget.ui \
    ui/widgets/memorypageswidget.ui \
    ui/widgets/registerswidget.ui \
    ui/widgets/stackwidget.ui

TRANSLATIONS += \
    unreal-qt_en_US.ts

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../cmake-build-release/core/src/ -lcore -lshlwapi
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../cmake-build-debug/core/src/ -lcore -lshlwapi
else:unix:!macx: LIBS += -L$$PWD/../cmake-build-debug/core/src/ -lcore
else:macx: LIBS += -L$$PWD/../cmake-build-debug/core/src/ -lcore -liconv

INCLUDEPATH += $$PWD/../core/src
DEPENDPATH += $$PWD/../core/src


win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-release/core/src/libcore.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/libcore.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/release/core.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/debug/core.lib
else:unix: PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/libcore.a

# Extra target to copy .ini and rom files


unix:!macx {
    CONFIG_FILE = $$PWD/../data/configs/spectrum128/unreal.ini
    ROM_PATH = $$PWD/../data/rom/

    COPY_CONFIG_COMMAND = $$quote(cp $$CONFIG_FILE $$OUT_PWD$$escape_expand(\n\t))
    COPY_ROM_COMMAND = $$quote(cp -R $$ROM_PATH $$OUT_PWD$$/rom/$$escape_expand(\n\t))
    message($$COPY_CONFIG_COMMAND)
    message($$COPY_ROM_COMMAND)

    QMAKE_POST_LINK += && $$COPY_CONFIG_COMMAND
    QMAKE_POST_LINK += && $$COPY_ROM_COMMAND
}

macx: {
    CONFIG_FILE = $$PWD/../data/configs/spectrum128/unreal.ini
    ROM_PATH = $$PWD/../data/rom/
    MACOS_PACKAGE_CONTENTS_PATH = $$OUT_PWD/unreal-qt.app/Contents/MacOS

    # Normalize paths (use system delimiters + convert paths to absolute)
    CONFIG_FILE_NORMALIZED = $$absolute_path($$system_path($$CONFIG_FILE))
    ROM_PATH_NORMALIZED = $$absolute_path($$system_path($$ROM_PATH))
    MACOS_PACKAGE_CONTENTS_PATH_NORMALIZED = $$absolute_path($$system_path($$MACOS_PACKAGE_CONTENTS_PATH))

    COPY_CONFIG_COMMAND = $$quote(cp $$CONFIG_FILE_NORMALIZED $$MACOS_PACKAGE_CONTENTS_PATH_NORMALIZED $$escape_expand(\n\t))
    COPY_ROM_COMMAND = $$quote(cp -R $$ROM_PATH_NORMALIZED $$MACOS_PACKAGE_CONTENTS_PATH_NORMALIZED$$/rom/ $$escape_expand(\n\t))
    message($$COPY_CONFIG_COMMAND)
    message($$COPY_ROM_COMMAND)

    QMAKE_POST_LINK += && $$COPY_CONFIG_COMMAND
    QMAKE_POST_LINK += && $$COPY_ROM_COMMAND
}

win32: {
    CONFIG_FILE = $$PWD\..\data\configs\spectrum128\unreal.ini
    QMAKE_POST_LINK += $$quote(cmd /c copy /y $$CONFIG_FILE $$OUT_PWD$$escape_expand(\n\t))
}

