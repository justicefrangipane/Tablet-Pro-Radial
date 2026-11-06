import QtQuick
import QtQuick.Controls
import QtQuick.Window
import TabletPro 1.0
Window {
    id: overlay
    objectName: "runtimeOverlay"
    width: 720; height: 600
    visible: false
    color: "transparent"
    title: "Tablet Pro Radial"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    signal quitRequested()
    property bool menuOpen: false
    readonly property bool morphing: studio.runtime.transitioning
    readonly property bool childrenVisible: surface.visualBounds.width > 0
    property string resultMessage: ""
    component OptionSwitch: Switch {
        id: option
        width: 178; height: 34
        focusPolicy: Qt.NoFocus
        rightPadding: 48
        indicator: Rectangle {
            x: option.width-width; y: (option.height-height)/2
            width: 38; height: 22; radius: 11
            color: option.checked ? "#426687" : "#24384c"
            border.color: option.checked ? "#7badd7" : "#50667c"
            Rectangle {
                x: option.checked ? 19 : 3; y: 3; width: 16; height: 16; radius: 8
                color: option.checked ? "#c8e3ff" : "#8da3b9"
                Behavior on x { NumberAnimation { duration: studio.reducedMotion ? 0 : 130; easing.type: Easing.OutCubic } }
            }
        }
        contentItem: Text { text: option.text; color: "#dfebf7"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
    }
    onVisibleChanged: {
        menuOpen = false; resultMessage = "";
        if (visible) studio.runtime.setViewport(surface.width,surface.height);
        else studio.runtime.cancel();
    }
    Shortcut { sequence: "Escape"; onActivated: { overlay.menuOpen=false; studio.runtime.cancel(); } }
    Connections {
        target: studio.runtime
        function onActionReported(description) { overlay.resultMessage=description; resultTimer.restart(); }
        function onStateChanged() { if (studio.runtime.active) overlay.menuOpen=false; }
    }
    Connections {
        target: studio
        function onChanged() {
            if (overlay.visible && studio.platformStatus.length > 0) {
                overlay.resultMessage = studio.platformStatus;
                resultTimer.restart();
            }
        }
    }
    Timer { id: resultTimer; interval: 3600; onTriggered: overlay.resultMessage="" }
    RadialSurface {
        id: surface
        objectName: "runtimeSurface"
        anchors.fill: parent
        buttons: studio.runtime.buttons
        childButtons: studio.runtime.childButtons
        dockProgress: studio.runtime.dockProgress
        selectedId: studio.runtime.selectedId
        reducedMotion: studio.reducedMotion
        glassEnabled: studio.backdropEnabled
        onWidthChanged: studio.runtime.setViewport(width,height)
        onHeightChanged: studio.runtime.setViewport(width,height)
        Component.onCompleted: studio.runtime.setViewport(width,height)
        onPressed: (pointerId,x,y) => { overlay.menuOpen=false; overlay.resultMessage=""; studio.runtime.pointerPressed(pointerId,x,y); }
        onMoved: (pointerId,x,y) => studio.runtime.pointerMoved(pointerId,x,y)
        onReleased: (pointerId,x,y) => studio.runtime.pointerReleased(pointerId,x,y)
        onCanceled: studio.runtime.cancel()
    }
    Rectangle {
        id: hub
        objectName: "overlayToolbar"
        x: studio.runtime.docked ? 3 : studio.runtime.radialCenter.x-width/2
        y: studio.runtime.docked ? Math.max(4,parent.height/2-230) : studio.runtime.radialCenter.y-height/2
        width: 76; height: 76; radius: 38
        visible: !studio.runtime.active && !overlay.morphing && !overlay.childrenVisible
        gradient: Gradient {
            GradientStop { position: 0; color: studio.backdropEnabled ? "#702c3a4c" : "#d92c3a4c" }
            GradientStop { position: 1; color: studio.backdropEnabled ? "#98182432" : "#e0182432" }
        }
        border.color: "#45647d"; border.width: 0.8
        HubButton {
            objectName: "dockModeButton"
            x: 8; y: 10
            glyph: studio.runtime.docked ? "radial" : "dock"
            hint: studio.runtime.docked ? "Send to radial" : "Send to dock"
            reduceMotion: studio.reducedMotion
            onClicked: { overlay.menuOpen=false; studio.runtime.toggleDock(); }
        }
        HubButton {
            objectName: "editMenuButton"
            x: 38; y: 10
            glyph: "edit"; hint: "Edit menu and pen button"
            reduceMotion: studio.reducedMotion
            onClicked: { overlay.menuOpen=false; studio.showEditor(); }
        }
        HubButton {
            objectName: "moreMenuButton"
            x: 23; y: 40
            glyph: "more"; hint: "Sound, glass, hide, and quit"
            reduceMotion: studio.reducedMotion
            onClicked: overlay.menuOpen=!overlay.menuOpen
        }
    }
    Rectangle {
        id: morePanel
        objectName: "overlayMorePanel"
        visible: overlay.menuOpen && hub.visible
        x: studio.runtime.docked ? 91 : (studio.runtime.radialCenter.x+112+width+8 < overlay.width ? studio.runtime.radialCenter.x+112 : Math.max(8,studio.runtime.radialCenter.x-112-width))
        y: Math.max(8,Math.min(overlay.height-height-8,hub.y-10))
        width: 194; height: options.implicitHeight+16; radius: 14
        color: studio.backdropEnabled ? "#b01b2939" : "#f01b2939"; border.color: "#4b6782"
        Column {
            id: options
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
            spacing: 3
            OptionSwitch {
                text: "Sound"; checked: studio.soundEnabled; focusPolicy: Qt.NoFocus
                onToggled: studio.soundEnabled=checked
            }
            OptionSwitch {
                text: "Glass background"; checked: studio.backdropEnabled; focusPolicy: Qt.NoFocus
                onToggled: studio.backdropEnabled=checked
            }
            Repeater {
                model: ["Hide menu","Quit Radial Studio"]
                Button {
                    required property int index
                    required property string modelData
                    width: options.width; height: 34; text: modelData; focusPolicy: Qt.NoFocus
                    onClicked: {
                        overlay.menuOpen=false;
                        if(index===0) studio.hideOverlay();
                        else { studio.showEditor(); overlay.quitRequested(); }
                    }
                    contentItem: Text { text: parent.text; color: "#d8e8f8"; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 8; color: parent.hovered ? "#334b64" : "transparent" }
                }
            }
        }
    }
    Rectangle {
        id: result
        objectName: "overlayResult"
        visible: overlay.resultMessage.length>0 && !overlay.childrenVisible && !overlay.morphing
        width: Math.min(320,overlay.width-24); height: message.implicitHeight+20; radius: 12
        x: studio.runtime.docked ? 12 : Math.max(8,Math.min(overlay.width-width-8,studio.runtime.radialCenter.x-width/2))
        y: Math.min(overlay.height-height-8,studio.runtime.docked ? overlay.height/2+164 : studio.runtime.radialCenter.y+116)
        color: studio.backdropEnabled ? "#b0192635" : "#ee192635"; border.color: "#547593"
        Text {
            id: message
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            text: overlay.resultMessage; color: "#e0edfa"; font.pixelSize: 12; wrapMode: Text.Wrap
        }
    }
}
