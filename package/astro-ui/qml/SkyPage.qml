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

    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        spacing: 6
        AstroButton { text: "+"; width: 56; onClicked: chart.fieldOfView = chart.fieldOfView / 1.5 }
        AstroButton { text: "−"; width: 56; onClicked: chart.fieldOfView = chart.fieldOfView * 1.5 }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 6
        width: Math.min(parent.width - 80, 320)
        height: searchColumn.implicitHeight + 8
        color: theme.panel
        border.color: theme.line
        radius: 6

        Column {
            id: searchColumn
            anchors.fill: parent
            anchors.margins: 4
            spacing: 2

            TextInput {
                id: searchBox
                width: parent.width
                height: 34
                verticalAlignment: TextInput.AlignVCenter
                color: theme.text
                font.pixelSize: 16
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextChanged: page.searchResults = sky.search(text).slice(0, 5)

                Text {
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    text: "Search: Mars, M31, Vega"
                    color: theme.dim
                    font.pixelSize: 15
                    visible: searchBox.text.length === 0
                }
            }

            Repeater {
                model: page.searchResults
                delegate: Text {
                    required property var modelData
                    width: searchColumn.width
                    height: 36
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    color: theme.text
                    font.pixelSize: 15
                    text: modelData.name

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

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        height: details.implicitHeight + 12
        visible: root.target.name !== undefined
        color: theme.panel
        border.color: theme.line
        radius: 6

        RowLayout {
            id: details
            anchors.fill: parent
            anchors.margins: 6
            spacing: 8

            Column {
                Layout.fillWidth: true
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: theme.text
                    font.pixelSize: 16
                    font.bold: true
                    text: root.target.name || ""
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: page.result.length > 0 ? theme.danger : theme.dim
                    font.pixelSize: 13
                    text: page.result.length > 0 ? page.result
                        : (root.target.type ? root.target.type + "   " : "")
                          + (root.target.mag !== undefined ? "Mag " + Number(root.target.mag).toFixed(1) + "   " : "")
                          + (root.target.alt !== undefined ? "Alt " + Number(root.target.alt).toFixed(1) + "°" : "")
                }
            }
            AstroButton {
                text: "GoTo"
                enabled: mount.mountConnected
                onClicked: page.result = mount.gotoTarget(root.target.ra, root.target.dec, root.solarFilterFitted)
            }
            AstroButton {
                text: "Sync"
                enabled: mount.mountConnected
                onClicked: page.result = mount.syncTarget(root.target.ra, root.target.dec)
            }
        }
    }
}
