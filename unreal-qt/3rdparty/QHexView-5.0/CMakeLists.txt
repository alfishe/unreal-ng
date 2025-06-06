cmake_minimum_required(VERSION 3.13)

project(QHexView)

option(QHEXVIEW_BUILD_EXAMPLE "Build Example Application" OFF)
option(QHEXVIEW_USE_QT5 "Enable Qt5 build" OFF)
option(QHEXVIEW_ENABLE_DIALOGS "BuiltIn dialogs" OFF)

if(QHEXVIEW_USE_QT5)
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
else()
    find_package(Qt6 COMPONENTS Widgets)

    if(NOT Qt6_FOUND)
        find_package(Qt5 REQUIRED COMPONENTS Widgets)
    endif()
endif()

add_library(${PROJECT_NAME} STATIC)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        CXX_STANDARD_REQUIRED YES
        CXX_STANDARD 11
        AUTOMOC ON
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Qt::Widgets
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_sources(${PROJECT_NAME}
    PRIVATE 
        include/QHexView/model/buffer/qdevicebuffer.h
        include/QHexView/model/buffer/qhexbuffer.h
        include/QHexView/model/buffer/qmappedfilebuffer.h
        include/QHexView/model/buffer/qmemorybuffer.h
        include/QHexView/model/buffer/qmemoryrefbuffer.h
        include/QHexView/model/commands/hexcommand.h
        include/QHexView/model/commands/insertcommand.h
        include/QHexView/model/commands/removecommand.h
        include/QHexView/model/commands/replacecommand.h
        include/QHexView/model/commands/replacecommand.h
        include/QHexView/model/qhexcursor.h
        include/QHexView/model/qhexdelegate.h
        include/QHexView/model/qhexdocument.h
        include/QHexView/model/qhexmetadata.h
        include/QHexView/model/qhexoptions.h
        include/QHexView/model/qhexutils.h
        include/QHexView/qhexview.h

    PRIVATE 
        src/model/commands/hexcommand.cpp
        src/model/commands/insertcommand.cpp
        src/model/commands/removecommand.cpp
        src/model/commands/replacecommand.cpp
        src/model/buffer/qdevicebuffer.cpp
        src/model/buffer/qhexbuffer.cpp
        src/model/buffer/qmemorybuffer.cpp
        src/model/buffer/qmemoryrefbuffer.cpp
        src/model/buffer/qmappedfilebuffer.cpp
        src/model/qhexdelegate.cpp
        src/model/qhexutils.cpp
        src/model/qhexcursor.cpp
        src/model/qhexmetadata.cpp
        src/model/qhexdocument.cpp
        src/qhexview.cpp
)

if(QHEXVIEW_ENABLE_DIALOGS)
    target_sources(${PROJECT_NAME}
        PRIVATE
            include/QHexView/dialogs/hexfinddialog.h
            src/dialogs/hexfinddialog.cpp
    )

    target_compile_definitions(${PROJECT_NAME} 
        PUBLIC
            QHEXVIEW_ENABLE_DIALOGS
    )
endif()

if(QHEXVIEW_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
