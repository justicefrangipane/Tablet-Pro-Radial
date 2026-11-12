import QtQuick
import TabletPro 1.0

Item {
    id: menu
    property var session
    property bool glassEnabled: false
    property bool reducedMotion: false
    readonly property bool open: session && session.parentMenuOpen
    component MenuPointer: TapHandler {
        property int pointerId: 0
        property bool ownsContact: false
        property int capturedId: 0
        enabled: menu.open || ownsContact
        gesturePolicy: TapHandler.ReleaseWithinBounds
        acceptedButtons: Qt.LeftButton
        onPointChanged: if(ownsContact && pressed)menu.session.pointerMoved(capturedId,point.position.x,point.position.y)
        onGrabChanged: (transition,eventPoint) => {
            if(transition===PointerDevice.GrabExclusive){
                // The handler point can still contain the previous contact when
                // pressed changes; the grab event identifies this contact.
                capturedId=pointerId || eventPoint.id;ownsContact=true
                menu.session.pointerPressed(capturedId,eventPoint.position.x,eventPoint.position.y)
                return
            }
            if(!ownsContact)return
            if(transition===PointerDevice.UngrabExclusive){
                ownsContact=false
                menu.session.pointerReleased(capturedId,eventPoint.position.x,eventPoint.position.y)
            }else if(transition===PointerDevice.CancelGrabExclusive){ownsContact=false;if(menu.session.active)menu.session.cancel()}
        }
    }
    Item {
        anchors.fill: parent
        containmentMask: QtObject {
            function contains(point: point): bool {
                return !menu.session || !menu.session.parentInPlace ||
                    Math.pow(point.x-menu.session.parentCenter.x,2)+Math.pow(point.y-menu.session.parentCenter.y,2)>40*40
            }
        }
        MenuPointer { acceptedDevices: PointerDevice.Stylus | PointerDevice.Airbrush; pointerId: -2 }
        MenuPointer { acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad; pointerId: -1 }
        MenuPointer { acceptedDevices: PointerDevice.TouchScreen }
    }
    RadialSurface {
        objectName: "parentMenuSurface"
        visible: menu.open
        x: menu.session ? menu.session.parentCenter.x-100 : 0
        y: menu.session ? menu.session.parentCenter.y-100 : 0
        width: 200; height: 200; clip: true
        buttons: menu.session ? menu.session.parentButtons.map(function(button){
            return Object.assign({},button,{cx:button.cx-menu.session.parentCenter.x+100,cy:button.cy-menu.session.parentCenter.y+100})
        }) : []
        selectedId: menu.session ? menu.session.parentSelectedId : ""
        glassEnabled: menu.glassEnabled
        reducedMotion: menu.reducedMotion
        enabled: false
    }
    Rectangle {
        id: center
        objectName: "parentMenuCenter"
        visible: menu.open
        x: menu.session ? menu.session.parentCenter.x-38 : 0
        y: menu.session ? menu.session.parentCenter.y-38 : 0
        width: 76; height: 76; radius: 38
        gradient: Gradient {
            GradientStop { position: 0; color: "#354454" }
            GradientStop { position: 1; color: "#19222e" }
        }
        border.color: "#688297"; border.width: 1
        containmentMask: QtObject {
            function contains(point: point): bool { return Math.pow(point.x-38,2)+Math.pow(point.y-38,2)<=38*38; }
        }
        TapHandler {
            enabled: menu.open && menu.session.parentInPlace
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: menu.session.parentBack()
        }
        RadialDragHandler {
            objectName: "parentCenterDrag"
            enabled: menu.open && menu.session.parentInPlace
            session: menu.session
        }
        HubButton {
            objectName: "parentMenuBack"
            anchors.fill: parent
            centerStyle: true; padding: 0; bottomPadding: 18
            enabled: false; dimWhenDisabled: false
            glyph: "back"; iconColor: "#ffe5bd"
            hint: "Back · Esc closes this menu"
            suppressHint: true
            reduceMotion: menu.reducedMotion
            onClicked: menu.session.parentBack()
        }
        Text {
            x: 7; y: 48; width: 62; height: 15
            text: menu.session ? menu.session.parentLabel : ""
            color: "#ffe5bd"; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
        }
    }
}
