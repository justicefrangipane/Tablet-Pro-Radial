import QtQuick
import QtQuick.Window

Window {
    id: tip
    objectName: "radialHoverTip"
    required property Item anchorItem
    property string text: ""
    property bool requested: false
    property bool ready: false
    property real avoidMargin: 0
    transientParent: anchorItem.Window.window
    screen: transientParent ? transientParent.screen : null
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    width: Math.min(320, Math.max(80, label.implicitWidth + 24), Screen.width - 16)
    height: label.implicitHeight + 20
    function reposition() {
        if (!anchorItem || !transientParent) return
        const top = anchorItem.mapToGlobal(anchorItem.width / 2, -avoidMargin)
        const bottom = anchorItem.mapToGlobal(anchorItem.width / 2, anchorItem.height + avoidMargin)
        x = Math.max(Screen.virtualX + 8, Math.min(top.x - width / 2, Screen.virtualX + Screen.width - width - 8))
        // Prefer completely above the body. If the wheel is at the top edge,
        // use the space below it instead of clamping the tip across its buttons.
        y = top.y - height - 8 >= Screen.virtualY + 8 ? top.y - height - 8 : bottom.y + 8
        y = Math.max(Screen.virtualY + 8, Math.min(y, Screen.virtualY + Screen.height - height - 8))
    }
    visible: ready && requested && text.length > 0 && anchorItem.visible && !!transientParent && transientParent.visible
    onRequestedChanged: if (!requested) ready = false
    Timer { interval: 650; running: tip.requested && !tip.ready; onTriggered: tip.ready = true }
    onReadyChanged: if (ready) reposition()
    Timer { interval: 50; repeat: true; running: tip.visible; onTriggered: tip.reposition() }
    Rectangle {
        anchors.fill: parent; anchors.margins: 1; radius: 8
        color: "#f529211c"; border.color: "#ad8055"
        Text {
            id: label
            x: 11; y: 9; width: tip.width - 24
            text: tip.text; color: "#ffe5bd"; font.family: "Segoe UI"; font.pixelSize: 12
            textFormat: Text.PlainText; wrapMode: Text.Wrap
        }
    }
}
