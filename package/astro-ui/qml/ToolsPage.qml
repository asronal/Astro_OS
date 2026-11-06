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
        property int decimals: 0
        signal changed(double updated)
        Layout.fillWidth: true
        spacing: 4
        AText { Layout.fillWidth: true; text: label; color: theme.dim; font.pixelSize: 12 }
        AstroButton { implicitWidth: 46; implicitHeight: 42; glyph: "minus"; onClicked: changed(Math.max(step, value - step)) }
        AText {
            Layout.preferredWidth: 54
            horizontalAlignment: Text.AlignHCenter
            text: value.toFixed(decimals)
            style: "data"
            font.pixelSize: 14
        }
        AstroButton { implicitWidth: 46; implicitHeight: 42; glyph: "plus"; onClicked: changed(value + step) }
    }

    component Result: Column {
        property string label
        property string value
        spacing: 0
        AText { text: label; color: theme.dim; font.pixelSize: 11 }
        AText { text: value; style: "data"; font.pixelSize: 17 }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Card {
            Layout.preferredWidth: 306
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 11
                spacing: 4
                AText {
                    text: "Your equipment"
                    color: theme.accent
                    font.pixelSize: 12
                    weight: 600
                    Layout.bottomMargin: 8
                }
                Setting { label: "Focal length, mm"; value: page.focalLength; step: 50; onChanged: (updated) => page.focalLength = updated }
                Setting { label: "Aperture, mm"; value: page.aperture; step: 10; onChanged: (updated) => page.aperture = updated }
                Setting { label: "Sensor width, mm"; value: page.sensorWidth; step: 0.5; decimals: 1; onChanged: (updated) => page.sensorWidth = updated }
                Setting { label: "Sensor height, mm"; value: page.sensorHeight; step: 0.5; decimals: 1; onChanged: (updated) => page.sensorHeight = updated }
                Setting { label: "Pixel size, µm"; value: page.pixelSize; step: 0.1; decimals: 1; onChanged: (updated) => page.pixelSize = updated }
                Item { Layout.fillHeight: true }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 118
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 6
                    AText {
                        text: "What you'll get"
                        color: theme.accent
                        font.pixelSize: 12
                        weight: 600
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 3
                        rowSpacing: 2
                        columnSpacing: 14
                        Result { label: "Field of view"; value: page.fovWidth.toFixed(2) + "×" + page.fovHeight.toFixed(2) + "°" }
                        Result { label: "Image scale"; value: page.imageScale.toFixed(2) + "″/px" }
                        Result { label: "Focal ratio"; value: "f/" + page.fRatio.toFixed(1) }
                        Result { label: "No tracking (500)"; value: page.rule500.toFixed(1) + " s" }
                        Result { label: "No tracking (NPF)"; value: page.npf.toFixed(1) + " s" }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 12
                    PolarDial {
                        Layout.preferredWidth: size
                        Layout.preferredHeight: size
                        size: 118
                        hourAngle: page.polarAngle
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        AText {
                            text: "Polar alignment"
                            color: theme.accent
                            font.pixelSize: 12
                            weight: 600
                        }
                        AText {
                            visible: sky.locationSet
                            text: (page.polarAngle / 15).toFixed(2) + " h"
                            style: "data"
                            font.pixelSize: 22
                        }
                        AText {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: theme.dim
                            font.pixelSize: 12
                            text: sky.locationSet
                                  ? "Polaris's hour angle, shown as a clock with 0 h at the top. Check how your own polar scope is marked."
                                  : "Set your location in Settings first."
                        }
                    }
                }
            }
        }
    }
}
