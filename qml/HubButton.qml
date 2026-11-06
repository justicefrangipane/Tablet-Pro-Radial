import QtQuick
import QtQuick.Controls
Button {
    id: control
    property string glyph: "more"
    property string hint: ""
    property bool reduceMotion: false
    width: 30; height: 30
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: hint
    ToolTip.visible: hovered
    ToolTip.delay: 650
    ToolTip.text: hint
    onGlyphChanged: { icon.requestPaint(); if (!reduceMotion) turn.restart(); }
    background: Rectangle {
        radius: 10
        color: control.down ? "#526b8b" : control.hovered ? "#344b66" : "transparent"
        border.width: control.hovered ? 1 : 0
        border.color: "#688ba9"
        Behavior on color { ColorAnimation { duration: control.reduceMotion ? 0 : 90 } }
    }
    contentItem: Canvas {
        id: icon
        implicitWidth: 22; implicitHeight: 22
        opacity: control.enabled ? 1 : 0.35
        onPaint: {
            var c = getContext("2d"); c.reset(); c.translate(width/2,height/2);
            c.strokeStyle = "#e0edfa"; c.fillStyle = "#e0edfa";
            c.lineWidth = 1.45; c.lineCap = "round"; c.lineJoin = "round";
            if (control.glyph === "dock") {
                c.beginPath(); c.moveTo(-8,-7); c.lineTo(-8,7); c.stroke();
                c.beginPath(); c.moveTo(8,0); c.lineTo(-4,0);
                c.moveTo(0,-4); c.lineTo(-4,0); c.lineTo(0,4); c.stroke();
            } else if (control.glyph === "radial") {
                for (var i=0;i<6;++i) { var a=i*Math.PI/3-Math.PI/2;
                    c.beginPath(); c.arc(0,0,7.5,a-0.38,a+0.38); c.stroke(); }
                c.beginPath(); c.arc(0,0,2,0,2*Math.PI); c.stroke();
            } else if (control.glyph === "edit") {
                c.beginPath(); c.moveTo(-7,7); c.lineTo(-5,1); c.lineTo(3,-7);
                c.quadraticCurveTo(5,-9,7,-6); c.lineTo(7,-4); c.lineTo(-1,4); c.closePath(); c.stroke();
                c.beginPath(); c.moveTo(2,-6); c.lineTo(6,-2); c.moveTo(-5,1); c.lineTo(-1,4); c.stroke();
            } else {
                for (var x=-6;x<=6;x+=6) { c.beginPath(); c.arc(x,0,1.35,0,2*Math.PI); c.fill(); }
            }
        }
    }
    NumberAnimation { id: turn; target: icon; property: "rotation"; from: -65; to: 0; duration: 220; easing.type: Easing.OutCubic }
}
