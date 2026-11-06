import QtQuick
import QtQuick.Controls
Button {
    id: control
    property string glyph: "more"
    property string hint: ""
    property bool reduceMotion: false
    property bool railStyle: false
    property color iconColor: "#e0edfa"
    property color activeIconColor: "#b8dcff"
    property color hoverFill: "#344b66"
    property color pressedFill: "#526b8b"
    property color selectedFill: "#263e54"
    width: 30; height: 30
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: hint
    ToolTip.visible: hovered
    ToolTip.delay: 650
    ToolTip.text: hint
    onGlyphChanged: { icon.requestPaint(); if (!reduceMotion) turn.restart(); }
    onHighlightedChanged: icon.requestPaint()
    onIconColorChanged: icon.requestPaint()
    onActiveIconColorChanged: icon.requestPaint()
    background: Rectangle {
        x: control.railStyle ? 1 : 0
        width: control.width-(control.railStyle ? 2 : 0)
        radius: control.railStyle ? 7 : 10
        color: control.down ? control.pressedFill : control.hovered ? control.hoverFill : control.highlighted ? control.selectedFill : "transparent"
        border.width: control.hovered && !control.railStyle ? 1 : 0
        border.color: "#688ba9"
        Behavior on color { ColorAnimation { duration: control.reduceMotion ? 0 : 90 } }
    }
    contentItem: Canvas {
        id: icon
        implicitWidth: 22; implicitHeight: 22
        opacity: control.enabled ? 1 : 0.35
        onPaint: {
            var c = getContext("2d"); c.reset(); c.translate(width/2,height/2);
            c.strokeStyle = (control.highlighted ? control.activeIconColor : control.iconColor).toString(); c.fillStyle = c.strokeStyle;
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
            } else if (control.glyph === "back") {
                c.beginPath(); c.moveTo(3,-6); c.lineTo(-3,0); c.lineTo(3,6); c.stroke();
            } else if (control.glyph === "sound") {
                c.beginPath(); c.moveTo(-8,-3); c.lineTo(-4,-3); c.lineTo(0,-7); c.lineTo(0,7); c.lineTo(-4,3); c.lineTo(-8,3); c.closePath(); c.stroke();
                c.beginPath(); c.arc(0,0,5,-0.8,0.8); c.stroke();
                if(control.highlighted){c.beginPath(); c.arc(0,0,9,-0.8,0.8); c.stroke();}
            } else if (control.glyph === "glass") {
                c.beginPath(); c.moveTo(0,-8); c.lineTo(9,-3); c.lineTo(0,2); c.lineTo(-9,-3); c.closePath(); c.stroke();
                c.beginPath(); c.moveTo(-8,2); c.lineTo(0,7); c.lineTo(8,2); c.stroke();
            } else if (control.glyph === "hide") {
                c.beginPath(); c.moveTo(-9,0); c.quadraticCurveTo(0,-12,9,0); c.quadraticCurveTo(0,12,-9,0); c.stroke();
                c.beginPath(); c.arc(0,0,3,0,2*Math.PI); c.stroke();
                c.beginPath(); c.moveTo(-8,-8); c.lineTo(8,8); c.stroke();
            } else if (control.glyph === "quit") {
                c.beginPath(); c.moveTo(0,-9); c.lineTo(0,0); c.stroke();
                c.beginPath(); c.arc(0,0,8,-Math.PI/3,Math.PI*4/3); c.stroke();
            } else {
                for (var x=-6;x<=6;x+=6) { c.beginPath(); c.arc(x,0,1.35,0,2*Math.PI); c.fill(); }
            }
        }
    }
    Rectangle {
        visible: control.railStyle && control.highlighted
        width: 3; height: 3; radius: 1.5
        x: parent.width-7; y: (parent.height-height)/2
        color: control.activeIconColor
    }
    NumberAnimation { id: turn; target: icon; property: "rotation"; from: -65; to: 0; duration: 220; easing.type: Easing.OutCubic }
}
