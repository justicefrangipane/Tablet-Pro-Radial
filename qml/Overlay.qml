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
    readonly property bool railMode: studio.runtime.docked && !morphing
    readonly property real railX: 13
    readonly property real railY: height/2-193.5
    readonly property color settingsInk: "#ffe5bd"
    readonly property color settingsActiveInk: "#ffd08a"
    readonly property color settingsHover: "#6a4c34"
    readonly property color settingsPressed: "#89613d"
    readonly property color settingsSelected: "#604329"
    onMenuOpenChanged: resultMessage=""
    component OptionSwitch: Switch {
        id: option
        width: 178; height: 34
        focusPolicy: Qt.NoFocus
        rightPadding: 48
        indicator: Rectangle {
            x: option.width-width; y: (option.height-height)/2
            width: 38; height: 22; radius: 11
            color: option.checked ? "#94693e" : "#3c2e25"
            border.color: option.checked ? "#e3b47c" : "#866649"
            Rectangle {
                x: option.checked ? 19 : 3; y: 3; width: 16; height: 16; radius: 8
                color: option.checked ? "#ffe5bd" : "#baa58e"
                Behavior on x { NumberAnimation { duration: studio.reducedMotion ? 0 : 130; easing.type: Easing.OutCubic } }
            }
        }
        contentItem: Text { text: option.text; color: overlay.settingsInk; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
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
            if (overlay.visible && !overlay.menuOpen && studio.platformStatus.length > 0) {
                overlay.resultMessage = studio.platformStatus;
                resultTimer.restart();
            }
        }
    }
    Timer { id: resultTimer; interval: 3600; onTriggered: overlay.resultMessage="" }
    Rectangle {
        id: railHousing
        objectName: "overlayRail"
        x: overlay.railX; y: overlay.railY
        width: 44; height: 347; radius: 14
        visible: overlay.railMode
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: overlay.menuOpen ? "#f06b4b35" : studio.backdropEnabled ? "#a0374758" : "#354454" }
            GradientStop { position: 0.3; color: overlay.menuOpen ? "#f050392b" : studio.backdropEnabled ? "#a5222e3c" : "#25313f" }
            GradientStop { position: 1; color: overlay.menuOpen ? "#f52f241f" : studio.backdropEnabled ? "#c019222e" : "#19222e" }
        }
        border.width: 1; border.color: overlay.menuOpen ? "#ad8055" : "#516577"
        Rectangle { x: 1; y: 44; width: 42; height: 1; color: overlay.menuOpen ? "#946b46" : "#465969" }
        Repeater {
            model: 5
            Rectangle {
                required property int index
                x: 5; y: 91.5+index*51; width: 34; height: 1
                color: overlay.menuOpen ? "#71513a" : "#304454"
            }
        }
    }
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
        continuousRail: true
        visible: !(overlay.railMode && overlay.menuOpen)
        enabled: visible
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
        x: studio.runtime.radialCenter.x-width/2
        y: studio.runtime.radialCenter.y-height/2
        width: 76; height: 76; radius: 38
        visible: !studio.runtime.docked && !studio.runtime.active && !overlay.morphing && !overlay.childrenVisible
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
    HubButton {
        objectName: "railSettingsButton"
        x: railHousing.x; y: railHousing.y
        width: 44; height: 44; railStyle: true
        glyph: overlay.menuOpen ? "back" : "more"
        hint: overlay.menuOpen ? "Back to drawing tools" : "Radial menu settings"
        iconColor: overlay.menuOpen ? overlay.settingsInk : "#e0edfa"
        hoverFill: overlay.menuOpen ? overlay.settingsHover : "#344b66"
        pressedFill: overlay.menuOpen ? overlay.settingsPressed : "#526b8b"
        reduceMotion: studio.reducedMotion
        visible: railHousing.visible && !studio.runtime.active && !overlay.childrenVisible
        onClicked: overlay.menuOpen=!overlay.menuOpen
    }
    Item {
        id: railSettings
        objectName: "railSettingsPanel"
        x: railHousing.x; y: railHousing.y+44
        width: 44; height: 299
        visible: railHousing.visible && overlay.menuOpen
        Repeater {
            model: [
                { key: "radial", label: "Send to radial" },
                { key: "edit", label: "Edit menu and pen button" },
                { key: "sound", label: "Sound" },
                { key: "glass", label: "Glass background" },
                { key: "hide", label: "Hide menu" },
                { key: "quit", label: "Quit Radial Studio" }
            ]
            HubButton {
                required property int index
                required property var modelData
                objectName: "railAction_"+modelData.key
                x: 0; y: index*51; width: 44; height: 44
                glyph: modelData.key
                hint: modelData.label + (modelData.key==="sound" ? (studio.soundEnabled ? " (on)" : " (off)") : modelData.key==="glass" ? (studio.backdropEnabled ? " (on)" : " (off)") : "")
                railStyle: true
                iconColor: overlay.settingsInk
                activeIconColor: overlay.settingsActiveInk
                hoverFill: overlay.settingsHover
                pressedFill: overlay.settingsPressed
                selectedFill: overlay.settingsSelected
                reduceMotion: studio.reducedMotion
                highlighted: modelData.key==="sound" ? studio.soundEnabled : modelData.key==="glass" ? studio.backdropEnabled : false
                onClicked: {
                    if(modelData.key==="sound") { studio.soundEnabled=!studio.soundEnabled; return; }
                    if(modelData.key==="glass") { studio.backdropEnabled=!studio.backdropEnabled; return; }
                    overlay.menuOpen=false
                    if(modelData.key==="radial") studio.runtime.toggleDock()
                    else if(modelData.key==="edit") studio.showEditor()
                    else if(modelData.key==="hide") studio.hideOverlay()
                    else { studio.showEditor(); overlay.quitRequested(); }
                }
            }
        }
    }
    Rectangle {
        id: morePanel
        objectName: "overlayMorePanel"
        visible: overlay.menuOpen && hub.visible
        x: studio.runtime.radialCenter.x+112+width+8 < overlay.width ? studio.runtime.radialCenter.x+112 : Math.max(8,studio.runtime.radialCenter.x-112-width)
        y: Math.max(8,Math.min(overlay.height-height-8,hub.y-10))
        width: 194; height: options.implicitHeight+16; radius: 14
        color: "#f03b2d24"; border.color: "#ad8055"
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
                    contentItem: Text { text: parent.text; color: overlay.settingsInk; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 8; color: parent.hovered ? overlay.settingsHover : "transparent" }
                }
            }
        }
    }
    Rectangle {
        id: result
        objectName: "overlayResult"
        visible: overlay.resultMessage.length>0 && !overlay.menuOpen && !overlay.childrenVisible && !overlay.morphing
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
