import QtQuick
import QtQuick.Layouts
import Astro 1.0

Item {
    id: page
    objectName: "cameraPage"

    property int driverIndex: {
        for (var i = 0; i < mount.cameraDrivers.length; i++) {
            if (mount.cameraDrivers[i].binary === mount.cameraDriver)
                return i
        }
        return 0
    }
    readonly property var selectedDriver: mount.cameraDrivers[driverIndex]
    property string problem: ""

    function exposureText(seconds) {
        return seconds >= 1 ? String(Math.round(seconds * 100) / 100) : String(seconds)
    }

    component Heading: AText {
        color: theme.accent
        font.pixelSize: 12
        weight: 600
    }

    // A number with a minus and a plus either side, which can also be typed
    component Stepper: ColumnLayout {
        id: stepper
        property string label
        property alias text: valueBox.text
        property string fieldName
        signal minus()
        signal plus()
        signal edited()
        Layout.fillWidth: true
        spacing: 3
        AText { text: stepper.label; color: theme.dim; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            AstroButton { implicitWidth: 44; implicitHeight: 42; glyph: "minus"; style: "quiet"; onClicked: stepper.minus() }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                radius: 8
                color: theme.background
                border.color: valueBox.activeFocus ? theme.accent : theme.line
                TextInput {
                    id: valueBox
                    objectName: stepper.fieldName
                    anchors.fill: parent
                    anchors.margins: 8
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: theme.text
                    font.family: theme.monoFamily
                    font.pixelSize: 14
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    activeFocusOnTab: true
                    onEditingFinished: stepper.edited()
                }
            }
            AstroButton { implicitWidth: 44; implicitHeight: 42; glyph: "plus"; style: "quiet"; onClicked: stepper.plus() }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        // The picture
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                CameraView {
                    objectName: "cameraView"
                    anchors.fill: parent
                    anchors.margins: 2
                    image: camera.previewImage
                }
                AText {
                    anchors.centerIn: parent
                    visible: !camera.hasFrame
                    color: theme.dim
                    font.pixelSize: 14
                    text: camera.cameraConnected ? "No picture yet" : "No camera connected"
                }
                Row {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 6
                    visible: camera.hasFrame
                    AstroButton {
                        objectName: "stretchButton"
                        text: "Stretch"
                        active: camera.stretch
                        onClicked: camera.stretch = !camera.stretch
                    }
                    AstroButton {
                        objectName: "detailButton"
                        text: "Focus"
                        active: camera.showDetail
                        onClicked: camera.showDetail = !camera.showDetail
                    }
                }
            }

            // How the exposure turned out
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.preferredHeight: 74
                visible: camera.hasFrame

                Canvas {
                    id: histogramCanvas
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    width: 150
                    property color barColour: theme.accent
                    onBarColourChanged: requestPaint()
                    Connections {
                        target: camera
                        function onFrameChanged() { histogramCanvas.requestPaint() }
                    }
                    onPaint: {
                        var c = getContext("2d")
                        c.reset()
                        c.fillStyle = barColour
                        var bars = camera.histogram
                        if (bars.length === 0)
                            return
                        var w = width / bars.length
                        for (var i = 0; i < bars.length; i++) {
                            var h = bars[i] * height
                            c.fillRect(i * w, height - h, Math.max(w, 1), h)
                        }
                    }
                }
                Column {
                    anchors.left: histogramCanvas.right
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 2
                    AText {
                        width: parent.width
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        text: camera.statsText
                    }
                    AText {
                        width: parent.width
                        elide: Text.ElideMiddle
                        color: theme.dim
                        font.pixelSize: 12
                        text: camera.lastSaved.length > 0 ? "Saved " + camera.lastSaved.split("/").pop() : ""
                    }
                    AText {
                        width: parent.width
                        elide: Text.ElideRight
                        color: theme.dim
                        font.pixelSize: 12
                        text: camera.freeSpaceText + " in " + camera.captureDirectory
                    }
                }
            }
        }

        // The controls
        Card {
            Layout.preferredWidth: 262
            Layout.fillWidth: false
            Layout.fillHeight: true
            clip: true

            Flickable {
                anchors.fill: parent
                anchors.margins: 10
                contentHeight: controls.implicitHeight
                interactive: contentHeight > height

                ColumnLayout {
                    id: controls
                    width: parent.width
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Heading {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: camera.cameraConnected ? camera.deviceName : "Camera"
                        }
                        AstroButton {
                            objectName: "cameraDisconnectButton"
                            visible: camera.cameraConnected
                            implicitHeight: 34
                            style: "quiet"
                            text: "Disconnect"
                            onClicked: camera.disconnectCamera()
                        }
                    }

                    // Choosing and connecting
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !camera.cameraConnected
                        spacing: 4
                        AstroButton {
                            glyph: "left"
                            implicitWidth: 44
                            onClicked: page.driverIndex = (page.driverIndex + mount.cameraDrivers.length - 1) % mount.cameraDrivers.length
                        }
                        AText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            weight: 500
                            text: page.selectedDriver.label
                        }
                        AstroButton {
                            glyph: "right"
                            implicitWidth: 44
                            onClicked: page.driverIndex = (page.driverIndex + 1) % mount.cameraDrivers.length
                        }
                    }
                    AstroButton {
                        objectName: "cameraApplyButton"
                        Layout.fillWidth: true
                        visible: !camera.cameraConnected && page.selectedDriver.binary !== mount.cameraDriver
                        text: "Use this camera"
                        onClicked: page.problem = mount.applyCameraDriver(page.selectedDriver.binary)
                    }
                    AstroButton {
                        objectName: "cameraConnectButton"
                        Layout.fillWidth: true
                        visible: !camera.cameraConnected && page.selectedDriver.binary === mount.cameraDriver
                        enabled: camera.cameraFound
                        style: "primary"
                        text: "Connect"
                        onClicked: {
                            page.problem = ""
                            camera.connectCamera()
                        }
                    }
                    AText {
                        Layout.fillWidth: true
                        visible: !camera.cameraConnected
                        wrapMode: Text.WordWrap
                        color: theme.dim
                        font.pixelSize: 12
                        text: !camera.serverUp ? "Starting the device service..."
                            : !camera.cameraFound ? "Looking for the camera. Check that it's plugged in and switched on."
                            : "Found " + camera.deviceName + "."
                    }

                    // Taking pictures
                    Stepper {
                        id: exposure
                        visible: camera.cameraConnected
                        label: "Exposure, seconds"
                        fieldName: "exposureField"
                        text: "1"
                        onMinus: text = page.exposureText(camera.nextExposure(parseFloat(text) || 1, -1))
                        onPlus: text = page.exposureText(camera.nextExposure(parseFloat(text) || 1, 1))
                    }
                    Stepper {
                        id: frames
                        visible: camera.cameraConnected
                        label: "Pictures"
                        fieldName: "framesField"
                        text: "1"
                        onMinus: text = String(Math.max((parseInt(text) || 1) - 1, 1))
                        onPlus: text = String(Math.min((parseInt(text) || 0) + 1, 999))
                    }
                    Stepper {
                        id: gain
                        visible: camera.cameraConnected && camera.hasGain
                        label: "Gain, " + camera.gainMin + " to " + camera.gainMax
                        fieldName: "gainField"
                        text: String(camera.gain)
                        onMinus: page.problem = camera.setGain(Math.max(camera.gain - 10, camera.gainMin))
                        onPlus: page.problem = camera.setGain(Math.min(camera.gain + 10, camera.gainMax))
                        onEdited: page.problem = camera.setGain(parseFloat(text))
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: camera.cameraConnected && camera.isoOptions.length > 0
                        spacing: 3
                        AText { text: "ISO"; color: theme.dim; font.pixelSize: 12 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            AstroButton {
                                implicitWidth: 44
                                implicitHeight: 42
                                glyph: "left"
                                style: "quiet"
                                enabled: camera.isoIndex > 0
                                onClicked: camera.setIso(camera.isoIndex - 1)
                            }
                            AText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                style: "data"
                                text: camera.isoIndex >= 0 ? camera.isoOptions[camera.isoIndex] : "-"
                            }
                            AstroButton {
                                implicitWidth: 44
                                implicitHeight: 42
                                glyph: "right"
                                style: "quiet"
                                enabled: camera.isoIndex < camera.isoOptions.length - 1
                                onClicked: camera.setIso(camera.isoIndex + 1)
                            }
                        }
                    }

                    AText {
                        Layout.fillWidth: true
                        visible: camera.cameraConnected && camera.hasTemperature
                        color: theme.dim
                        font.pixelSize: 12
                        text: "Sensor " + camera.temperature.toFixed(1) + " °C"
                    }

                    AstroButton {
                        objectName: "captureButton"
                        Layout.fillWidth: true
                        visible: camera.cameraConnected && !camera.capturing
                        style: "primary"
                        text: "Take " + (parseInt(frames.text) > 1 ? parseInt(frames.text) + " pictures" : "picture")
                        onClicked: {
                            exposure.text = exposure.text.trim()
                            page.problem = camera.capture(parseFloat(exposure.text), parseInt(frames.text))
                        }
                    }
                    AstroButton {
                        objectName: "cancelButton"
                        Layout.fillWidth: true
                        visible: camera.capturing
                        text: "Cancel"
                        onClicked: camera.cancel()
                    }
                    AText {
                        Layout.fillWidth: true
                        visible: camera.capturing
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        text: "Picture " + camera.frameNumber + " of " + camera.frameCount
                              + (camera.secondsLeft > 0 ? ", " + camera.secondsLeft.toFixed(0) + " s left" : ", downloading")
                    }
                    AText {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.WordWrap
                        color: theme.danger
                        font.pixelSize: 12
                        text: page.problem.length > 0 ? page.problem : camera.message
                    }
                }
            }
        }
    }
}
