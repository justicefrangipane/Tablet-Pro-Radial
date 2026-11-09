pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import TabletPro 1.0
import "." as StudioUi

ApplicationWindow {
    id: editor
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 640
    visible: startInEditor
    title: "Tablet Pro · Radial Studio" + (studio.dirty ? " •" : "")
    color: "#101217"
    font.family: "Segoe UI"
    font.pixelSize: 13
    palette.window: "#101217"
    palette.windowText: "#e4e9f2"
    palette.base: "#141820"
    palette.text: "#e4e9f2"
    palette.button: "#1c232f"
    palette.buttonText: "#d5dfed"
    palette.highlight: "#7eb7ef"
    palette.highlightedText: "#0f1924"
    palette.placeholderText: "#69778c"
    property color accent: "#91c5fa"
    property var iconNames: ["brush", "paintbrush", "eraser", "select", "lasso-select", "palette", "text", "type", "settings", "settings-2", "bold", "italic", "underline", "copy", "paste", "clipboard-paste", "scissors", "text-select", "arrow-left-to-line", "arrow-right-to-line", "undo", "undo-2", "redo", "redo-2", "save", "x", "trash-2", "ellipsis", "group"]
    property var kindNames: ["group", "shortcut", "text", "empty", "modifier"]
    property var modifierNames: ["Ctrl", "Alt", "Shift", "Meta"]
    property bool quitAfterClose: false
    property bool arrangeButtons: false
    property string draggedButtonId: ""
    property string draggedButtonLabel: ""
    property string dropParentId: ""
    property string dropParentLabel: ""
    property string dropProblem: ""
    property point buttonDragPosition: Qt.point(0,0)
    onArrangeButtonsChanged: { cancelButtonDrag(); studio.preview.cancel(); }
    onActiveChanged: if(!active)cancelButtonDrag()

    function previewIdAt(point) {
        const at=previewSurface.mapFromItem(editor.contentItem,point.x,point.y)
        if(at.x<0 || at.y<0 || at.x>previewSurface.width || at.y>previewSurface.height)return ""
        return previewSurface.hitRoot(at.x,at.y)
    }
    function outlineIdAt(point) {
        const at=outlineList.mapFromItem(editor.contentItem,point.x,point.y)
        if(at.x<0 || at.y<0 || at.x>=outlineList.width || at.y>=outlineList.height)return ""
        const row=outlineList.itemAt(at.x+outlineList.contentX,at.y+outlineList.contentY)
        return row ? row.modelData.id : ""
    }
    function moveButtonDrag(point) {
        if(!draggedButtonId.length)return
        buttonDragPosition=point
        const at=outlineList.mapFromItem(editor.contentItem,point.x,point.y)
        let id=""
        if(at.x>=0 && at.y>=0 && at.x<outlineList.width && at.y<outlineList.height){
            id=outlineIdAt(point)
        }else if(!studio.preview.parentMenuOpen && !studio.preview.transitioning)id=previewIdAt(point)
        dropParentId=id
        dropParentLabel=""
        for(const node of studio.outline)if(node.id===id){dropParentLabel=node.label;break}
        dropProblem=studio.reparentProblem(draggedButtonId,id)
    }
    function beginButtonDrag(id,point) {
        if(!id.length || draggedButtonId.length)return false
        studio.preview.cancel()
        draggedButtonId=id
        for(const node of studio.outline)if(node.id===id){draggedButtonLabel=node.label;break}
        moveButtonDrag(point)
        return true
    }
    function cancelButtonDrag() { draggedButtonId="";dropParentId="";dropParentLabel="";dropProblem="" }
    function finishButtonDrag() {
        const source=draggedButtonId, destination=dropParentId
        cancelButtonDrag()
        if(source.length && destination.length)Qt.callLater(function(){studio.reparentButton(source,destination)})
    }
    component ButtonMove: DragHandler {
        property string buttonId: ""
        property bool ownsMove: false
        target: null
        acceptedButtons: Qt.LeftButton
        dragThreshold: 8
        onActiveChanged: if(active)ownsMove=editor.beginButtonDrag(buttonId,centroid.scenePosition)
        onCentroidChanged: if(active && ownsMove)editor.moveButtonDrag(centroid.scenePosition)
        onGrabChanged: (transition,eventPoint) => {
            if(!ownsMove)return
            if(transition===PointerDevice.UngrabExclusive){
                ownsMove=false
                editor.moveButtonDrag(eventPoint.scenePosition)
                editor.finishButtonDrag()
            }else if(transition===PointerDevice.CancelGrabExclusive){ownsMove=false;editor.cancelButtonDrag()}
        }
        Component.onDestruction: if(ownsMove)editor.cancelButtonDrag()
    }
    Timer {
        interval: 32; repeat: true; running: editor.draggedButtonId.length>0
        onTriggered: {
            const at=outlineList.mapFromItem(editor.contentItem,editor.buttonDragPosition.x,editor.buttonDragPosition.y)
            if(at.x<0 || at.x>outlineList.width || at.y<0 || at.y>outlineList.height)return
            const step=at.y<28?-9:at.y>outlineList.height-28?9:0
            if(step){
                outlineList.contentY=Math.max(outlineList.originY,Math.min(outlineList.originY+Math.max(0,outlineList.contentHeight-outlineList.height),outlineList.contentY+step))
                editor.moveButtonDrag(editor.buttonDragPosition)
            }
        }
    }
    Rectangle {
        objectName: "buttonDragFeedback"
        parent: editor.contentItem; z: 1000
        visible: editor.draggedButtonId.length>0
        x: Math.max(4,Math.min(editor.contentItem.width-width-4,editor.buttonDragPosition.x+18))
        y: Math.max(4,Math.min(editor.contentItem.height-height-4,editor.buttonDragPosition.y-70))
        width: 250; height: 62; radius: 10
        color: editor.dropParentId.length && !editor.dropProblem.length ? "#254b43" : "#262d3b"
        border.color: editor.dropParentId.length && !editor.dropProblem.length ? "#8cd5b7" : "#71859f"
        Column {
            anchors.fill: parent; anchors.margins: 9; spacing: 3
            Text { width: parent.width; text: editor.draggedButtonLabel; color: "#eef4ff"; font.bold: true; elide: Text.ElideRight }
            Text { width: parent.width; text: editor.dropParentId.length && !editor.dropProblem.length ? "Move into "+editor.dropParentLabel : editor.dropProblem; color: "#c1cfde"; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
    }

    onClosing: close => {
        if (studio.dirty) {
            close.accepted = false;
            unappliedDialog.open();
        }
    }

    function requestAppQuit() {
        if (studio.dirty) {
            quitAfterClose = true;
            studio.showEditor();
            unappliedDialog.open();
        } else desktop.finishQuit();
    }

    function finishClosing() {
        const shouldQuit = quitAfterClose;
        unappliedDialog.close();
        if (shouldQuit) desktop.finishQuit();
        else editor.close();
    }

    function setModifier(name, enabled) {
        const values = Array.from(studio.selectedModifiers);
        const index = values.indexOf(name);
        if (enabled && index < 0) values.push(name);
        if (!enabled && index >= 0) values.splice(index, 1);
        studio.setShortcut(studio.selectedKey, values);
    }

    function saveTestAs() {
        editor.contentItem.forceActiveFocus();
        saveDialog.selectedFile = studio.suggestedTestFile();
        saveDialog.open();
    }

    function saveCurrent() {
        editor.contentItem.forceActiveFocus();
        if (studio.currentFile.toString().length > 0) studio.saveCurrentFile();
        else saveTestAs();
    }

    component StudioButton: Button {
        id: control
        property bool primary: false
        property bool subtle: false
        property bool destructive: false
        property string hint: ""
        implicitHeight: 34
        implicitWidth: Math.max(34, contentItem.implicitWidth + 24)
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        font.weight: primary ? Font.DemiBold : Font.Normal
        Accessible.name: text
        contentItem: Text {
            text: control.text
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: !control.enabled ? "#4d5869" : control.primary ? "#0e1b2a" : control.destructive ? "#e9a1a5" : "#d5dfed"
        }
        background: Rectangle {
            radius: 7
            color: !control.enabled ? "#161b23" : control.primary ? (control.down ? "#71a9df" : control.hovered ? "#b3d9ff" : "#91c5fa") : control.down ? "#29374b" : control.hovered ? "#252f3f" : control.subtle ? "transparent" : "#1c2431"
            border.width: control.primary || control.subtle ? 0 : 1
            border.color: control.activeFocus ? "#91c5fa" : "#303b4e"
        }
        ToolTip.visible: hovered && hint.length > 0
        ToolTip.delay: 550
        ToolTip.text: hint
    }

    component SectionLabel: Label {
        color: "#75849a"
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 1.6
    }

    component FieldLabel: Label {
        color: "#a6b3c6"
        font.pixelSize: 12
    }

    component StudioField: TextField {
        id: field
        implicitHeight: 36
        color: "#e4e9f2"
        selectByMouse: true
        leftPadding: 10
        rightPadding: 10
        background: Rectangle {
            radius: 6
            color: "#10151e"
            border.color: field.activeFocus ? "#7caddd" : "#2b3647"
        }
    }

    component StudioCombo: ComboBox {
        id: combo
        implicitHeight: 36
        leftPadding: 10
        rightPadding: 26
        contentItem: Text {
            text: combo.displayText
            color: "#d9e3f1"
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 6
            color: "#10151e"
            border.color: combo.activeFocus ? "#7caddd" : "#2b3647"
        }
        indicator: Text {
            x: combo.width - width - 10
            y: (combo.height - height) / 2 - 2
            text: "⌄"
            color: "#8da6c1"
            font.pixelSize: 17
        }
    }

    component StudioSwitch: Switch {
        id: switchControl
        implicitHeight: 28
        implicitWidth: contentItem.implicitWidth
        padding: 0
        spacing: 8
        font.pixelSize: 11
        indicator: Rectangle {
            implicitWidth: 32
            implicitHeight: 18
            y: (switchControl.height - height) / 2
            radius: 9
            color: switchControl.checked ? "#43688c" : "#273241"
            border.color: switchControl.activeFocus ? "#91c5fa" : switchControl.checked ? "#7eb2e5" : "#42536a"
            Rectangle {
                x: switchControl.checked ? 17 : 3
                y: 3
                width: 12
                height: 12
                radius: 6
                color: switchControl.checked ? "#d8edff" : "#8d9db0"
            }
        }
        contentItem: Text {
            text: switchControl.text
            font: switchControl.font
            color: "#aebfd2"
            leftPadding: switchControl.indicator.width + switchControl.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }

    component StudioCheck: CheckBox {
        id: checkControl
        implicitHeight: 29
        padding: 0
        spacing: 7
        font.pixelSize: 11
        indicator: Rectangle {
            implicitWidth: 16
            implicitHeight: 16
            y: (checkControl.height - height) / 2
            radius: 4
            color: checkControl.checked ? "#779fc8" : "#10151e"
            border.color: checkControl.activeFocus ? "#c5e3ff" : checkControl.checked ? "#9fcafa" : "#4b5e76"
            Text { anchors.centerIn: parent; text: "✓"; visible: checkControl.checked; color: "#101c29"; font.pixelSize: 13; font.bold: true }
        }
        contentItem: Text {
            text: checkControl.text
            font: checkControl.font
            color: "#b3c2d5"
            leftPadding: checkControl.indicator.width + checkControl.spacing
            verticalAlignment: Text.AlignVCenter
        }
    }

    FileDialog {
        id: openDialog
        title: "Open radial menu or test file as a draft"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Radial menu and test files (*.radial.json *.json)", "All files (*)"]
        onAccepted: studio.loadFile(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: "Save test radial — menu and settings"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Radial test file (*.radial.json *.json)"]
        onAccepted: studio.saveTestFile(selectedFile)
    }
    Shortcut { sequence: StandardKey.Save; onActivated: editor.saveCurrent() }
    Shortcut { sequence: StandardKey.SaveAs; onActivated: editor.saveTestAs() }
    Shortcut { sequence: StandardKey.Open; onActivated: openDialog.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: studio.canUndo; onActivated: studio.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: studio.canRedo; onActivated: studio.redo() }
    Shortcut {
        sequence: "Escape"
        enabled: editor.visible
        onActivated: {
            editor.cancelButtonDrag();
            studio.preview.cancel();
            studio.runtime.cancel();
        }
    }

    Dialog {
        id: penDialog
        objectName: "penSetupDialog"
        parent: editor.contentItem
        anchors.centerIn: parent
        width: Math.min(480, editor.width - 48)
        title: "Open your menu with a pen button"
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 22
        onOpened: penInputSwitch.forceActiveFocus()
        background: Rectangle { radius: 12; color: "#1a222f"; border.color: "#425570" }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                Layout.fillWidth: true
                text: "Open the applied menu from your drawing app. Dock it to keep your tools at the edge of the screen."
                color: "#b8c7da"
                wrapMode: Text.WordWrap
                lineHeight: 1.2
            }
            StudioSwitch {
                id: penInputSwitch
                Layout.fillWidth: true
                text: "Use Tablet Pro pen input"
                checked: studio.stylus.enabled
                onToggled: studio.stylus.enabled = checked
            }
            StudioCombo {
                Layout.fillWidth: true
                enabled: studio.stylus.enabled
                model: ["Barrel button (default)", "Invert / second button"]
                currentIndex: studio.stylus.selectedSideButton - 1
                Accessible.name: "Button that opens the radial menu"
                onActivated: studio.stylus.selectedSideButton = currentIndex + 1
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: penStatus.implicitHeight + 24
                radius: 8
                color: "#111c28"
                border.color: "#2c435a"
                Label {
                    id: penStatus
                    anchors.fill: parent
                    anchors.margins: 12
                    text: studio.stylus.status
                    color: "#a6c9eb"
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                    Accessible.name: "Pen input status: " + text
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#344357" }
            Repeater {
                model: studio.stylus.readings
                Label {
                    required property var modelData
                    Layout.fillWidth: true
                    color: "#b8c7da"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    text: modelData.name + ": " + modelData.status
                          + " · tip down " + modelData.tipDowns + " / up " + modelData.tipUps
                          + (modelData.packets > 0 ? "\nTilt (DLL units) " + modelData.tiltX + ", " + modelData.tiltY
                             + " · pressure (raw) " + modelData.pressure : "")
                }
            }
            SectionLabel { text: "USING YOUR PEN DRIVER" }
            Label {
                Layout.fillWidth: true
                text: "For Wacom, Xencelabs, or another pen driver, assign Ctrl+Alt+Space to your side button in its settings. That shortcut opens the same menu. Use one method per button."
                color: "#a8b8cc"
                wrapMode: Text.WordWrap
                lineHeight: 1.2
            }
            Label {
                Layout.fillWidth: true
                visible: studio.platformStatus.length > 0
                text: studio.platformStatus
                color: "#e6b2ab"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Accessible.name: "Shortcut status: " + text
            }
            RowLayout {
                Layout.fillWidth: true
                StudioButton {
                    text: "Reconnect pen"
                    enabled: studio.stylus.enabled
                    onClicked: {
                        studio.stylus.stop();
                        studio.stylus.start();
                    }
                }
                Item { Layout.fillWidth: true }
                StudioButton { text: "Done"; primary: true; onClicked: penDialog.close() }
            }
        }
    }

    Dialog {
        id: unappliedDialog
        objectName: "unappliedDialog"
        parent: editor.contentItem
        anchors.centerIn: parent
        width: Math.min(460, editor.width - 48)
        title: "Unapplied changes"
        modal: true
        closePolicy: Popup.CloseOnEscape
        padding: 22
        property string applyError: ""
        onClosed: editor.quitAfterClose = false
        onOpened: {
            applyError = "";
            keepEditingButton.forceActiveFocus();
        }
        background: Rectangle { radius: 12; color: "#1a222f"; border.color: "#425570" }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                Layout.fillWidth: true
                text: "Your draft has changes that have not been applied. Keep editing, or choose what to do before closing."
                color: "#b8c7da"
                wrapMode: Text.WordWrap
                lineHeight: 1.2
            }
            Label {
                Layout.fillWidth: true
                visible: unappliedDialog.applyError.length > 0
                text: unappliedDialog.applyError
                color: "#e6b2ab"
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StudioButton {
                    text: "Discard & close"
                    objectName: "discardAndClose"
                    destructive: true
                    onClicked: {
                        studio.discardDraft();
                        editor.finishClosing();
                    }
                }
                StudioButton {
                    text: "Apply & close"
                    objectName: "applyAndClose"
                    onClicked: {
                        studio.applyDraft();
                        unappliedDialog.applyError = studio.error;
                        if (studio.error.length === 0 && !studio.dirty) {
                            editor.finishClosing();
                        }
                    }
                }
                StudioButton {
                    id: keepEditingButton
                    objectName: "keepEditingButton"
                    text: "Keep editing"
                    primary: true
                    onClicked: { desktop.cancelQuit(); unappliedDialog.close(); }
                    Keys.onReturnPressed: { desktop.cancelQuit(); unappliedDialog.close(); }
                    Keys.onEnterPressed: { desktop.cancelQuit(); unappliedDialog.close(); }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            color: "#141820"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 22
                spacing: 10
                Item {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    Repeater {
                        model: 4
                        Rectangle {
                            required property int index
                            x: (index % 2) * 13
                            y: Math.floor(index / 2) * 13
                            width: 10
                            height: 10
                            radius: 3
                            color: index === 0 ? "#a0cfff" : index === 1 ? "#6e9dcb" : index === 2 ? "#456c98" : "#2a425f"
                        }
                    }
                }
                ColumnLayout {
                    spacing: 2
                    SectionLabel { text: "TABLET PRO"; color: "#8eaccb"; font.pixelSize: 9 }
                    Label { text: "Radial Studio"; font.pixelSize: 19; font.weight: Font.DemiBold; color: "#f0f4fa" }
                }
                Item { Layout.fillWidth: true }
                StudioButton { text: "↶"; font.pixelSize: 21; enabled: studio.canUndo; hint: "Undo · Ctrl+Z"; onClicked: studio.undo(); Accessible.name: "Undo" }
                StudioButton { text: "↷"; font.pixelSize: 21; enabled: studio.canRedo; hint: "Redo"; onClicked: studio.redo(); Accessible.name: "Redo" }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; Layout.leftMargin: 3; Layout.rightMargin: 3; color: "#2c3544" }
                StudioButton { text: "Open"; subtle: true; onClicked: openDialog.open(); hint: "Open a radial file as a draft; Apply publishes it" }
                StudioButton { text: "Save"; visible: editor.width >= 1060 && studio.currentFile.toString().length > 0; hint: "Save this file · Ctrl+S"; onClicked: editor.saveCurrent() }
                StudioButton { text: editor.width < 1060 ? "Save test…" : "Save test radial…"; Accessible.name: "Save test radial file"; hint: "Save the draft menu and appearance settings to Test.radial.json · Ctrl+Shift+S"; onClicked: editor.saveTestAs() }
                StudioButton { text: "Open overlay"; primary: true; onClicked: studio.showOverlay(); hint: "Open the applied menu on your desktop" }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#27313e" }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: "#171e28"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 22
                spacing: 14
                StudioSwitch {
                    objectName: "editorPenTriggerEnabled"
                    text: "Pen trigger"
                    checked: studio.stylus.enabled
                    onToggled: studio.stylus.enabled = checked
                    Accessible.name: "Enable the pen button to open the radial menu"
                }
                StudioCombo {
                    objectName: "editorPenTrigger"
                    Layout.preferredWidth: 220
                    enabled: studio.stylus.enabled
                    model: ["Barrel button (default)", "Invert / second button"]
                    currentIndex: studio.stylus.selectedSideButton - 1
                    Accessible.name: "Button that opens the radial menu"
                    onActivated: studio.stylus.selectedSideButton = currentIndex + 1
                }
                Label {
                    text: "Saved automatically"
                    visible: editor.width >= 1060
                    color: "#8a99ad"
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Label { Layout.preferredWidth: 150; text: "Ctrl+Alt+Space also works"; elide: Text.ElideRight; color: "#a8b8cc"; font.pixelSize: 11 }
                StudioButton {
                    objectName: "editorPenSettings"
                    text: "Pen settings"
                    subtle: true
                    hint: studio.stylus.status
                    onClicked: penDialog.open()
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#27313e" }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: editor.width > 1060 ? 220 : 194
                Layout.fillHeight: true
                color: "#151921"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "MENU STRUCTURE" }
                        Item { Layout.fillWidth: true }
                        Rectangle { Layout.preferredWidth: 5; Layout.preferredHeight: 5; radius: 3; color: studio.dirty ? "#d8b57a" : "#75b59d" }
                    }
                    Label { Layout.fillWidth: true; text: "Drag a button onto a parent to make it a child."; color: "#8a99ad"; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    StudioButton {
                        id: rootMenuButton
                        Layout.fillWidth: true
                        text: "Root menu"
                        hint: "Select the root menu to add top-level buttons"
                        onClicked: studio.selectNode("")
                        background: Rectangle {
                            radius: 7
                            color: studio.selectedId.length === 0 ? "#263b53" : rootMenuButton.hovered ? "#202a38" : "#19222f"
                            border.color: studio.selectedId.length === 0 ? "#6c98c5" : "#303b4e"
                        }
                    }
                    ListView {
                        id: outlineList
                        objectName: "outlineList"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: studio.outline
                        spacing: 4
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: editor.draggedButtonId.length===0
                        ButtonMove { buttonId: editor.outlineIdAt(centroid.scenePressPosition) }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: ItemDelegate {
                            id: nodeDelegate
                            required property var modelData
                            objectName: "outline-" + modelData.id
                            width: outlineList.width
                            height: 42
                            leftPadding: 10 + Math.min(3, modelData.depth) * 12
                            rightPadding: 8
                            hoverEnabled: true
                            Accessible.name: modelData.label + ", " + modelData.kind
                            onClicked: studio.selectNode(modelData.id)
                            background: Rectangle {
                                radius: 7
                                readonly property bool dropTarget: editor.draggedButtonId.length>0 && editor.dropParentId===nodeDelegate.modelData.id
                                color: dropTarget ? (editor.dropProblem.length ? "#503038" : "#254b43") : studio.selectedId === nodeDelegate.modelData.id ? "#263b53" : nodeDelegate.hovered ? "#202a38" : "transparent"
                                border.width: dropTarget || studio.selectedId === nodeDelegate.modelData.id ? 1 : 0
                                border.color: dropTarget ? (editor.dropProblem.length ? "#c28d95" : "#8cd5b7") : "#3c5673"
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Label { text: nodeDelegate.modelData.kind === "group" ? "◇" : "·"; color: studio.selectedId === nodeDelegate.modelData.id ? "#abd6ff" : "#8092a9"; font.pixelSize: 19; Layout.preferredWidth: 14 }
                                Label { text: nodeDelegate.modelData.label; color: studio.selectedId === nodeDelegate.modelData.id ? "#e6f2ff" : "#b6c3d6"; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 12 }
                                Label { text: nodeDelegate.modelData.slot >= 0 ? String(nodeDelegate.modelData.slot + 1) : ""; color: "#6b819b"; font.pixelSize: 10 }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#293241" }
                    StudioButton { Layout.fillWidth: true; text: "Use new default menu"; hint: "Load Brush, Eraser, Alt, Ctrl, Shift and Tools as a draft. Undo restores your menu."; onClicked: studio.useDefaultMenu() }
                    SectionLabel { text: "ADD TO " + studio.insertionLabel.toUpperCase(); Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 9; font.letterSpacing: 1 }
                    StudioButton { objectName: "addChildButton"; Layout.fillWidth: true; text: studio.selectedKind === "group" || studio.selectedParentId.length ? "+ Add child" : "+ Add button"; enabled: studio.canAddButton; onClicked: studio.addNextButton(false) }
                    StudioButton { objectName: "addParentButton"; Layout.fillWidth: true; text: "+ Add parent"; enabled: studio.canAddParent; hint: "Create a button that opens its own radial menu"; onClicked: studio.addNextButton(true) }
                    StudioButton { objectName: "makeParentButton"; Layout.fillWidth: true; visible: studio.canMakeParent; text: "Make parent"; hint: "Keep this action as the first child of a new parent menu"; onClicked: studio.makeParent() }
                    Label { Layout.fillWidth: true; visible: !studio.canAddButton; text: "All six slots are filled. Select an action and choose Make parent to add children."; color: "#8a99ad"; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        rowSpacing: 6
                        columnSpacing: 6
                        Repeater {
                            model: studio.childSlots
                            StudioButton {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitWidth: 40
                                text: (modelData.empty ? "+ " : "") + (modelData.slot + 1)
                                hint: modelData.empty ? "Add a child in slot " + (modelData.slot + 1) : "Edit " + modelData.label
                                Accessible.name: hint
                                onClicked: modelData.empty ? studio.addChild(modelData.slot,false) : studio.selectNode(modelData.id)
                            }
                        }
                    }
                }
                Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#27313e" }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: editor.width > 1060 ? 28 : 18
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            spacing: 5
                            SectionLabel { text: "LIVE PREVIEW" }
                            Label { text: "Quiet Gravity"; color: "#e5edf8"; font.pixelSize: 23; font.weight: Font.DemiBold }
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            Layout.preferredWidth: 75
                            Layout.preferredHeight: 25
                            radius: 13
                            color: "#1d2c3b"
                            border.color: "#304359"
                            Label { anchors.centerIn: parent; text: "MOTION 10"; font.pixelSize: 9; font.letterSpacing: 1; color: "#9bbad8" }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: editor.height >= 700
                        text: "Light gathers the pieces. Every gesture keeps its place."
                        font.pixelSize: 12
                        color: "#8899af"
                        wrapMode: Text.WordWrap
                    }
                    Rectangle {
                        id: previewFrame
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 180
                        color: "#111923"
                        radius: 14
                        border.color: "#2a3748"
                        clip: true
                        Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 60 * previewSurface.scale; color: "#111a25"; opacity: 0.8 }
                        Rectangle { x: 60 * previewSurface.scale; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 1; color: "#233246"; opacity: 0.75 }
                        RadialSurface {
                            id: previewSurface
                            objectName: "editorSurface"
                            x: 1
                            y: 1
                            transformOrigin: Item.TopLeft
                            scale: Math.min(1, Math.max(1, previewFrame.height - 2) / 500)
                            width: Math.max(1, previewFrame.width - 2) / scale
                            height: Math.max(1, previewFrame.height - 2) / scale
                            buttons: studio.preview.buttons
                            childButtons: studio.preview.childButtons
                            dockProgress: studio.preview.dockProgress
                            selectedId: editor.draggedButtonId.length && !editor.dropProblem.length ? editor.dropParentId : studio.preview.selectedId
                            enabled: !editor.arrangeButtons
                            reducedMotion: studio.reducedMotion
                            opacity: studio.preview.parentMenuOpen ? 0 : 1
                            onWidthChanged: studio.preview.setViewport(width, height)
                            onHeightChanged: studio.preview.setViewport(width, height)
                            Component.onCompleted: studio.preview.setViewport(width, height)
                            onPressed: (pointerId, x, y) => { const id=previewSurface.hitRoot(x,y); if(id.length)studio.selectNode(id); studio.preview.pointerPressed(pointerId, x, y); }
                            onEditRequested: (id) => studio.selectNode(id)
                            onMoved: (pointerId, x, y) => studio.preview.pointerMoved(pointerId, x, y)
                            onReleased: (pointerId, x, y) => studio.preview.pointerReleased(pointerId, x, y)
                            onCanceled: studio.preview.cancel()
                        }
                        ParentMenu {
                            x: previewSurface.x; y: previewSurface.y
                            width: previewSurface.width; height: previewSurface.height
                            transformOrigin: Item.TopLeft; scale: previewSurface.scale
                            session: studio.preview; reducedMotion: studio.reducedMotion
                        }
                        Item {
                            anchors.fill: parent
                            visible: editor.arrangeButtons
                            enabled: !studio.preview.transitioning
                            ButtonMove { buttonId: editor.previewIdAt(centroid.scenePressPosition) }
                            TapHandler { onTapped: {const id=editor.previewIdAt(point.scenePosition);if(id.length)studio.selectNode(id)} }
                        }
                        Rectangle {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 14
                            width: previewSurface.scale < 0.999 ? 112 : 76
                            height: 23
                            radius: 6
                            color: "#192433"
                            border.color: "#2c3d50"
                            Label { anchors.centerIn: parent; text: "SIMULATION" + (previewSurface.scale < 0.999 ? " · " + Math.round(previewSurface.scale * 100) + "%" : ""); font.pixelSize: 8; font.letterSpacing: 1; color: "#8ca6c1" }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        StudioButton { objectName: "arrangeButtonsButton"; text: editor.arrangeButtons ? "Done arranging" : "Arrange buttons"; primary: editor.arrangeButtons; onClicked: editor.arrangeButtons=!editor.arrangeButtons }
                        StudioButton {
                            text: studio.preview.docked ? "Undock menu" : "Dock to rail"
                            hint: "Preview the voxel transition"
                            onClicked: studio.preview.toggleDock()
                        }
                        StudioButton {
                            text: "↻"
                            font.pixelSize: 18
                            hint: "Replay Quiet Gravity"
                            Accessible.name: hint
                            onClicked: studio.preview.replayDock()
                        }
                        Label { Layout.fillWidth: true; text: studio.preview.status; color: "#9fb6cd"; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: editor.arrangeButtons ? "Drag a button onto a parent in the preview or the menu list. Green means it can move. Undo restores the previous layout." : "Hold a parent for 300 ms to open its radial. Slide out for a fan. Use Arrange buttons to move buttons into parents."
                        color: "#7d90a7"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        lineHeight: 1.25
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#283445"; Layout.topMargin: 2; Layout.bottomMargin: 2 }
                    RowLayout {
                        Layout.fillWidth: true
                        StudioSwitch { text: "Sound"; checked: studio.soundEnabled; onToggled: studio.soundEnabled = checked }
                        Item { Layout.fillWidth: true }
                        StudioSwitch { text: "Reduce motion"; checked: studio.reducedMotion; onToggled: studio.reducedMotion = checked }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: editor.width > 1060 ? 260 : 238
                Layout.fillHeight: true
                color: "#151921"
                Rectangle { width: 1; height: parent.height; color: "#27313e" }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12
                    SectionLabel { text: "BUTTON INSPECTOR" }
                    Label { Layout.fillWidth: true; text: studio.selectedLabel || "Choose a button"; color: "#e1e9f6"; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight }
                    ScrollView {
                        id: inspectorScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            width: inspectorScroll.availableWidth
                            spacing: 9
                            enabled: studio.selectedId.length > 0
                            FieldLabel { text: "Name" }
                            StudioField { Layout.fillWidth: true; text: studio.selectedLabel; placeholderText: "Button name"; onEditingFinished: studio.renameSelected(text) }
                            Label { Layout.fillWidth: true; text: "Modifier keys show their key name on the menu."; color: "#73869e"; font.pixelSize: 10; wrapMode: Text.WordWrap }
                            FieldLabel { text: "Icon"; Layout.topMargin: 7 }
                            StudioCombo { Layout.fillWidth: true; model: editor.iconNames; currentIndex: Math.max(0, editor.iconNames.indexOf(studio.selectedIcon)); onActivated: studio.setIcon(currentText) }
                            FieldLabel { text: "Behavior"; Layout.topMargin: 7 }
                            StudioCombo {
                                Layout.fillWidth: true
                                model: ["Parent · opens radial", "Keyboard shortcut", "Type text", "Empty slot", "Modifier key"]
                                currentIndex: Math.max(0, editor.kindNames.indexOf(studio.selectedKind))
                                onActivated: studio.setKind(editor.kindNames[currentIndex])
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: studio.selectedKind === "group"
                                text: "Hold for 300 ms to open a radial at the pen tip. Slide out for a fan. Choose a child below to edit it."
                                color: "#8ba1bb"
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                                lineHeight: 1.2
                            }
                            StudioButton { Layout.fillWidth: true; visible: studio.selectedParentId.length>0; text: "← Back to parent"; onClicked: studio.selectNode(studio.selectedParentId) }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: studio.selectedKind === "group"
                                spacing: 5
                                FieldLabel { text: "Child buttons" }
                                Repeater {
                                    model: studio.selectedKind === "group" ? studio.childSlots : []
                                    StudioButton {
                                        id: childSlotButton
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: (modelData.slot+1) + "  " + (modelData.empty ? "+ Add child" : modelData.label)
                                        hint: modelData.empty ? "Add a child to this parent" : "Edit " + modelData.label
                                        onClicked: modelData.empty ? studio.addChild(modelData.slot,false) : studio.selectNode(modelData.id)
                                        ButtonMove { enabled: !childSlotButton.modelData.empty; buttonId: childSlotButton.modelData.id }
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: studio.selectedKind === "shortcut"
                                spacing: 8
                                FieldLabel { text: "Key"; Layout.topMargin: 7 }
                                StudioField { Layout.fillWidth: true; text: studio.selectedKey; placeholderText: "e.g. B, F5, Space"; onEditingFinished: studio.setShortcut(text, studio.selectedModifiers) }
                                GridLayout {
                                    columns: 2
                                    Layout.fillWidth: true
                                    columnSpacing: 0
                                    rowSpacing: 0
                                    Repeater {
                                        model: editor.modifierNames
                                        StudioCheck {
                                            required property string modelData
                                            Layout.fillWidth: true
                                            text: modelData
                                            font.pixelSize: 11
                                            checked: studio.selectedModifiers.indexOf(modelData) >= 0
                                            onToggled: editor.setModifier(modelData, checked)
                                        }
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: studio.selectedKind === "modifier"
                                spacing: 10
                                StudioCombo {
                                    Layout.fillWidth: true
                                    model: ["Alt", "Ctrl", "Shift"]
                                    currentIndex: Math.max(0,model.indexOf(studio.selectedKey))
                                    onActivated: studio.setModifierKey(currentText)
                                }
                                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#bca58d"; font.pixelSize: 12; text: "Hold to use. Drag out to lock for this contact; lift to release. Double-tap to keep on; tap again to release." }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: studio.selectedKind === "text"
                                spacing: 8
                                FieldLabel { text: "Text to type"; Layout.topMargin: 7 }
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 110
                                    TextArea {
                                        text: studio.selectedText
                                        color: "#dce7f5"
                                        placeholderText: "Enter text…"
                                        wrapMode: TextEdit.Wrap
                                        selectByMouse: true
                                        padding: 10
                                        background: Rectangle { color: "#10151e"; radius: 6; border.color: parent.activeFocus ? "#7caddd" : "#2b3647" }
                                        onActiveFocusChanged: if (!activeFocus) studio.setText(text)
                                    }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2b3544"; Layout.topMargin: 13; Layout.bottomMargin: 4 }
                            FieldLabel { text: "Position" }
                            RowLayout {
                                Layout.fillWidth: true
                                StudioButton { text: "←"; Layout.fillWidth: true; hint: "Move to the previous slot"; Accessible.name: hint; onClicked: studio.moveSelected(-1) }
                                StudioButton { text: "→"; Layout.fillWidth: true; hint: "Move to the next slot"; Accessible.name: hint; onClicked: studio.moveSelected(1) }
                                StudioButton { text: "Remove"; destructive: true; onClicked: studio.removeSelected() }
                            }
                            Label { Layout.fillWidth: true; text: "Slot positions stay fixed as you edit. Empty slots preserve your learned movements."; color: "#73869e"; font.pixelSize: 10; wrapMode: Text.WordWrap; lineHeight: 1.25 }
                            Item { Layout.preferredHeight: 4 }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#2b3544" }
                    Label { Layout.fillWidth: true; text: studio.hasPendingSettings ? "File settings are staged. Apply will use the imported sound, motion, timing and backdrop settings." : studio.dirty ? "Draft changes are ready to apply." : "Your applied menu is up to date."; color: studio.dirty ? "#c9b591" : "#8298af"; font.pixelSize: 10; wrapMode: Text.WordWrap }
                    RowLayout {
                        Layout.fillWidth: true
                        StudioButton { text: "Discard"; subtle: true; enabled: studio.dirty; onClicked: studio.discardDraft() }
                        StudioButton { text: "Apply draft"; Layout.fillWidth: true; primary: true; enabled: studio.dirty; onClicked: studio.applyDraft() }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "#12171e"
            Rectangle { width: parent.width; height: 1; color: "#263141" }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 8
                Rectangle { Layout.preferredWidth: 5; Layout.preferredHeight: 5; radius: 3; color: studio.error.length > 0 ? "#e6a59c" : "#7bafa0" }
                Label { Layout.fillWidth: true; text: studio.error.length > 0 ? studio.error : studio.status; color: studio.error.length > 0 ? "#e6b2ab" : "#8399b0"; font.pixelSize: 10; elide: Text.ElideRight }
                StudioButton { text: "Updates"; subtle: true; implicitHeight: 28; onClicked: desktop.showUpdates(); hint: "Check GitHub for a newer version" }
                StudioButton { text: "Update log"; subtle: true; implicitHeight: 28; onClicked: desktop.showUpdateLog(); hint: "Read what changed in each version" }
                StudioButton { text: "Quit"; subtle: true; implicitHeight: 28; onClicked: desktop.requestQuit(); hint: "Close Tablet Pro Radial completely" }
            }
        }
    }

    StudioUi.Overlay {
        objectName: "runtimeOverlay"
        onQuitRequested: desktop.requestQuit()
    }
}
