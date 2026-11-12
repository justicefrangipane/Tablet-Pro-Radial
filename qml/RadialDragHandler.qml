import QtQuick

DragHandler {
    property var session
    property point origin: Qt.point(0,0)
    target: null
    dragThreshold: 7
    acceptedButtons: Qt.LeftButton
    minimumPointCount: 1; maximumPointCount: 1
    cursorShape: Qt.ClosedHandCursor
    function moveCenter() {
        if(active && session.dragging)
            session.moveRadialCenter(origin.x+activeTranslation.x,origin.y+activeTranslation.y)
    }
    onActiveChanged: {
        if(active){origin=session.radialCenter;if(session.beginRadialDrag())moveCenter();}
        else session.endRadialDrag()
    }
    onTranslationChanged: moveCenter()
}
