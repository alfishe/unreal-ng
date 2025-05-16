QT += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# Qt5 compatibility mode for Qt6
greaterThan(QT_MAJOR_VERSION, 6): QT += core5compat

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
    3rdparty/QHexView-5.0/src/model/buffer/qfilebuffer.cpp \
    3rdparty/QHexView-5.0/src/model/buffer/qhexbuffer.cpp \
    3rdparty/QHexView-5.0/src/model/buffer/qmemorybuffer.cpp \
    3rdparty/QHexView-5.0/src/model/buffer/qmemoryrefbuffer.cpp \
    3rdparty/QHexView-5.0/src/model/commands/hexcommand.cpp \
    3rdparty/QHexView-5.0/src/model/commands/insertcommand.cpp \
    3rdparty/QHexView-5.0/src/model/commands/removecommand.cpp \
    3rdparty/QHexView-5.0/src/model/commands/replacecommand.cpp \
    3rdparty/QHexView-5.0/src/model/qhexcursor.cpp \
    3rdparty/QHexView-5.0/src/model/qhexdocument.cpp \
    3rdparty/QHexView-5.0/src/model/qhexmetadata.cpp \
    3rdparty/QHexView-5.0/src/model/qhexoptions.cpp \
    3rdparty/QHexView-5.0/src/model/qhexutils.cpp \
    3rdparty/QHexView-5.0/src/model/qhexdelegate.cpp \
    3rdparty/QHexView-5.0/src/qhexview.cpp
    src/common/widgets/qclickablelabel.cpp \
    src/debugger/debuggerwindow.cpp \
    src/debugger/disassemblercolumnview.cpp \
    src/debugger/disassemblerlistingview.cpp \
    src/debugger/disassemblertextview.cpp \
    src/debugger/disassemblerview.cpp \
    src/debugger/disassemblerwidget.cpp \
    src/debugger/memorypageswidget.cpp \
    src/debugger/registerswidget.cpp \
    src/debugger/stackwidget.cpp \
    src/emulator/emulatormanager.cpp \
    src/emulator/filemanager.cpp \
    src/emulator/guiemulatorcontext.cpp \
    src/emulator/keyboardmanager.cpp \
    src/emulator/soundmanager.cpp \
    src/logviewer/logstatusbar.cpp \
    src/logviewer/logviewer.cpp \
    src/logviewer/logwindow.cpp \
    src/main.cpp \
    src/mainwindow.cpp \
    src/widgets/devicescreen.cpp \
    src/widgets/verticallabel.cpp

HEADERS += \
    3rdparty/QHexView-5.0/include/QHexView/model/buffer/qfilebuffer.h \
    3rdparty/QHexView-5.0/include/QHexView/model/buffer/qhexbuffer.h \
    3rdparty/QHexView-5.0/include/QHexView/model/buffer/qmemorybuffer.h \
    3rdparty/QHexView-5.0/include/QHexView/model/buffer/qmemoryrefbuffer.h \
    3rdparty/QHexView-5.0/include/QHexView/model/commands/hexcommand.h \
    3rdparty/QHexView-5.0/include/QHexView/model/commands/insertcommand.h \
    3rdparty/QHexView-5.0/include/QHexView/model/commands/removecommand.h \
    3rdparty/QHexView-5.0/include/QHexView/model/commands/replacecommand.h \
    3rdparty/QHexView-5.0/include/QHexView/model/qhexcursor.h \
    3rdparty/QHexView-5.0/include/QHexView/model/qhexdocument.h \
    3rdparty/QHexView-5.0/include/QHexView/model/qhexmetadata.h \
    3rdparty/QHexView-5.0/include/QHexView/model/qhexoptions.h \
    3rdparty/QHexView-5.0/include/QHexView/model/qhexutils.h \
    3rdparty/QHexView-5.0/include/QHexView/model/qhexdelegate.h \
    3rdparty/QHexView-5.0/include/QHexView/qhexview.h
    src/common/widgets/qclickablelabel.h \
    src/debugger/debuggerwindow.h \
    src/debugger/disassemblercolumnview.h \
    src/debugger/disassemblerlistingview.h \
    src/debugger/disassemblertextview.h \
    src/debugger/disassemblerview.h \
    src/debugger/disassemblerwidget.h \
    src/debugger/memorypageswidget.h \
    src/debugger/registerswidget.h \
    src/debugger/stackwidget.h \
    src/emulator/emulatormanager.h \
    src/emulator/filemanager.h \
    src/emulator/guiemulatorcontext.h \
    src/emulator/keyboardmanager.h \
    src/emulator/soundmanager.h \
    src/logviewer/logstatusbar.h \
    src/logviewer/logviewer.h \
    src/logviewer/logwindow.h \
    src/mainwindow.h \
    src/widgets/devicescreen.h \
    src/widgets/verticallabel.h

FORMS += \
    src/mainwindow.ui \
    src/ui/debuggerwindow.ui \
    src/ui/devicescreen.ui \
    src/ui/disassemblerview.ui \
    src/ui/hexview.ui \
    src/ui/logstatusbar.ui \
    src/ui/logwindow.ui \
    src/ui/memoryblock.ui \
    src/ui/memorymap.ui \
    src/ui/ula.ui \
    src/ui/widgets/disassemblerwidget.ui \
    src/ui/widgets/memorypageswidget.ui \
    src/ui/widgets/registerswidget.ui \
    src/ui/widgets/stackwidget.ui

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

