import QtQuick
import QtQuick.Layouts

Item {
    id: page

    property int driverIndex: {
        for (var i = 0; i < mount.drivers.length; i++) {
            if (mount.drivers[i].binary === mount.driver)
                return i
        }
        return 0
    }
    readonly property var selectedDriver: mount.drivers[driverIndex]
    readonly property bool servoSelected: selectedDriver.binary === "indi_astro_servo"

    function degToHms(deg) {
        var hours = deg / 15.0
        var h = Math.floor(hours)
        var m = Math.floor((hours - h) * 60)
        var s = Math.floor(((hours - h) * 60 - m) * 60)
        return String(h).padStart(2, "0") + "h " + String(m).padStart(2, "0") + "m " + String(s).padStart(2, "0") + "s"
    }

    function degToDms(deg) {
        var sign = deg < 0 ? "−" : "+"
        var a = Math.abs(deg)
        var d = Math.floor(a)
        var m = Math.floor((a - d) * 60)
        var s = Math.floor(((a - d) * 60 - m) * 60)
        return sign + String(d).padStart(2, "0") + "° " + String(m).padStart(2, "0") + "′ " + String(s).padStart(2, "0") + "″"
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // Choosing and connecting a mount
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: setup.implicitHeight + 22
                ColumnLayout {
                    id: setup
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 11
                    spacing: 8

                    AText {
                        text: "Mount"
                        color: theme.accent
                        font.pixelSize: 12
                        weight: 600
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        AstroButton {
                            glyph: "left"
                            implicitWidth: 46
                            implicitHeight: 44
                            onClicked: page.driverIndex = (page.driverIndex + mount.drivers.length - 1) % mount.drivers.length
                        }
                        AText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: page.selectedDriver.label
                            font.pixelSize: 15
                            weight: 500
                            elide: Text.ElideRight
                        }
                        AstroButton {
                            glyph: "right"
                            implicitWidth: 46
                            implicitHeight: 44
                            onClicked: page.driverIndex = (page.driverIndex + 1) % mount.drivers.length
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.selectedDriver.serial
                        spacing: 8
                        AText { text: "Port"; color: theme.dim; font.pixelSize: 12 }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 36
                            radius: 8
                            color: theme.background
                            border.color: theme.line
                            TextInput {
                                id: portBox
                                anchors.fill: parent
                                anchors.margins: 8
                                verticalAlignment: TextInput.AlignVCenter
                                text: mount.port
                                color: theme.text
                                font.family: theme.monoFamily
                                font.pixelSize: 13
                            }
                        }
                    }
                    AText {
                        visible: page.servoSelected
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.dim
                        font.pixelSize: 12
                        text: "RC servos are for pointing and visual use, and aren't accurate enough to track long exposures."
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        AText {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: mount.mountConnected ? theme.good : theme.dim
                            font.pixelSize: 12
                            text: !mount.serverUp ? "Starting the device service…"
                                : !mount.mountFound ? "Waiting for the driver…"
                                : mount.deviceName + (mount.mountConnected ? " connected" : " ready")
                        }
                        AstroButton {
                            text: "Apply"
                            implicitHeight: 44
                            onClicked: mount.applyProfile(page.selectedDriver.binary, portBox.text, mount.remoteAccess)
                        }
                        AstroButton {
                            text: mount.mountConnected ? "Disconnect" : "Connect"
                            style: mount.mountConnected ? "plain" : "primary"
                            implicitHeight: 44
                            enabled: mount.mountFound
                            onClicked: mount.mountConnected ? mount.disconnectMount() : mount.connectMount()
                        }
                    }
                }
            }

            // Where it's pointing
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 2

                    AText {
                        text: "Pointing at"
                        color: theme.accent
                        font.pixelSize: 12
                        weight: 600
                    }
                    AText {
                        Layout.topMargin: 4
                        style: "display"
                        weight: 600
                        font.pixelSize: 22
                        text: !mount.mountConnected ? "Not connected"
                            : mount.parked ? "Parked"
                            : mount.slewing ? "Slewing"
                            : mount.tracking ? "Tracking" : "Stopped"
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        spacing: 22
                        Column {
                            spacing: 1
                            AText { text: "Right ascension"; color: theme.dim; font.pixelSize: 11 }
                            AText {
                                text: mount.mountConnected ? page.degToHms(mount.ra) : "––h ––m ––s"
                                style: "data"
                                font.pixelSize: 20
                            }
                        }
                        Column {
                            spacing: 1
                            AText { text: "Declination"; color: theme.dim; font.pixelSize: 11 }
                            AText {
                                text: mount.mountConnected ? page.degToDms(mount.dec) : "––° ––′ ––″"
                                style: "data"
                                font.pixelSize: 20
                            }
                        }
                    }
                    AText {
                        visible: mount.message.length > 0
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: theme.danger
                        font.pixelSize: 12
                        text: mount.message
                    }
                }
            }
        }

        // Moving it
        Card {
            Layout.preferredWidth: 232
            Layout.fillHeight: true
            opacity: mount.mountConnected ? 1.0 : 0.45
            enabled: mount.mountConnected

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 11
                spacing: 10

                AText {
                    text: "Move"
                    color: theme.accent
                    font.pixelSize: 12
                    weight: 600
                }

                GridLayout {
                    Layout.alignment: Qt.AlignHCenter
                    columns: 3
                    rowSpacing: 5
                    columnSpacing: 5
                    Item { width: 56; height: 1 }
                    AstroButton { implicitWidth: 56; implicitHeight: 52; glyph: "up"; onPressed: mount.moveStart("N"); onReleased: mount.moveStop() }
                    Item { width: 56; height: 1 }
                    AstroButton { implicitWidth: 56; implicitHeight: 52; glyph: "left"; onPressed: mount.moveStart("E"); onReleased: mount.moveStop() }
                    AstroButton { implicitWidth: 56; implicitHeight: 52; glyph: "stop"; style: "danger"; onClicked: mount.abort() }
                    AstroButton { implicitWidth: 56; implicitHeight: 52; glyph: "right"; onPressed: mount.moveStart("W"); onReleased: mount.moveStop() }
                    Item { width: 56; height: 1 }
                    AstroButton { implicitWidth: 56; implicitHeight: 52; glyph: "down"; onPressed: mount.moveStart("S"); onReleased: mount.moveStop() }
                    Item { width: 56; height: 1 }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    AstroButton {
                        Layout.fillWidth: true
                        text: "Tracking"
                        active: mount.tracking
                        onClicked: mount.setTracking(!mount.tracking)
                    }
                    AstroButton {
                        Layout.fillWidth: true
                        text: mount.parked ? "Unpark" : "Park"
                        onClicked: mount.parked ? mount.unpark() : mount.park()
                    }
                }
            }
        }
    }
}
