import QtQuick
import QtQuick.Layouts

Item {
    id: page

    property var bodies: []
    property var night: ({})
    property var targets: []

    function refresh() {
        if (!sky.locationSet)
            return
        bodies = sky.bodies()
        night = sky.nightInfo()
        targets = sky.bestTargets()
    }

    Component.onCompleted: refresh()

    Connections {
        target: sky
        function onTicked() { page.refresh() }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 8
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 6

            Text {
                Layout.fillWidth: true
                color: theme.dim
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                text: !page.night.normal ? "No normal sunset today at this location."
                    : "Sunset " + page.night.sunset + "   Dark from " + page.night.astronomicalDusk
                      + " to " + page.night.astronomicalDawn + "   Sunrise " + page.night.sunrise
            }

            Text {
                text: "Best targets tonight"
                color: theme.text
                font.pixelSize: 16
                font.bold: true
                visible: page.targets.length > 0
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: page.targets
                    delegate: AstroButton {
                        required property var modelData
                        text: modelData.name + "  " + modelData.maxAlt.toFixed(0) + "° at " + modelData.bestTime
                        onClicked: {
                            root.selectTarget(modelData)
                            root.page = 1
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 7
                columnSpacing: 10
                rowSpacing: 4

                Repeater {
                    model: ["Object", "Alt", "Az", "Mag", "Rise", "Transit", "Set"]
                    delegate: Text {
                        required property string modelData
                        text: modelData
                        color: theme.dim
                        font.pixelSize: 13
                        font.bold: true
                    }
                }

                Repeater {
                    model: page.bodies
                    delegate: Text {
                        required property var modelData
                        Layout.columnSpan: 7
                        Layout.fillWidth: true
                        font.pixelSize: 15
                        font.family: "DejaVu Sans Mono"
                        color: modelData.alt > 0 ? theme.text : theme.dim
                        text: modelData.name.padEnd(9, " ") + modelData.alt.toFixed(1).padStart(6, " ") + "°"
                              + modelData.az.toFixed(0).padStart(5, " ") + "°"
                              + modelData.mag.toFixed(1).padStart(7, " ")
                              + "  " + modelData.rise.padEnd(6, " ")
                              + modelData.transit.padEnd(6, " ") + modelData.set
                    }
                }
            }
        }
    }
}
