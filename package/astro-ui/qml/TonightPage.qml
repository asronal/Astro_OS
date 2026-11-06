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

    function moonName(lit, waxing) {
        if (lit < 0.03) return "New Moon"
        if (lit > 0.97) return "Full Moon"
        if (Math.abs(lit - 0.5) < 0.05) return waxing ? "First Quarter" : "Last Quarter"
        return (waxing ? "Waxing " : "Waning ") + (lit < 0.5 ? "Crescent" : "Gibbous")
    }

    Component.onCompleted: refresh()

    Connections {
        target: sky
        function onTicked() { page.refresh() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // Moon and darkness
        ColumnLayout {
            Layout.preferredWidth: 216
            Layout.maximumWidth: 216
            Layout.fillHeight: true
            spacing: 10

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 116
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    MoonDisc {
                        size: 74
                        lit: page.night.moonLit !== undefined ? page.night.moonLit : 0
                        waxing: page.night.moonWaxing === true
                    }
                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        AText {
                            width: parent.width
                            text: page.night.moonLit !== undefined ? page.moonName(page.night.moonLit, page.night.moonWaxing) : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                            weight: 600
                        }
                        AText {
                            text: page.night.moonLit !== undefined ? Math.round(page.night.moonLit * 100) + "% lit" : ""
                            color: theme.dim
                            font.pixelSize: 12
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 2

                    AText {
                        text: "DARK SKY"
                        color: theme.accent
                        font.pixelSize: 10
                        font.letterSpacing: 1.8
                        weight: 600
                    }
                    AText {
                        visible: page.night.normal === true
                        text: (page.night.astronomicalDusk || "") + " – " + (page.night.astronomicalDawn || "")
                        style: "data"
                        font.pixelSize: 21
                    }
                    AText {
                        visible: page.night.normal === false
                        Layout.fillWidth: true
                        text: "No sunset today here"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 14
                    }
                    Item { Layout.fillHeight: true }
                    Repeater {
                        model: [
                            { name: "Sunset", time: page.night.sunset },
                            { name: "Sunrise", time: page.night.sunrise },
                            { name: "Moonrise", time: page.night.moonRise },
                            { name: "Moonset", time: page.night.moonSet }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            AText { Layout.fillWidth: true; text: modelData.name; color: theme.dim; font.pixelSize: 12 }
                            AText { text: modelData.time || "–"; style: "data"; font.pixelSize: 13 }
                        }
                    }
                }
            }
        }

        // Targets and the solar system
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            AText {
                text: page.targets.length > 0 ? "BEST TONIGHT" : "NOTHING HIGH TONIGHT"
                color: theme.accent
                font.pixelSize: 10
                font.letterSpacing: 1.8
                weight: 600
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                visible: page.targets.length > 0
                orientation: ListView.Horizontal
                spacing: 7
                clip: true
                model: page.targets
                delegate: Card {
                    required property var modelData
                    width: chip.implicitWidth + 22
                    height: 52
                    Column {
                        id: chip
                        anchors.centerIn: parent
                        spacing: 1
                        AText { text: modelData.name; font.pixelSize: 13; weight: 600 }
                        AText {
                            text: modelData.maxAlt.toFixed(0) + "° at " + modelData.bestTime
                            color: theme.dim
                            font.pixelSize: 11
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.selectTarget(modelData)
                            root.page = 1
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.topMargin: 8
                    anchors.bottomMargin: 6
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        Layout.preferredHeight: 18
                        spacing: 0
                        Repeater {
                            model: [["", 86], ["ALT", 62], ["MAG", 50], ["RISE", 54], ["HIGH", 54], ["SET", 54]]
                            delegate: AText {
                                required property var modelData
                                Layout.fillWidth: modelData[0] === ""
                                Layout.preferredWidth: modelData[1]
                                horizontalAlignment: modelData[0] === "" ? Text.AlignLeft : Text.AlignRight
                                text: modelData[0]
                                color: theme.dim
                                font.pixelSize: 10
                                font.letterSpacing: 1.1
                                weight: 600
                            }
                        }
                    }

                    Repeater {
                        model: page.bodies
                        delegate: Item {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool up: modelData.alt > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Rectangle {
                                anchors.top: parent.top
                                width: parent.width
                                height: 1
                                color: theme.line
                                opacity: 0.55
                            }
                            RowLayout {
                                anchors.fill: parent
                                spacing: 0
                                AText {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 86
                                    text: row.modelData.name
                                    color: row.up ? theme.text : theme.dim
                                    font.pixelSize: 13
                                    weight: row.up ? 600 : 400
                                }
                                Repeater {
                                    model: [
                                        [row.modelData.alt.toFixed(1) + "°", 62],
                                        [row.modelData.mag.toFixed(1), 50],
                                        [row.modelData.rise, 54],
                                        [row.modelData.transit, 54],
                                        [row.modelData.set, 54]
                                    ]
                                    delegate: AText {
                                        required property var modelData
                                        Layout.preferredWidth: modelData[1]
                                        horizontalAlignment: Text.AlignRight
                                        text: modelData[0]
                                        style: "data"
                                        color: row.up ? theme.text : theme.dim
                                        font.pixelSize: 12
                                    }
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.selectTarget(row.modelData)
                                    root.page = 1
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
