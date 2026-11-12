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
    property bool selectionDismissal: false
    readonly property bool morphing: studio.runtime.transitioning
    onMorphingChanged: { if(morphing){menuOpen=false;settingsBloomAnimation.stop();bloomProgress=0;} }
    readonly property bool childrenVisible: surface.visualBounds.width > 0 || studio.runtime.parentMenuOpen
    property string resultMessage: ""
    readonly property bool railMode: studio.runtime.docked && !morphing
    readonly property real railX: studio.runtime.dockBounds.x
    readonly property real railY: studio.runtime.dockBounds.y
    readonly property color settingsInk: "#ffe5bd"
    readonly property color settingsActiveInk: "#ffd08a"
    readonly property color settingsHover: "#6a4c34"
    readonly property color settingsPressed: "#89613d"
    readonly property color settingsSelected: "#604329"
    property real bloomProgress: 0
    readonly property bool settingsReady: menuOpen && bloomProgress >= 1
    readonly property bool settingsVisible: menuOpen || bloomProgress > 0
    readonly property bool toolsReady: !menuOpen && bloomProgress <= 0
    readonly property bool instantSettings: studio.reducedMotion || radialBloom.softwareRenderer
    readonly property int settingsDuration: railMode ? 360 : 420
    readonly property bool rippleAllowed: !railMode && !morphing && !instantSettings
    readonly property bool centerHeld: visible && hub.visible && centerTap.pressed && !centerDrag.active && !studio.runtime.dragging && !editingButtons
    onCenterHeldChanged: if(centerHeld)studio.playMenuSound("press")
    property real centerPressure: centerHeld && rippleAllowed ? 1 : 0
    Behavior on centerPressure { NumberAnimation { duration: overlay.instantSettings ? 0 : overlay.centerHeld ? 90 : 180; easing.type: Easing.OutCubic } }
    readonly property real centerRippleScale: rippleAllowed ? 1+.08*centerPressure : 1
    readonly property real ringRippleDistance: rippleAllowed ? 6*Math.sin(Math.PI*bloomProgress) : 0
    function smoothProgress(value) { const p=Math.max(0,Math.min(1,value)); return p*p*(3-2*p); }
    function toolButtons() {
        const distance=ringRippleDistance
        return studio.runtime.buttons.map(function(button){
            return Object.assign({},button,{cx:button.cx+distance*Math.cos(button.angle),cy:button.cy+distance*Math.sin(button.angle)})
        })
    }
    onMenuOpenChanged: {
        if(visible&&!morphing)studio.playMenuSound(menuOpen ? "open" : "close")
        resultMessage=""
        const current=bloomProgress
        const goal=menuOpen ? 1 : 0
        settingsBloomAnimation.stop()
        if(menuOpen)studio.runtime.releaseModifiers()
        if(instantSettings || !visible || morphing)bloomProgress=goal
        else if(Math.abs(goal-current)>0.0001){
            settingsBloomAnimation.from=current
            settingsBloomAnimation.to=goal
            settingsBloomAnimation.duration=Math.max(1,Math.round(settingsDuration*Math.abs(goal-current)))
            settingsBloomAnimation.start()
        }else bloomProgress=goal
    }
    onInstantSettingsChanged: {
        if(instantSettings){settingsBloomAnimation.stop();bloomProgress=menuOpen ? 1 : 0;}
    }
    NumberAnimation {
        id: settingsBloomAnimation
        objectName: "settingsBloomAnimation"
        target: overlay; property: "bloomProgress"
        from: 0; to: 1; duration: 420
        easing.type: Easing.Linear
    }
    property bool editingButtons: false
    property string editPressedId: ""
    function activateOption(key) {
        if(key==="sound"){studio.soundEnabled=!studio.soundEnabled;return;}
        if(key==="glass"){studio.backdropEnabled=!studio.backdropEnabled;return;}
        menuOpen=false
        if(key==="dock" || key==="radial")studio.runtime.toggleDock()
        else if(key==="edit"){studio.runtime.releaseModifiers();editingButtons=true;}
        else if(key==="hide")studio.hideOverlay()
        else {studio.showEditor();quitRequested();}
    }
    function options() {
        const keys=["dock","edit","sound","glass","hide","quit"]
        const labels=["Dock","Edit","Sound","Glass","Hide","Quit"]
        return keys.map(function(key,i){
            const angle=-Math.PI/2+i*Math.PI/3
            return {id:key,icon:key,caption:labels[i],slot:i,color:"#ad8055",angle:angle,
                modifierState:(key==="sound"&&studio.soundEnabled)||(key==="glass"&&studio.backdropEnabled)?"held":"",
                cx:108+(70+ringRippleDistance)*Math.cos(angle),cy:108+(70+ringRippleDistance)*Math.sin(angle)}
        })
    }
    onVisibleChanged: {
        menuOpen = false; editingButtons=false; resultMessage = "";
        settingsBloomAnimation.stop();bloomProgress=0;
        if (visible) studio.runtime.setViewport(surface.width,surface.height);
        else if(!selectionDismissal)studio.runtime.cancel();
    }
    Shortcut { sequence: "Escape"; enabled: overlay.visible; onActivated: { overlay.menuOpen=false;overlay.editingButtons=false; studio.runtime.cancel(); } }
    Connections {
        target: studio.runtime
        function onActionReported(description) { overlay.resultMessage=description; resultTimer.restart(); }
        function onStateChanged() { if (studio.runtime.active) overlay.menuOpen=false; }
        function onSelectionCommitted() {
            if(studio.runtime.docked)return
            // A completed selection hides the wheel without canceling the
            // modifier just chosen. Explicit Hide/trigger still cancels it.
            selectionDismissal=true
            overlay.hide()
            selectionDismissal=false
        }
    }
    Connections {
        target: studio
        function onShowRuntimeRequested(target, toggle) {
            overlay.menuOpen=false;overlay.editingButtons=false
            settingsBloomAnimation.stop();overlay.bloomProgress=0
        }
        function onChanged() {
            if (overlay.visible && !overlay.menuOpen && studio.platformStatus.length > 0) {
                overlay.resultMessage = studio.platformStatus;
                resultTimer.restart();
            }
        }
    }
    Timer { id: resultTimer; interval: 3600; onTriggered: overlay.resultMessage="" }
    Item {
        id: railHousing
        objectName: "overlayRail"
        x: overlay.railX; y: overlay.railY
        width: studio.runtime.dockBounds.width; height: studio.runtime.dockBounds.height
        visible: overlay.railMode || overlay.morphing
        opacity: overlay.morphing ? overlay.smoothProgress((studio.runtime.dockProgress-.72)/.23) : 1
        Rectangle {
        anchors.fill: parent; anchors.margins: 1; radius: 13
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: studio.backdropEnabled ? "#a0374758" : "#354454" }
            GradientStop { position: 0.3; color: studio.backdropEnabled ? "#a5222e3c" : "#25313f" }
            GradientStop { position: 1; color: studio.backdropEnabled ? "#c019222e" : "#19222e" }
        }
        border.width: 1; border.color: "#516577"
        }
        Rectangle { x: 1; y: 44; width: 42; height: 1; color: "#465969" }
        Repeater {
            model: 5
            Rectangle {
                required property int index
                x: 5; y: 91.5+index*51; width: 34; height: 1
                color: "#304454"
            }
        }
    }
    RadialSurface {
        id: surface
        objectName: "runtimeSurface"
        anchors.fill: parent
        buttons: overlay.toolButtons()
        childButtons: studio.runtime.childButtons
        dockProgress: studio.runtime.dockProgress
        selectedId: studio.runtime.selectedId
        reducedMotion: studio.reducedMotion
        glassEnabled: studio.backdropEnabled
        continuousRail: true
        bronzeTheme: false
        visible: overlay.bloomProgress < 1
        opacity: studio.runtime.parentMenuOpen && !studio.runtime.docked ? 0 : 1
        enabled: overlay.toolsReady
        onWidthChanged: studio.runtime.setViewport(width,height)
        onHeightChanged: studio.runtime.setViewport(width,height)
        Component.onCompleted: studio.runtime.setViewport(width,height)
        onPressed: (pointerId,x,y) => {
            overlay.resultMessage=""
            if(overlay.editingButtons){overlay.editPressedId=surface.hitRoot(x,y);return;}
            studio.runtime.pointerPressed(pointerId,x,y)
        }
        onEditRequested: (id) => studio.editButton(id)
        onMoved: (pointerId,x,y) => { if(!overlay.editingButtons)studio.runtime.pointerMoved(pointerId,x,y); }
        onReleased: (pointerId,x,y) => {
            if(overlay.editingButtons){
                const id=surface.hitRoot(x,y)
                if(id.length && id===overlay.editPressedId){overlay.editingButtons=false;studio.editButton(id);}
                overlay.editPressedId="";return;
            }
            studio.runtime.pointerReleased(pointerId,x,y)
        }
        onCanceled: if(overlay.visible && !overlay.selectionDismissal)studio.runtime.cancel()
    }
    Rectangle {
        objectName: "radialToolCenter"
        x: hub.x; y: hub.y; width: hub.width; height: hub.height; radius: 38
        scale: overlay.centerRippleScale
        visible: hub.visible && overlay.bloomProgress < 1
        z: -1
        gradient: Gradient {
            GradientStop { position: 0; color: "#354454" }
            GradientStop { position: 0.45; color: "#25313f" }
            GradientStop { position: 1; color: "#19222e" }
        }
        border.color: "#688297";border.width: 1
        Rectangle { anchors.fill: parent;anchors.margins: 4;radius: width/2;color: "transparent";border.color: "#40516a7f" }
    }
    BloomLayer {
        id: radialBloom
        objectName: "radialSettingsBloom"
        x: studio.runtime.radialCenter.x-108
        y: studio.runtime.radialCenter.y-108
        width: 216; height: 216
        visible: overlay.settingsVisible && !overlay.railMode
        progress: overlay.bloomProgress
        enabled: overlay.settingsReady
        RadialSurface {
            id: settingsSurface
            objectName: "settingsSurface"
            anchors.fill: parent
            buttons: overlay.options()
            bronzeTheme: true
            glassEnabled: studio.backdropEnabled
            reducedMotion: studio.reducedMotion
            property string pressedId: ""
            onPressed: (pointerId,x,y) => { pressedId=hitRoot(x,y);selectedId=pressedId; }
            onMoved: (pointerId,x,y) => { selectedId=hitRoot(x,y)===pressedId?pressedId:""; }
            onReleased: (pointerId,x,y) => { const id=hitRoot(x,y);selectedId="";if(id.length&&id===pressedId)overlay.activateOption(id);pressedId=""; }
            onCanceled: {selectedId="";pressedId="";}
        }
        Rectangle {
            objectName: "radialSettingsCenter"
            x: 70; y: 70; width: 76; height: 76; radius: 38
            scale: overlay.centerRippleScale
            gradient: Gradient {
                GradientStop { position: 0; color: "#956b46" }
                GradientStop { position: 0.45; color: "#583e2d" }
                GradientStop { position: 1; color: "#30241e" }
            }
            border.color: "#d4ab78";border.width: 1
            Rectangle { anchors.fill: parent;anchors.margins: 4;radius: width/2;color: "transparent";border.color: "#407f5b3e" }
        }
    }
    Item {
        objectName: "settingsRippleEnvelope"
        x: studio.runtime.radialCenter.x-108; y: studio.runtime.radialCenter.y-108
        width: 216; height: 216
        visible: overlay.visible && overlay.rippleAllowed && (overlay.centerPressure > .001 || (overlay.bloomProgress > 0 && overlay.bloomProgress < 1))
    }
    Item {
        id: hub
        objectName: "overlayToolbar"
        x: studio.runtime.radialCenter.x-width/2
        y: studio.runtime.radialCenter.y-height/2
        width: 76; height: 76
        visible: !studio.runtime.docked && !studio.runtime.active && !overlay.morphing && !overlay.childrenVisible
        containmentMask: QtObject {
            function contains(point: point): bool { return Math.pow(point.x-38,2)+Math.pow(point.y-38,2)<=38*38; }
        }
        Accessible.role: Accessible.Button
        Accessible.name: centerButton.hint
        HubButton {
            id: centerButton
            objectName: "moreMenuButton"
            anchors.fill: parent
            padding: 0
            centerStyle: true
            enabled: false
            dimWhenDisabled: false
            glyph: overlay.menuOpen || overlay.editingButtons ? "back" : "more"
            hint: overlay.editingButtons ? "Tap to cancel editing · drag to move" : overlay.menuOpen ? "Tap to return to tools · drag to move" : "Tap for settings · drag to move"
            iconColor: overlay.settingsInk
            hoverFill: "#306a4c34";pressedFill: "#807b5537"
            reduceMotion: studio.reducedMotion
            onClicked: {if(overlay.editingButtons)overlay.editingButtons=false;else overlay.menuOpen=!overlay.menuOpen;}
        }
        HoverHandler { id: centerHover; cursorShape: centerDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
        HoverTip { objectName: "centerHoverTip"; anchorItem: hub; avoidMargin: 63; text: centerButton.hint; requested: centerHover.hovered && !centerTap.pressed && !centerDrag.active }
        TapHandler {
            id: centerTap
            objectName: "radialCenterTap"
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            longPressThreshold: 0
            enabled: !centerDrag.active
            onTapped: centerButton.clicked()
        }
        RadialDragHandler {
                id: centerDrag
                objectName: "radialCenterDrag"
                session: studio.runtime
        }
    }
    BloomLayer {
        id: railBloom
        objectName: "railSettingsBloom"
        x: railHousing.x; y: railHousing.y
        width: 44; height: 347
        visible: railHousing.visible && overlay.settingsVisible
        progress: overlay.bloomProgress
        docked: true
        Rectangle {
            anchors.fill: parent;anchors.margins: 1;radius: 13
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: studio.backdropEnabled ? "#f06b4b35" : "#6b4b35" }
                GradientStop { position: 0.3; color: studio.backdropEnabled ? "#f050392b" : "#50392b" }
                GradientStop { position: 1; color: studio.backdropEnabled ? "#f52f241f" : "#2f241f" }
            }
            border.width: 1;border.color: "#ad8055"
            Rectangle { x: 1;y: 44;width: 42;height: 1;color: "#946b46" }
            Repeater {
                model: 5
                Rectangle { required property int index;x: 5;y: 91.5+index*51;width: 34;height: 1;color: "#71513a" }
            }
        }
        Item {
            id: railSettings
            objectName: "railSettingsPanel"
            x: 0; y: 44
            width: 44; height: 299
            enabled: overlay.settingsReady
            Repeater {
                model: [
                    { key: "radial", label: "Send to radial" },
                    { key: "edit", label: "Edit buttons: choose a button" },
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
                    dimWhenDisabled: false
                    highlighted: modelData.key==="sound" ? studio.soundEnabled : modelData.key==="glass" ? studio.backdropEnabled : false
                    onClicked: overlay.activateOption(modelData.key)
                }
            }
        }
    }
    HubButton {
        objectName: "railSettingsButton"
        x: railHousing.x; y: railHousing.y
        width: 44; height: 44; railStyle: true; railCap: true
        glyph: overlay.menuOpen || overlay.editingButtons ? "back" : "more"
        hint: overlay.menuOpen ? "Back to drawing tools" : "Radial menu settings"
        iconColor: overlay.menuOpen ? overlay.settingsInk : "#e0edfa"
        hoverFill: overlay.menuOpen ? overlay.settingsHover : "#344b66"
        pressedFill: overlay.menuOpen ? overlay.settingsPressed : "#526b8b"
        reduceMotion: studio.reducedMotion
        visible: railHousing.visible && !studio.runtime.active && !overlay.childrenVisible
        opacity: railHousing.opacity
        enabled: overlay.railMode
        dimWhenDisabled: false
        onPressedChanged: if(pressed&&!overlay.editingButtons)studio.playMenuSound("press")
        onClicked: {if(overlay.editingButtons)overlay.editingButtons=false;else overlay.menuOpen=!overlay.menuOpen;}
    }
    ParentMenu {
        objectName: "parentMenu"
        anchors.fill: parent
        session: studio.runtime
        glassEnabled: studio.backdropEnabled
        reducedMotion: studio.reducedMotion
    }
    Rectangle {
        id: result
        objectName: "overlayResult"
        visible: (overlay.resultMessage.length>0 || overlay.editingButtons) && overlay.toolsReady && !overlay.childrenVisible && !overlay.morphing
        width: Math.min(320,overlay.width-24); height: message.implicitHeight+20; radius: 12
        x: studio.runtime.docked ? 12 : Math.max(8,Math.min(overlay.width-width-8,studio.runtime.radialCenter.x-width/2))
        y: Math.min(overlay.height-height-8,studio.runtime.docked ? overlay.height/2+164 : studio.runtime.radialCenter.y+116)
        color: "#ee382920"; border.color: "#ad8055"
        Text {
            id: message
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            text: overlay.editingButtons ? "Tap a button to edit it. Tap the center to cancel." : overlay.resultMessage; color: "#e0edfa"; font.pixelSize: 12; wrapMode: Text.Wrap
        }
    }
}
