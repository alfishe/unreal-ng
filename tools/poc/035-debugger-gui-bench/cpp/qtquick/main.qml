import QtQuick
import Bench 1.0

// Same fixed layout as the other variants (common/layout.h). Text views are Repeaters of Text items bound to
// C++ list models; the timeline and the images are custom scene-graph items.
Rectangle {
    id: root
    width: 1600; height: 1000
    color: "#0E1014"
    scale: metrics.zoom              // runtime zoom (scale-switch test); start-up scale uses QT_SCALE_FACTOR
    transformOrigin: Item.TopLeft

    readonly property string mono: "DejaVu Sans Mono"
    readonly property real lineH: 16
    readonly property real titleH: 24
    readonly property real pad: 8

    component Panel: Rectangle {
        property alias title: titleText.text
        default property alias content: body.data
        color: "#161A20"; border.color: "#2A303A"; radius: 6
        Rectangle {
            x: 1; y: 1; width: parent.width - 2; height: root.titleH - 1
            color: "#1C2129"; radius: 6
            Text { id: titleText; x: 8; anchors.verticalCenter: parent.verticalCenter
                   font.family: root.mono; font.pixelSize: 12; color: "#AEB6C0" }
        }
        Item { id: body; x: root.pad; y: root.titleH + root.pad; clip: true
               width: parent.width - 2 * root.pad; height: parent.height - root.titleH - 2 * root.pad }
    }

    Loader {
        active: showDisasm
        sourceComponent: Component {
            Panel {
                title: "Disassembly"
                x: 8; y: 8; width: 440; height: 24 + 60 * 16 + 16
                Repeater {
                    model: disasmModel
                    Item {
                        y: index * metrics.lineH; width: 424; height: metrics.lineH
                        Rectangle { anchors.fill: parent; color: "#2B2414"; visible: model.pc }
                        Rectangle { x: 1; y: metrics.lineH / 2 - 4; width: 8; height: 8; radius: 4; color: "#FF8A7A"; visible: model.bp }
                        Text { x: metrics.disAddrX;    text: model.addr;    font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#8E98A5" }
                        Text { x: metrics.disBytesX;   text: model.bytes;   font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#8E98A5" }
                        Text { x: metrics.disTextX;    text: model.text;    font.family: root.mono; font.pixelSize: metrics.fontPx; color: model.pc ? "#E8A33D" : "#E6E8EB" }
                        Text { x: metrics.disCommentX; text: model.comment; font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#8E98A5" }
                    }
                }
            }
        }
    }

    Loader {
        active: showHex
        sourceComponent: Component {
            Panel {
                title: "Memory"
                x: 456; y: 8; width: 560; height: 24 + 32 * 16 + 16
                Repeater {
                    model: hexModel
                    Item {
                        id: hexRow
                        y: index * metrics.lineH; width: 544; height: metrics.lineH
                        property var rowBytes: model.bytes
                        property var rowChanged: model.changed
                        Text { x: 0; text: model.addr; font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#8E98A5" }
                        Repeater {
                            model: 16
                            Rectangle {
                                x: metrics.hexBytesX + index * metrics.hexByteStep - 2; width: metrics.hexChangedW; height: metrics.lineH - 1
                                color: hexRow.rowChanged[index] ? "#3A2E14" : "transparent"
                                Text { x: 2; text: hexRow.rowBytes[index]; font.family: root.mono; font.pixelSize: metrics.fontPx
                                       color: hexRow.rowChanged[index] ? "#E8A33D" : "#E6E8EB" }
                            }
                        }
                        Text { x: metrics.hexAsciiX; text: model.ascii; font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#8E98A5" }
                    }
                }
            }
        }
    }

    Loader {
        active: showImages
        sourceComponent: Component {
            Panel {
                title: "Screen / layers"
                x: 1024; y: 8; width: 568; height: 24 + 2 * 224 + 24
                Repeater {
                    model: 4
                    ImageItem { index: modelData; x: (modelData % 2) * (272 + 8); y: Math.floor(modelData / 2) * (217.6 + 8)
                                width: 272; height: 217.6 }
                }
            }
        }
    }

    Loader {
        active: showTimeline
        sourceComponent: Component {
            Panel {
                title: "Timeline"
                x: 456; y: 568; width: 1136; height: 424
                TimelineItem { id: timeline; width: 1120; height: 376 }
                Repeater {
                    model: 5
                    Text { x: 4; y: index * ((424 - 24 - 16 - 20) / 5) + 2; text: ["INT", "line INT", "DMA", "reg writes", "copper"][index]
                           font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#E6E8EB" }
                }
                Repeater {
                    model: 20   // fixed delegates; positions and labels are bindings to the bridge (no delegate re-creation)
                    Text { x: bridge.ticks[index].x + 2; y: (424 - 24 - 16 - 20) + 3; text: bridge.ticks[index].label
                           font.family: root.mono; font.pixelSize: metrics.fontPx; color: "#8E98A5" }
                }
            }
        }
    }
}
