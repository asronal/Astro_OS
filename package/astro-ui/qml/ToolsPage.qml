import QtQuick
import QtQuick.Layouts

Item {
    id: page

    property double focalLength: 400
    property double aperture: 80
    property double sensorWidth: 22.3
    property double sensorHeight: 14.9
    property double pixelSize: 4.3

    readonly property double fovWidth: 57.2958 * sensorWidth / focalLength
    readonly property double fovHeight: 57.2958 * sensorHeight / focalLength
    readonly property double imageScale: 206.265 * pixelSize / focalLength
    readonly property double fRatio: focalLength / aperture
    readonly property double rule500: 500 / focalLength
    // Simplified NPF rule, from Frederic Michaud's formula
    readonly property double npf: (35 * fRatio + 30 * pixelSize) / focalLength

    property double polarAngle: 0

    function refreshPolar() {
        if (sky.locationSet)
            polarAngle = sky.polarisHourAngle()
    }

    Component.onCompleted: refreshPolar()

    Connections {
        target: sky
        function onTicked() { page.refreshPolar() }
    }

    component Setting: RowLayout {
        property string label
        property double value
        property double step
        signal changed(double updated)
        spacing: 6
        Text { Layout.preferredWidth: 150; text: label; color: theme.dim; font.pixelSize: 14 }
        AstroButton { text: "−"; onClicked: changed(Math.max(step, value - step)) }
        Text { Layout.preferredWidth: 70; horizontalAlignment: Text.AlignHCenter; text: value.toFixed(1); color: theme.text; font.pixelSize: 16 }
        AstroButton { text: "+"; onClicked: changed(value + step) }
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

            Text { text: "Telescope and camera"; color: theme.text; font.pixelSize: 16; font.bold: true }
            Setting { label: "Focal length (mm)"; value: page.focalLength; step: 50; onChanged: (updated) => page.focalLength = updated }
            Setting { label: "Aperture (mm)"; value: page.aperture; step: 10; onChanged: (updated) => page.aperture = updated }
            Setting { label: "Sensor width (mm)"; value: page.sensorWidth; step: 1; onChanged: (updated) => page.sensorWidth = updated }
            Setting { label: "Sensor height (mm)"; value: page.sensorHeight; step: 1; onChanged: (updated) => page.sensorHeight = updated }
            Setting { label: "Pixel size (µm)"; value: page.pixelSize; step: 0.1; onChanged: (updated) => page.pixelSize = updated }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.text
                font.pixelSize: 15
                text: "Field of view " + page.fovWidth.toFixed(2) + "° × " + page.fovHeight.toFixed(2) + "°\n"
                      + "Image scale " + page.imageScale.toFixed(2) + " arcsec/pixel, f/" + page.fRatio.toFixed(1) + "\n"
                      + "Longest exposure without tracking: " + page.rule500.toFixed(1) + " s (500 rule), "
                      + page.npf.toFixed(1) + " s (NPF rule, sharper)"
            }

            Text { text: "Polar alignment"; color: theme.text; font.pixelSize: 16; font.bold: true; Layout.topMargin: 8 }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: theme.dim
                font.pixelSize: 14
                text: sky.locationSet
                      ? "Polaris hour angle is " + (page.polarAngle / 15).toFixed(2) + " h. In a polar scope with 0 h at the top, turning clockwise as hours increase, place Polaris at that position. Check how your own polar scope is marked."
                      : "Set your location in Settings first."
            }
        }
    }
}
