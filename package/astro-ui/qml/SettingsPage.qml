import QtQuick
import QtQuick.Layouts

Item {
    id: page

    component Field: ColumnLayout {
        property alias text: input.text
        property string label
        Layout.fillWidth: true
        spacing: 3
        AText { text: label; color: theme.dim; font.pixelSize: 11 }
        Rectangle {
            Layout.fillWidth: true
            height: 42
            radius: 8
            color: theme.background
            border.color: input.activeFocus ? theme.accent : theme.line
            TextInput {
                id: input
                anchors.fill: parent
                anchors.margins: 8
                verticalAlignment: TextInput.AlignVCenter
                color: theme.text
                font.family: theme.monoFamily
                font.pixelSize: 14
                inputMethodHints: Qt.ImhFormattedNumbersOnly
            }
        }
    }

    component Heading: AText {
        color: theme.accent
        font.pixelSize: 12
        weight: 600
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: content.implicitHeight
        clip: true

        GridLayout {
            id: content
            width: parent.width
            columns: 2
            rowSpacing: 10
            columnSpacing: 10

            Card {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.preferredHeight: welcome.implicitHeight + 22
                visible: !sky.locationSet
                ColumnLayout {
                    id: welcome
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 12
                    spacing: 2
                    AText { text: "Welcome to Astro OS"; style: "display"; weight: 600; font.pixelSize: 18 }
                    AText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.dim
                        font.pixelSize: 13
                        text: "Tell it where you are, so it can work out your sky."
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: location.implicitHeight + 22
                ColumnLayout {
                    id: location
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 11
                    spacing: 7

                    Heading { text: "Location" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 7
                        columnSpacing: 8
                        Field { id: lat; label: "Latitude, north +"; text: sky.locationSet ? sky.latitude.toFixed(4) : "" }
                        Field { id: lon; label: "Longitude, east +"; text: sky.locationSet ? sky.longitude.toFixed(4) : "" }
                        Field { id: elev; label: "Elevation, m"; text: sky.locationSet ? sky.elevation.toFixed(0) : "0" }
                        Field { id: offset; label: "Time zone, UTC +"; text: sky.utcOffsetMinutes / 60 }
                    }
                    AText {
                        id: problem
                        visible: text.length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.danger
                        font.pixelSize: 12
                        text: ""
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                    AstroButton {
                        text: sky.detecting ? "Finding..." : "Find my location"
                        enabled: !sky.detecting
                        onClicked: {
                            problem.text = ""
                            sky.detectLocation()
                        }
                    }
                    AstroButton {
                        Layout.fillWidth: true
                        text: "Save location"
                        style: "primary"
                        onClicked: {
                            var a = parseFloat(lat.text)
                            var o = parseFloat(lon.text)
                            var e = parseFloat(elev.text)
                            var z = parseFloat(offset.text)
                            if (isNaN(a) || a < -90 || a > 90 || isNaN(o) || o < -180 || o > 180) {
                                problem.text = "Latitude must be −90 to 90, and longitude −180 to 180."
                                return
                            }
                            problem.text = ""
                            sky.setLocation(a, o, isNaN(e) ? 0 : e)
                            sky.utcOffsetMinutes = isNaN(z) ? 0 : Math.round(z * 60)
                            root.page = 0
                        }
                    }
                    }
                    AText {
                        visible: text.length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.dim
                        font.pixelSize: 12
                        text: sky.detectStatus
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 10

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: clockColumn.implicitHeight + 22
                    ColumnLayout {
                        id: clockColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 7
                        Heading { text: "Clock" }
                        AText {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: sky.clockSet ? theme.text : theme.danger
                            font.pixelSize: 13
                            text: sky.clockStatus
                        }
                        RowLayout {
                            visible: !sky.clockSynced
                            Layout.fillWidth: true
                            spacing: 8
                            Field { id: clockField; label: "Date and time, UTC (YYYY-MM-DD HH:MM)" }
                            AstroButton {
                                Layout.alignment: Qt.AlignBottom
                                text: "Set"
                                onClicked: clockProblem.text = sky.setClockUtc(clockField.text)
                            }
                        }
                        AText {
                            id: clockProblem
                            visible: text.length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: theme.danger
                            font.pixelSize: 12
                            text: ""
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: display.implicitHeight + 22
                    ColumnLayout {
                        id: display
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 7
                        Heading { text: "Display" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            AstroButton { Layout.fillWidth: true; text: "Day"; active: !sky.nightMode; onClicked: sky.nightMode = false }
                            AstroButton { Layout.fillWidth: true; text: "Night"; active: sky.nightMode; onClicked: sky.nightMode = true }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: sky.hasBacklight
                            spacing: 6
                            Repeater {
                                model: [10, 25, 50, 100]
                                delegate: AstroButton {
                                    required property int modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 44
                                    text: modelData + "%"
                                    active: sky.brightness === modelData
                                    onClicked: sky.brightness = modelData
                                }
                            }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: filter.implicitHeight + 22
                    ColumnLayout {
                        id: filter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 6
                        Heading { text: "Solar filter" }
                        AText {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: theme.dim
                            font.pixelSize: 12
                            text: "Pointing at the Sun without a proper filter can cause permanent blindness and destroy equipment, so GoTo refuses to go near it. This resets every time Astro OS starts."
                        }
                        AstroButton {
                            Layout.fillWidth: true
                            text: root.solarFilterFitted ? "Solar filter is fitted" : "I have fitted a solar filter"
                            active: root.solarFilterFitted
                            onClicked: root.solarFilterFitted = !root.solarFilterFitted
                        }
                    }
                }
            }

            Card {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.preferredHeight: laptop.implicitHeight + 22
                RowLayout {
                    id: laptop
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 11
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Heading { text: "Laptop control" }
                        AText {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: theme.dim
                            font.pixelSize: 12
                            text: "Lets apps such as KStars control this telescope over the network. INDI has no password, so anyone on the network could move it, and the Sun protection here doesn't apply to those apps."
                        }
                    }
                    AstroButton {
                        text: mount.remoteAccess ? "On" : "Off"
                        active: mount.remoteAccess
                        implicitWidth: 72
                        onClicked: mount.applyProfile(mount.driver, mount.port, !mount.remoteAccess)
                    }
                }
            }
        }
    }
}
