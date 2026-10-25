import QtQuick
import BenchSG 1.0

// Same layout as every other variant; the four views are C++ scene-graph items (items.cpp), no QML delegates.
Rectangle {
    id: root
    width: 1600; height: 1000
    color: "#0E1014"

    readonly property string mono: "DejaVu Sans Mono"

    component Panel: Rectangle {
        property alias title: titleText.text
        default property alias content: body.data
        color: "#161A20"; border.color: "#2A303A"; radius: 6
        Rectangle {
            x: 1; y: 1; width: parent.width - 2; height: 23
            color: "#1C2129"; radius: 6
            Text { id: titleText; x: 8; anchors.verticalCenter: parent.verticalCenter
                   font.family: root.mono; font.pixelSize: 12; color: "#AEB6C0" }
        }
        Item { id: body; x: 8; y: 32; width: parent.width - 16; height: parent.height - 40; clip: true }
    }

    Panel { title: "Disassembly"; x: 8; y: 8; width: 440; height: 24 + 60 * 16 + 16; visible: showDisasm
            DisasmItem { objectName: "hot"; width: 424; height: 960 } }
    Panel { title: "Memory"; x: 456; y: 8; width: 560; height: 24 + 32 * 16 + 16; visible: showHex
            HexItem { objectName: "hot"; width: 544; height: 512 } }
    Panel { title: "Screen / layers"; x: 1024; y: 8; width: 568; height: 24 + 2 * 224 + 24; visible: showImages
            Repeater { model: showImages ? 4 : 0
                ImageItem { objectName: "hot"; index: modelData; x: (modelData % 2) * (272 + 8); y: Math.floor(modelData / 2) * (217.6 + 8)
                            width: 272; height: 217.6 } } }
    Panel { title: "Timeline"; x: 456; y: 568; width: 1136; height: 424; visible: showTimeline
            TimelineItem { objectName: "hot"; width: 1120; height: 376 } }
}
