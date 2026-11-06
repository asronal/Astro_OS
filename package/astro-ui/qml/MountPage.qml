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

    function degToHms(deg) {
        var hours = deg / 15.0
        var h = Math.floor(hours)
        var m = Math.floor((hours - h) * 60)
        var s = Math.floor(((hours - h) * 60 - m) * 60)
        return h + "h " + m + "m " + s + "s"
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 8
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 8

            Text {
                text: "Mount type"
                color: theme.dim
                font.pixelSize: 14
            }

            RowLayout {
                spacing: 6
                AstroButton {
                    text: "<"
                    onClicked: page.driverIndex = (page.driverIndex + mount.drivers.length - 1) % mount.drivers.length
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: page.selectedDriver.label
                    color: theme.text
                    font.pixelSize: 17
                    elide: Text.ElideRight
                }
                AstroButton {
                    text: ">"
                    onClicked: page.driverIndex = (page.driverIndex + 1) % mount.drivers.length
                }
                AstroButton {
                    text: "Apply"
                    onClicked: mount.applyProfile(page.selectedDriver.binary, portBox.text, mount.remoteAccess)
                }
            }

            RowLayout {
                visible: page.selectedDriver.serial
                spacing: 8
                Text { text: "Serial port"; color: theme.dim; font.pixelSize: 14 }
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: theme.panel
                    border.color: theme.line
                    radius: 4
                    TextInput {
                        id: portBox
                        anchors.fill: parent
                        anchors.margins: 8
                        verticalAlignment: TextInput.AlignVCenter
                        text: mount.port
                        color: theme.text
                        font.pixelSize: 16
                    }
                }
            }

            RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: theme.text
                    font.pixelSize: 15
                    text: !mount.serverUp ? "Starting the device service..."
                        : !mount.mountFound ? "Waiting for the mount driver..."
                        : mount.deviceName + (mount.mountConnected ? " connected" : " not connected")
                }
                AstroButton {
                    text: mount.mountConnected ? "Disconnect" : "Connect"
                    enabled: mount.mountFound
                    onClicked: mount.mountConnected ? mount.disconnectMount() : mount.connectMount()
                }
            }

            Text {
                visible: mount.message.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.danger
                font.pixelSize: 14
                text: mount.message
            }

            Text {
                visible: page.selectedDriver.binary === "indi_astro_servo"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.dim
                font.pixelSize: 13
                text: "RC servos are for pointing and visual use. They are not accurate enough to track during long exposures."
            }

            Text {
                visible: mount.mountConnected
                color: theme.text
                font.pixelSize: 16
                text: "Pointing at RA " + page.degToHms(mount.ra) + "   Dec " + mount.dec.toFixed(2) + "°"
            }

            RowLayout {
                visible: mount.mountConnected
                spacing: 16

                GridLayout {
                    columns: 3
                    rowSpacing: 4
                    columnSpacing: 4
                    Item { width: 1; height: 1 }
                    AstroButton { text: "N"; onPressed: mount.moveStart("N"); onReleased: mount.moveStop() }
                    Item { width: 1; height: 1 }
                    AstroButton { text: "E"; onPressed: mount.moveStart("E"); onReleased: mount.moveStop() }
                    AstroButton { text: "Stop"; onClicked: mount.abort() }
                    AstroButton { text: "W"; onPressed: mount.moveStart("W"); onReleased: mount.moveStop() }
                    Item { width: 1; height: 1 }
                    AstroButton { text: "S"; onPressed: mount.moveStart("S"); onReleased: mount.moveStop() }
                    Item { width: 1; height: 1 }
                }

                ColumnLayout {
                    spacing: 6
                    AstroButton {
                        text: mount.tracking ? "Tracking on" : "Tracking off"
                        active: mount.tracking
                        onClicked: mount.setTracking(!mount.tracking)
                    }
                    AstroButton {
                        text: mount.parked ? "Unpark" : "Park"
                        onClicked: mount.parked ? mount.unpark() : mount.park()
                    }
                }
            }
        }
    }
}
