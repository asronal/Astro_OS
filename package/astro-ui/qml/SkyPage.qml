import QtQuick
import QtQuick.Layouts
import Astro 1.0

Item {
    id: page

    property string result: ""
    property var searchResults: []
    property bool searchOpen: false
    property bool layersOpen: false

    function closePanels() {
        searchOpen = false
        layersOpen = false
        searchBox.text = ""
        searchBox.focus = false
    }

    onVisibleChanged: {
        if (visible && root.target.ra !== undefined)
            chart.centerOn(root.target.ra, root.target.dec)
        if (!visible)
            closePanels()
    }

    // Runs under the header and navigation, which sit on top of it
    SkyChart {
        id: chart
        anchors.fill: parent
        anchors.topMargin: -root.headerHeight - 1
        anchors.bottomMargin: -root.navigationHeight
        backend: sky
        nightMode: sky.nightMode
        target: root.target
        mountVisible: mount.mountConnected
        mountRa: mount.ra
        mountDec: mount.dec
        safeTop: root.headerHeight
        safeBottom: root.navigationHeight + (root.target.name !== undefined ? 84 : 44)
    }

    PinchArea {
        anchors.fill: chart
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
                if (page.searchOpen || page.layersOpen) {
                    page.closePanels()
                    return
                }
                var object = chart.objectAt(mouse.x, mouse.y)
                if (object.name !== undefined) {
                    root.selectTarget(object)
                    page.result = ""
                } else {
                    root.skyFullView = !root.skyFullView
                }
            }
        }
    }

    // Search and layers
    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 8

        Card {
            width: 50
            height: 100
            opacity: 0.94
            Column {
                anchors.fill: parent
                AstroButton {
                    width: 50; height: 50; style: "quiet"; glyph: "search"
                    active: page.searchOpen
                    onClicked: {
                        page.layersOpen = false
                        page.searchOpen = !page.searchOpen
                        if (page.searchOpen)
                            searchBox.forceActiveFocus()
                        else {
                            searchBox.text = ""
                            searchBox.focus = false
                        }
                    }
                }
                AstroButton {
                    width: 50; height: 50; style: "quiet"; glyph: "layers"
                    active: page.layersOpen
                    onClicked: {
                        page.searchOpen = false
                        page.layersOpen = !page.layersOpen
                    }
                }
            }
        }
    }

    // Zoom
    Card {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        width: 50
        height: 100
        opacity: 0.94
        Column {
            anchors.fill: parent
            AstroButton { width: 50; height: 50; style: "quiet"; glyph: "plus"; onClicked: chart.fieldOfView = chart.fieldOfView / 1.5 }
            AstroButton { width: 50; height: 50; style: "quiet"; glyph: "minus"; onClicked: chart.fieldOfView = chart.fieldOfView * 1.5 }
        }
    }

    // Where the chart is looking
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 10
        visible: root.target.name === undefined
        width: viewText.implicitWidth + 20
        height: 26
        radius: 13
        color: Qt.rgba(theme.background.r, theme.background.g, theme.background.b, 0.7)
        AText {
            id: viewText
            anchors.centerIn: parent
            font.pixelSize: 12
            color: theme.dim
            text: {
                var names = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]
                var facing = names[Math.round(chart.centerAz / 45) % 8]
                return "Facing " + facing + ", " + Math.round(chart.centerAlt) + "° up, " + chart.fieldOfView.toFixed(chart.fieldOfView < 10 ? 1 : 0) + "° wide"
            }
        }
    }

    // Search panel
    Card {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 68
        anchors.topMargin: 10
        width: Math.min(parent.width - 130, 320)
        height: searchColumn.implicitHeight + 6
        visible: page.searchOpen

        Column {
            id: searchColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3

            Row {
                width: parent.width
                height: 40
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
                            page.closePanels()
                            page.result = ""
                        }
                    }
                }
            }
        }
    }

    // Layers panel
    Card {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 68
        anchors.topMargin: 10
        width: 228
        height: layerColumn.implicitHeight + 16
        visible: page.layersOpen

        Column {
            id: layerColumn
            anchors.centerIn: parent
            width: parent.width - 16
            spacing: 4

            Repeater {
                model: [
                    { label: "Constellations", key: "showConstellations" },
                    { label: "Milky Way", key: "showMilkyWay" },
                    { label: "Landscape", key: "showLandscape" },
                    { label: "Daylight and air", key: "showAtmosphere" }
                ]
                delegate: Item {
                    id: layer
                    required property var modelData
                    readonly property bool isOn: chart[modelData.key]
                    width: layerColumn.width
                    height: 44
                    AText {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 14
                        text: layer.modelData.label
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 42
                        height: 24
                        radius: 12
                        color: layer.isOn ? theme.accent : theme.surfaceHigh
                        border.color: theme.line
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 9
                            y: 3
                            x: layer.isOn ? 21 : 3
                            color: layer.isOn ? theme.accentInk : theme.dim
                            Behavior on x { NumberAnimation { duration: 100 } }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: chart[layer.modelData.key] = !chart[layer.modelData.key]
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
        opacity: 0.96

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
                        : [root.target.type,
                           root.target.mag !== undefined ? "magnitude " + Number(root.target.mag).toFixed(1) : undefined,
                           root.target.alt !== undefined ? Number(root.target.alt).toFixed(0) + "° up" : undefined]
                          .filter(function (part) { return part !== undefined && part !== "" }).join(", ")
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
            AstroButton {
                glyph: "close"
                style: "quiet"
                implicitWidth: 46
                onClicked: {
                    root.selectTarget({})
                    page.result = ""
                }
            }
        }
    }
}
