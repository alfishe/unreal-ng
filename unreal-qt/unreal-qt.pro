QT += core gui

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
    emulator/emulatormanager.cpp \
    emulator/keyboardmanager.cpp \
    main.cpp \
    mainwindow.cpp \
    widgets/devicescreen.cpp \
    widgets/verticallabel.cpp

HEADERS += \
    emulator/emulatormanager.h \
    emulator/keyboardmanager.h \
    mainwindow.h \
    widgets/devicescreen.h \
    widgets/verticallabel.h

FORMS += \
    mainwindow.ui \
    ui/devicescreen.ui \
    ui/memoryblock.ui \
    ui/memorymap.ui \
    ui/registers.ui

TRANSLATIONS += \
    unreal-qt_en_US.ts

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES +=

win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../cmake-build-release/core/src/ -lcore -lshlwapi
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../cmake-build-debug/core/src/ -lcore -lshlwapi
else:unix:!macx: LIBS += -L$$PWD/../cmake-build-debug/core/src/ -lcore
else:macx: LIBS += -L$$PWD/../cmake-build-debug/core/src/ -lcore -liconv

INCLUDEPATH += $$PWD/../core/src
DEPENDPATH += $$PWD/../core/src


win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-release/core/src/libcore.a
else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/debug/libcore.a
else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/release/core.lib
else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/debug/core.lib
else:unix: PRE_TARGETDEPS += $$PWD/../cmake-build-debug/core/src/libcore.a

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
    MACOS_PACKAGE_CONTENTS_PATH = $$OUT_PWD/unreal-qt.app/Contents/MacOS/

    COPY_CONFIG_COMMAND = $$quote(cp $$CONFIG_FILE $$MACOS_PACKAGE_CONTENTS_PATH$$escape_expand(\n\t))
    COPY_ROM_COMMAND = $$quote(cp -R $$ROM_PATH $$MACOS_PACKAGE_CONTENTS_PATH$$/rom/$$escape_expand(\n\t))
    message($$COPY_CONFIG_COMMAND)
    message($$COPY_ROM_COMMAND)

    QMAKE_POST_LINK += && $$COPY_CONFIG_COMMAND
    QMAKE_POST_LINK += && $$COPY_ROM_COMMAND
}

win32: {
    CONFIG_FILE = $$PWD\..\data\configs\spectrum128\unreal.ini
    QMAKE_POST_LINK += $$quote(cmd /c copy /y $$CONFIG_FILE $$OUT_PWD$$escape_expand(\n\t))
}
