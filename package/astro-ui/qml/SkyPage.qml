import QtQuick
import QtQuick.Layouts
import Astro 1.0

Item {
    id: page
    objectName: "skyPage"

    property string result: ""
    property var searchResults: []
    property int searchIndex: 0
    property bool searchOpen: false
    property bool layersOpen: false
    property bool ringsOpen: false
    property bool timeOpen: false
    property bool detailsOpen: false
    property string summary: ""
    property var details: ({})
    // Whole days of the time preview. The slider adds up to 12 hours either way.
    property int previewDays: 0
    readonly property bool hasTarget: root.target.name !== undefined
    readonly property bool previewing: chart.timeOffsetHours !== 0
    readonly property int bottomCardSpace: hasTarget ? 84 : 44
    readonly property int timeStripSpace: timeOpen ? 82 : 0

    function closePanels() {
        searchOpen = false
        layersOpen = false
        ringsOpen = false
        searchBox.text = ""
        searchBox.focus = false
    }

    function toggleSearch() {
        layersOpen = false
        ringsOpen = false
        searchOpen = !searchOpen
        if (searchOpen)
            searchBox.forceActiveFocus()
        else {
            searchBox.text = ""
            searchBox.focus = false
        }
    }

    function chooseResult(index) {
        var chosen = searchResults[index]
        if (chosen === undefined)
            return
        root.selectTarget(chosen)
        chart.centerOnTarget()
        closePanels()
        result = ""
    }

    function togglePanel(name) {
        var opening = !page[name]
        closePanels()
        page[name] = opening
    }

    function selectAtCenter() {
        var object = chart.nextObjectAt(chart.width / 2, chart.height / 2)
        if (object.name !== undefined) {
            root.selectTarget(object)
            result = ""
        }
    }

    // Keeps what's in the middle of the view in the middle, and moves about a tenth of it
    readonly property real panStep: chart.width / 10

    function stepPreview(hours) {
        timeOpen = true
        var total = chart.timeOffsetHours + hours
        var days = Math.round(total / 24)
        setPreview(days, total - days * 24)
    }

    function backToNow() {
        previewDays = 0
        chart.timeOffsetHours = 0
        detailsDelay.restart()
    }

    function setPreview(days, hours) {
        // Days are kept apart so that the slider's handle doesn't jump at its ends
        previewDays = Math.max(-30, Math.min(30, days))
        chart.timeOffsetHours = previewDays * 24 + hours
        detailsDelay.restart()
    }

    function offsetText(hours) {
        var minutes = Math.round(Math.abs(hours) * 60)
        var days = Math.floor(minutes / 1440)
        var h = Math.floor((minutes % 1440) / 60)
        var parts = []
        if (days > 0) parts.push(days + (days === 1 ? " day" : " days"))
        if (h > 0) parts.push(h + " h")
        if (minutes % 60 > 0) parts.push((minutes % 60) + " min")
        return (hours < 0 ? "−" : "+") + parts.join(" ")
    }

    function refreshDetails() {
        summary = hasTarget ? sky.targetSummary(root.target, chart.timeOffsetHours) : ""
        details = hasTarget && detailsOpen ? sky.targetDetails(root.target, chart.timeOffsetHours, root.solarFilterFitted) : ({})
    }

    onDetailsOpenChanged: refreshDetails()
    onHasTargetChanged: if (!hasTarget) detailsOpen = false

    onVisibleChanged: {
        if (visible && hasTarget)
            chart.centerOnTarget()
        if (!visible) {
            // A forgotten preview would make every other page disagree with this one
            closePanels()
            detailsOpen = false
            timeOpen = false
            backToNow()
        }
    }

    Connections {
        target: root
        function onTargetChanged() { page.refreshDetails() }
        function onSolarFilterFittedChanged() { page.refreshDetails() }
        function onTimeControlsRequested() { page.timeOpen = true }
        function onEscapePressed() {
            if (!page.visible)
                return
            if (page.searchOpen || page.layersOpen || page.ringsOpen) {
                page.closePanels()
            } else if (page.detailsOpen) {
                page.detailsOpen = false
            } else if (page.timeOpen) {
                page.timeOpen = false
            } else if (page.hasTarget) {
                root.selectTarget({})
                page.result = ""
            } else {
                root.skyFullView = false
            }
        }
    }

    // Keys for this page. Those that a search box needs for itself step aside for it.
    // Some input paths, such as VNC, report a shifted key as Shift plus that key, rather
    // than working out what it means, so those are listed both ways.
    readonly property bool keysActive: visible && !searchBox.activeFocus && !root.helpOpen
    Shortcut { sequence: "Left"; enabled: page.keysActive; onActivated: chart.panBy(page.panStep, 0) }
    Shortcut { sequence: "Right"; enabled: page.keysActive; onActivated: chart.panBy(-page.panStep, 0) }
    Shortcut { sequence: "Up"; enabled: page.keysActive; onActivated: chart.panBy(0, page.panStep) }
    Shortcut { sequence: "Down"; enabled: page.keysActive; onActivated: chart.panBy(0, -page.panStep) }
    Shortcut { sequences: ["+", "=", "Shift++"]; enabled: page.keysActive; onActivated: chart.fieldOfView = chart.fieldOfView / 1.25 }
    Shortcut { sequences: ["-", "_", "Shift+_"]; enabled: page.keysActive; onActivated: chart.fieldOfView = chart.fieldOfView * 1.25 }
    Shortcut { sequences: ["Return", "Enter"]; enabled: page.keysActive; onActivated: page.selectAtCenter() }
    Shortcut { sequences: ["F", "/"]; enabled: page.keysActive; onActivated: page.toggleSearch() }
    Shortcut { sequence: "L"; enabled: page.keysActive; onActivated: page.togglePanel("layersOpen") }
    Shortcut { sequence: "R"; enabled: page.keysActive; onActivated: page.togglePanel("ringsOpen") }
    Shortcut { sequence: "T"; enabled: page.keysActive; onActivated: page.timeOpen = !page.timeOpen }
    Shortcut { sequence: "I"; enabled: page.keysActive && page.hasTarget; onActivated: page.detailsOpen = !page.detailsOpen }
    Shortcut { sequence: "["; enabled: page.keysActive; onActivated: page.stepPreview(-1) }
    Shortcut { sequence: "]"; enabled: page.keysActive; onActivated: page.stepPreview(1) }
    Shortcut { sequences: ["{", "Shift+{"]; enabled: page.keysActive; onActivated: page.stepPreview(-24) }
    Shortcut { sequences: ["}", "Shift+}"]; enabled: page.keysActive; onActivated: page.stepPreview(24) }
    Shortcut { sequence: "Home"; enabled: page.keysActive; onActivated: page.backToNow() }

    Binding {
        target: root
        property: "skyPreviewText"
        value: {
            // Depends on the clock, so that it stays right as time passes
            sky.clockText
            return page.previewing ? "Showing " + sky.previewTimeText(chart.timeOffsetHours) : ""
        }
    }

    // Working out details takes a moment, so wait for the slider to pause
    Timer { id: detailsDelay; interval: 150; onTriggered: page.refreshDetails() }
    // Heights and verdicts change with the time
    Timer { interval: 30000; repeat: true; running: page.visible && page.hasTarget; onTriggered: page.refreshDetails() }

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
        safeBottom: root.navigationHeight + page.bottomCardSpace + page.timeStripSpace
        rings: sky.eyepieces
        showCenterMark: inputMode.keyboardInUse
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
            // One notch of a mouse wheel is 120. Towards the pointer, as maps do it.
            onWheel: (wheel) => chart.zoomAt(wheel.x, wheel.y, Math.pow(0.998, wheel.angleDelta.y))
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
                if (page.searchOpen || page.layersOpen || page.ringsOpen) {
                    page.closePanels()
                    return
                }
                var object = chart.nextObjectAt(mouse.x, mouse.y)
                if (object.name !== undefined) {
                    root.selectTarget(object)
                    page.result = ""
                } else if (page.detailsOpen) {
                    page.detailsOpen = false
                } else {
                    root.skyFullView = !root.skyFullView
                }
            }
        }
    }

    // A switch on a panel, the same for layers and eyepiece rings
    component SwitchRow: Item {
        id: switchRow
        objectName: "switch: " + label
        property string label
        property bool isOn: false
        signal toggled()
        height: 44
        AText {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.right: track.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            font.pixelSize: 14
            text: switchRow.label
        }
        Rectangle {
            id: track
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 42
            height: 24
            radius: 12
            color: switchRow.isOn ? theme.accent : theme.surfaceHigh
            border.color: theme.line
            Rectangle {
                width: 18
                height: 18
                radius: 9
                y: 3
                x: switchRow.isOn ? 21 : 3
                color: switchRow.isOn ? theme.accentInk : theme.dim
                Behavior on x { NumberAnimation { duration: 100 } }
            }
        }
        MouseArea {
            anchors.fill: parent
            activeFocusOnTab: true
            Keys.onShortcutOverride: (event) => event.accepted = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
            Keys.onReturnPressed: switchRow.toggled()
            Keys.onEnterPressed: switchRow.toggled()
            onClicked: switchRow.toggled()
            FocusRing { }
        }
    }

    // Search, layers, time and rings
    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 8

        Card {
            width: 50
            height: 200
            opacity: 0.94
            Column {
                anchors.fill: parent
                AstroButton {
                    width: 50; height: 50; style: "quiet"; glyph: "search"
                    active: page.searchOpen
                    onClicked: page.toggleSearch()
                }
                AstroButton {
                    objectName: "layersButton"
                    width: 50; height: 50; style: "quiet"; glyph: "layers"
                    active: page.layersOpen
                    onClicked: {
                        page.searchOpen = false
                        page.ringsOpen = false
                        page.layersOpen = !page.layersOpen
                    }
                }
                AstroButton {
                    objectName: "timeButton"
                    width: 50; height: 50; style: "quiet"; glyph: "clock"
                    active: page.timeOpen
                    onClicked: page.timeOpen = !page.timeOpen
                }
                AstroButton {
                    objectName: "ringsButton"
                    width: 50; height: 50; style: "quiet"; glyph: "rings"
                    active: page.ringsOpen
                    onClicked: {
                        page.searchOpen = false
                        page.layersOpen = false
                        page.ringsOpen = !page.ringsOpen
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
        visible: !page.hasTarget
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

        MouseArea { anchors.fill: parent }

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
                    activeFocusOnTab: true
                    onTextChanged: {
                        page.searchResults = sky.search(text).slice(0, 5)
                        page.searchIndex = 0
                    }
                    Keys.onDownPressed: page.searchIndex = Math.min(page.searchIndex + 1, page.searchResults.length - 1)
                    Keys.onUpPressed: page.searchIndex = Math.max(page.searchIndex - 1, 0)
                    Keys.onReturnPressed: page.chooseResult(page.searchIndex)
                    Keys.onEnterPressed: page.chooseResult(page.searchIndex)

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
                    required property int index
                    width: searchColumn.width
                    height: 38
                    Rectangle {
                        anchors.fill: parent
                        visible: parent.index === page.searchIndex && inputMode.keyboardInUse
                        color: theme.surfaceHigh
                    }
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
                        onClicked: page.chooseResult(parent.index)
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

        MouseArea { anchors.fill: parent }

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
                delegate: SwitchRow {
                    required property var modelData
                    width: layerColumn.width
                    label: modelData.label
                    isOn: chart[modelData.key]
                    onToggled: chart[modelData.key] = !chart[modelData.key]
                }
            }
        }
    }

    // Eyepiece rings panel
    Card {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 68
        anchors.topMargin: 10
        width: 268
        height: Math.min(ringColumn.implicitHeight + 16, page.height - 20)
        visible: page.ringsOpen
        clip: true

        MouseArea { anchors.fill: parent }

        Flickable {
            anchors.fill: parent
            anchors.margins: 8
            contentHeight: ringColumn.implicitHeight
            interactive: contentHeight > height

            Column {
                id: ringColumn
                width: parent.width
                spacing: 4

                AText {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: sky.telescopeFocalMm <= 0 || sky.eyepieces.length === 0
                    color: theme.dim
                    font.pixelSize: 13
                    leftPadding: 6
                    rightPadding: 6
                    topPadding: 4
                    bottomPadding: 6
                    text: "Rings show how much sky an eyepiece takes in. Add your telescope and eyepieces in Settings to use them."
                }
                AstroButton {
                    visible: sky.telescopeFocalMm <= 0 || sky.eyepieces.length === 0
                    width: parent.width
                    text: "Open Settings"
                    onClicked: root.page = 4
                }
                Repeater {
                    model: sky.telescopeFocalMm > 0 ? sky.eyepieces : []
                    delegate: SwitchRow {
                        required property int index
                        required property var modelData
                        width: ringColumn.width
                        label: modelData.label
                        isOn: modelData.on
                        onToggled: sky.setEyepieceOn(index, !modelData.on)
                    }
                }
            }
        }
    }

    // Time preview
    Card {
        id: timeStrip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.bottomMargin: page.bottomCardSpace + 2
        height: 72
        visible: page.timeOpen
        opacity: 0.96

        // Keeps taps on the gaps from selecting objects behind it
        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            AstroButton {
                text: "−1 day"
                style: "quiet"
                enabled: page.previewDays > -30
                onClicked: page.setPreview(page.previewDays - 1, chart.timeOffsetHours - page.previewDays * 24)
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 6
                Layout.bottomMargin: 2
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    AText {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 14
                        weight: 600
                        text: {
                            sky.clockText
                            return sky.previewTimeText(chart.timeOffsetHours)
                        }
                    }
                    AText {
                        visible: page.previewing
                        color: theme.dim
                        font.pixelSize: 12
                        text: page.offsetText(chart.timeOffsetHours)
                    }
                }

                // The middle is the same time of day as now
                Item {
                    id: slider
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    readonly property real hours: Math.max(-12, Math.min(12, chart.timeOffsetHours - page.previewDays * 24))
                    activeFocusOnTab: true
                    Keys.onShortcutOverride: (event) => event.accepted = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                    Keys.onLeftPressed: page.setPreview(page.previewDays, Math.max(-12, hours - 1 / 12))
                    Keys.onRightPressed: page.setPreview(page.previewDays, Math.min(12, hours + 1 / 12))
                    FocusRing { }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 4
                        radius: 2
                        color: theme.line
                    }
                    Repeater {
                        model: 5
                        delegate: Rectangle {
                            required property int index
                            x: index * slider.width / 4 - width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: index === 2 ? 16 : 10
                            color: theme.dim
                        }
                    }
                    Rectangle {
                        x: (slider.hours + 12) / 24 * slider.width - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 24
                        radius: 12
                        color: theme.accent
                        border.color: theme.accentInk
                        border.width: 2
                    }
                    MouseArea {
                        objectName: "timeSlider"
                        anchors.fill: parent
                        // Reaches beyond the track, so that it's easy to hit with a finger
                        anchors.topMargin: -8
                        anchors.bottomMargin: -4
                        preventStealing: true
                        // A notch of the wheel is a quarter of an hour
                        onWheel: (wheel) => page.setPreview(page.previewDays, Math.max(-12, Math.min(12, slider.hours + wheel.angleDelta.y / 480)))
                        function moveTo(x) {
                            var hours = Math.max(0, Math.min(1, x / width)) * 24 - 12
                            page.setPreview(page.previewDays, Math.round(hours * 12) / 12)
                        }
                        onPressed: (mouse) => moveTo(mouse.x)
                        onPositionChanged: (mouse) => moveTo(mouse.x)
                    }
                }
            }

            AstroButton {
                text: "+1 day"
                style: "quiet"
                enabled: page.previewDays < 30
                onClicked: page.setPreview(page.previewDays + 1, chart.timeOffsetHours - page.previewDays * 24)
            }
            AstroButton {
                objectName: "nowButton"
                text: "Now"
                enabled: page.previewing
                onClicked: page.backToNow()
            }
        }
    }

    // Everything about the selected object
    Card {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: 68
        anchors.topMargin: 10
        anchors.bottomMargin: page.bottomCardSpace + page.timeStripSpace
        width: Math.min(parent.width - 146, 310)
        visible: page.detailsOpen && page.hasTarget
        clip: true

        MouseArea { anchors.fill: parent }

        Flickable {
            id: detailsScroll
            anchors.fill: parent
            anchors.margins: 12
            contentHeight: detailsColumn.implicitHeight
            interactive: contentHeight > height

            Column {
                id: detailsColumn
                width: parent.width
                spacing: 6

                AText {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    style: "display"
                    weight: 600
                    font.pixelSize: 17
                    text: root.target.name || ""
                }
                AText {
                    width: parent.width
                    visible: text.length > 0
                    color: theme.dim
                    font.pixelSize: 13
                    bottomPadding: 2
                    text: root.target.type || ""
                }
                Repeater {
                    model: page.details.rows || []
                    delegate: Item {
                        required property var modelData
                        width: detailsColumn.width
                        height: Math.max(rowLabel.implicitHeight, rowValue.implicitHeight) + 2
                        AText {
                            id: rowLabel
                            width: 112
                            color: theme.dim
                            font.pixelSize: 13
                            text: modelData.label
                        }
                        AText {
                            id: rowValue
                            anchors.left: rowLabel.right
                            anchors.right: parent.right
                            wrapMode: Text.WordWrap
                            color: modelData.warning ? theme.danger : theme.text
                            font.pixelSize: 13
                            text: modelData.value
                        }
                    }
                }
            }
        }

        // There's no scroll wheel on a touch screen, so show that there's more
        Rectangle {
            visible: detailsScroll.interactive
            anchors.right: parent.right
            anchors.rightMargin: 3
            y: 12 + detailsScroll.visibleArea.yPosition * (parent.height - 24)
            width: 3
            height: detailsScroll.visibleArea.heightRatio * (parent.height - 24)
            radius: 2
            color: theme.dim
        }
    }

    // The selected object
    Card {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        height: 64
        visible: page.hasTarget
        opacity: 0.96

        // A disabled button lets a tap through, which would otherwise select whatever
        // happens to be on the chart behind it
        MouseArea { anchors.fill: parent }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        AText {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: 16
                            weight: 600
                            text: root.target.name || ""
                        }
                        AText {
                            visible: (root.target.cycleCount || 0) > 1
                            color: theme.accent
                            font.pixelSize: 12
                            text: root.target.cycleIndex + " of " + root.target.cycleCount + " here, tap again for next"
                        }
                    }
                    AText {
                        width: parent.width
                        elide: Text.ElideRight
                        color: page.result.length > 0 ? theme.danger : theme.dim
                        font.pixelSize: 12
                        text: page.result.length > 0 ? page.result
                            : page.previewing && mount.mountConnected ? "Showing another time. Go back to now to use the mount."
                            : page.summary
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: page.detailsOpen = !page.detailsOpen
                }
            }
            AstroButton {
                objectName: "infoButton"
                glyph: "info"
                style: "quiet"
                implicitWidth: 46
                active: page.detailsOpen
                onClicked: page.detailsOpen = !page.detailsOpen
            }
            // Positions are worked out again here, as planets keep moving after being
            // selected. Both stay off during a preview, when they'd only cause confusion.
            AstroButton {
                text: "Sync"
                enabled: mount.mountConnected && !page.previewing
                onClicked: {
                    var now = sky.livePosition(root.target)
                    page.result = mount.syncTarget(now.ra, now.dec)
                }
            }
            AstroButton {
                objectName: "gotoButton"
                text: "GoTo"
                style: "primary"
                implicitWidth: 88
                enabled: mount.mountConnected && !page.previewing
                onClicked: {
                    var now = sky.livePosition(root.target)
                    page.result = mount.gotoTarget(now.ra, now.dec, root.solarFilterFitted)
                }
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
