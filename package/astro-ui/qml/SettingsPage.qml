import QtQuick
import QtQuick.Layouts

Item {
    id: page

    component Field: RowLayout {
        property alias text: input.text
        property string label
        spacing: 8
        Text { Layout.preferredWidth: 170; text: label; color: theme.dim; font.pixelSize: 14 }
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: theme.panel
            border.color: theme.line
            radius: 4
            TextInput {
                id: input
                anchors.fill: parent
                anchors.margins: 8
                verticalAlignment: TextInput.AlignVCenter
                color: theme.text
                font.pixelSize: 16
                inputMethodHints: Qt.ImhFormattedNumbersOnly
            }
        }
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
                visible: !sky.locationSet
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.text
                font.pixelSize: 16
                text: "Welcome to Astro OS. Enter your location so that it can work out your sky."
            }

            Field { id: lat; label: "Latitude (north +)"; text: sky.locationSet ? sky.latitude : "" }
            Field { id: lon; label: "Longitude (east +)"; text: sky.locationSet ? sky.longitude : "" }
            Field { id: elev; label: "Elevation (m)"; text: sky.locationSet ? sky.elevation : "0" }
            Field { id: offset; label: "Time zone (UTC + hours)"; text: sky.utcOffsetMinutes / 60 }

            Text {
                id: problem
                visible: text.length > 0
                color: theme.danger
                font.pixelSize: 14
                text: ""
            }

            AstroButton {
                text: "Save location"
                onClicked: {
                    var a = parseFloat(lat.text)
                    var o = parseFloat(lon.text)
                    var e = parseFloat(elev.text)
                    var z = parseFloat(offset.text)
                    if (isNaN(a) || a < -90 || a > 90 || isNaN(o) || o < -180 || o > 180) {
                        problem.text = "Latitude must be -90 to 90, and longitude -180 to 180."
                        return
                    }
                    problem.text = ""
                    sky.setLocation(a, o, isNaN(e) ? 0 : e)
                    sky.utcOffsetMinutes = isNaN(z) ? 0 : Math.round(z * 60)
                    root.page = 0
                }
            }

            RowLayout {
                visible: sky.hasBacklight
                spacing: 8
                Text { text: "Brightness"; color: theme.dim; font.pixelSize: 14 }
                Repeater {
                    model: [10, 25, 50, 100]
                    delegate: AstroButton {
                        required property int modelData
                        text: modelData + "%"
                        active: sky.brightness === modelData
                        onClicked: sky.brightness = modelData
                    }
                }
            }

            Text { text: "Solar filter"; color: theme.text; font.pixelSize: 16; font.bold: true; Layout.topMargin: 6 }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.dim
                font.pixelSize: 14
                text: "Pointing at the Sun without a proper solar filter can cause permanent blindness and destroy equipment, so GoTo refuses to go near it. This setting resets every time Astro OS starts."
            }
            AstroButton {
                text: root.solarFilterFitted ? "Solar filter is fitted" : "I have fitted a solar filter"
                active: root.solarFilterFitted
                onClicked: root.solarFilterFitted = !root.solarFilterFitted
            }

            Text { text: "Laptop control"; color: theme.text; font.pixelSize: 16; font.bold: true; Layout.topMargin: 6 }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.dim
                font.pixelSize: 14
                text: "Lets apps such as KStars control this telescope over the network. INDI has no password, so anyone on the network could move it, and the Sun protection on this screen does not apply to those apps."
            }
            AstroButton {
                text: mount.remoteAccess ? "Laptop control is on" : "Laptop control is off"
                active: mount.remoteAccess
                onClicked: mount.applyProfile(mount.driver, mount.port, !mount.remoteAccess)
            }
        }
    }
}
