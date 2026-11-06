import QtQuick

// Text in the Astro OS type styles: "body", "data" for numbers, and "display"
Text {
    property string style: "body"
    property int weight: 400

    color: theme.text
    font.family: style === "data" ? theme.monoFamily : (style === "display" ? theme.displayFamily : theme.sansFamily)
    font.pixelSize: 14
    font.weight: weight
    font.variableAxes: style === "data" ? ({}) : ({ "wght": weight })
    renderType: Text.QtRendering
}
