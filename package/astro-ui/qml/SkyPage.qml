import QtQuick
import QtQuick.Layouts
import Astro 1.0

Item {
    id: page

    property string result: ""
    property var searchResults: []

    onVisibleChanged: if (visible && root.target.ra !== undefined) chart.centerOn(root.target.ra, root.target.dec)

    SkyChart {
        id: chart
        anchors.fill: parent
        backend: sky
        nightMode: sky.nightMode
        target: root.target
        mountVisible: mount.mountConnected
        mountRa: mount.ra
        mountDec: mount.dec
    }

    PinchArea {
        anchors.fill: parent
        property double startFov: 90
        onPinchStarted: startFov = chart.fieldOfView
        onPinchUpdated: (pinch) => chart.fieldOfView = startFov / pinch.scale

        MouseArea {
            anchors.fill: parent
            property double lastX: 0
            property double lastY: 0
            property bool dragged: false
            onPressed: (mouse) => { lastX = mouse.x; lastY = mouse.y; dragged = false }
            onPositionChanged: (mouse) => {
                if (Math.abs(mouse.x - lastX) + Math.abs(mouse.y - lastY) > 2)
                    dragged = true
                chart.panBy(mouse.x - lastX, mouse.y - lastY)
                lastX = mouse.x
                lastY = mouse.y
            }
            onClicked: (mouse) => {
                if (dragged)
                    return
                var object = chart.objectAt(mouse.x, mouse.y)
                if (object.name !== undefined) {
                    root.selectTarget(object)
                    page.result = ""
                }
            }
        }
    }

    // Zoom
    Card {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        width: 46
        height: 92
        Column {
            anchors.fill: parent
            AstroButton { width: 46; height: 46; style: "quiet"; glyph: "plus"; onClicked: chart.fieldOfView = chart.fieldOfView / 1.5 }
            AstroButton { width: 46; height: 46; style: "quiet"; glyph: "minus"; onClicked: chart.fieldOfView = chart.fieldOfView * 1.5 }
        }
    }

    // Search
    Card {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        width: Math.min(parent.width - 80, 300)
        height: searchColumn.implicitHeight + 6

        Column {
            id: searchColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3

            Row {
                width: parent.width
                height: 34
                leftPadding: 8
                spacing: 7
                Glyph {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"
                    size: 17
                    tint: theme.dim
                }
                TextInput {
                    id: searchBox
                    width: parent.width - 42
                    height: parent.height
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.text
                    font.family: theme.sansFamily
                    font.pixelSize: 14
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onTextChanged: page.searchResults = sky.search(text).slice(0, 5)

                    AText {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "Search Mars, M31, Vega"
                        color: theme.dim
                        font.pixelSize: 14
                        visible: searchBox.text.length === 0
                    }
                }
            }

            Repeater {
                model: page.searchResults
                delegate: Item {
                    required property var modelData
                    width: searchColumn.width
                    height: 38
                    Rectangle { width: parent.width; height: 1; color: theme.line }
                    AText {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.pixelSize: 14
                        text: modelData.name
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.selectTarget(modelData)
                            chart.centerOn(modelData.ra, modelData.dec)
                            searchBox.text = ""
                            searchBox.focus = false
                            page.result = ""
                        }
                    }
                }
            }
        }
    }

    // The selected object
    Card {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        height: 64
        visible: root.target.name !== undefined

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Column {
                Layout.fillWidth: true
                spacing: 2
                AText {
                    width: parent.width
                    elide: Text.ElideRight
                    font.pixelSize: 16
                    weight: 600
                    text: root.target.name || ""
                }
                AText {
                    width: parent.width
                    elide: Text.ElideRight
                    color: page.result.length > 0 ? theme.danger : theme.dim
                    font.pixelSize: 12
                    text: page.result.length > 0 ? page.result
                        : (root.target.type ? root.target.type + "  ·  " : "")
                          + (root.target.mag !== undefined ? "Mag " + Number(root.target.mag).toFixed(1) + "  ·  " : "")
                          + (root.target.alt !== undefined ? "Alt " + Number(root.target.alt).toFixed(1) + "°" : "")
                }
            }
            AstroButton {
                text: "Sync"
                enabled: mount.mountConnected
                onClicked: page.result = mount.syncTarget(root.target.ra, root.target.dec)
            }
            AstroButton {
                text: "GoTo"
                style: "primary"
                implicitWidth: 88
                enabled: mount.mountConnected
                onClicked: page.result = mount.gotoTarget(root.target.ra, root.target.dec, root.solarFilterFitted)
            }
        }
    }
}
