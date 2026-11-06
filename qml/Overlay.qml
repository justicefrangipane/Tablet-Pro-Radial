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
                cx:studio.runtime.radialCenter.x+70*Math.cos(angle),cy:studio.runtime.radialCenter.y+70*Math.sin(angle)}
        })
    }
    onVisibleChanged: {
        menuOpen = false; editingButtons=false; resultMessage = "";
        if (visible) studio.runtime.setViewport(surface.width,surface.height);
        else studio.runtime.cancel();
    }
    Shortcut { sequence: "Escape"; onActivated: { overlay.menuOpen=false;overlay.editingButtons=false; studio.runtime.cancel(); } }
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
        bronzeTheme: true
        visible: !overlay.menuOpen
        enabled: visible
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
        onCanceled: studio.runtime.cancel()
    }
    RadialSurface {
        id: settingsSurface
        objectName: "settingsSurface"
        anchors.fill: parent
        visible: overlay.menuOpen && !overlay.railMode
        enabled: visible
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
        id: hub
        objectName: "overlayToolbar"
        x: studio.runtime.radialCenter.x-width/2
        y: studio.runtime.radialCenter.y-height/2
        width: 76; height: 76; radius: 38
        visible: !studio.runtime.docked && !studio.runtime.active && !overlay.morphing && !overlay.childrenVisible
        gradient: Gradient {
            GradientStop { position: 0; color: "#956b46" }
            GradientStop { position: 0.45; color: "#583e2d" }
            GradientStop { position: 1; color: "#30241e" }
        }
        border.color: "#d4ab78";border.width: 1
        Rectangle { anchors.fill: parent;anchors.margins: 4;radius: width/2;color: "transparent";border.color: "#407f5b3e" }
        HubButton {
            objectName: "moreMenuButton"
            anchors.fill: parent
            bottomPadding: 21
            glyph: overlay.menuOpen || overlay.editingButtons ? "back" : "radial"
            hint: overlay.editingButtons ? "Tap a button to edit. Tap center to cancel." : overlay.menuOpen ? "Back to tools" : "Menu settings and Edit buttons"
            iconColor: overlay.settingsInk
            hoverFill: "#306a4c34";pressedFill: "#807b5537"
            reduceMotion: studio.reducedMotion
            onClicked: {if(overlay.editingButtons)overlay.editingButtons=false;else overlay.menuOpen=!overlay.menuOpen;}
        }
        Text { anchors.horizontalCenter: parent.horizontalCenter;y: 47;text: overlay.editingButtons?"Edit":overlay.menuOpen?"Back":"Menu";color: overlay.settingsInk;font.pixelSize: 11;font.weight: Font.Medium }
    }
    HubButton {
        objectName: "railSettingsButton"
        x: railHousing.x; y: railHousing.y
        width: 44; height: 44; railStyle: true
        glyph: overlay.menuOpen || overlay.editingButtons ? "back" : "more"
        hint: overlay.menuOpen ? "Back to drawing tools" : "Radial menu settings"
        iconColor: overlay.menuOpen ? overlay.settingsInk : "#e0edfa"
        hoverFill: overlay.menuOpen ? overlay.settingsHover : "#344b66"
        pressedFill: overlay.menuOpen ? overlay.settingsPressed : "#526b8b"
        reduceMotion: studio.reducedMotion
        visible: railHousing.visible && !studio.runtime.active && !overlay.childrenVisible
        onClicked: {if(overlay.editingButtons)overlay.editingButtons=false;else overlay.menuOpen=!overlay.menuOpen;}
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
                highlighted: modelData.key==="sound" ? studio.soundEnabled : modelData.key==="glass" ? studio.backdropEnabled : false
                onClicked: overlay.activateOption(modelData.key)
            }
        }
    }
    Rectangle {
        id: result
        objectName: "overlayResult"
        visible: (overlay.resultMessage.length>0 || overlay.editingButtons) && !overlay.menuOpen && !overlay.childrenVisible && !overlay.morphing
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
