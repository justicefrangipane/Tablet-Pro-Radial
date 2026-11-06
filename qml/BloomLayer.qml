import QtQuick

// A small, temporary texture for the settings only. The shader reveals the
// existing painted contents, so transparent gaps and rounded rail corners stay
// transparent. Once settled, Qt renders the contents normally again.
Item {
    id: bloom
    property real progress: 1
    property bool docked: false
    readonly property bool softwareRenderer: GraphicsInfo.api === GraphicsInfo.Software
    opacity: progress > 0 ? 1 : 0
    layer.enabled: visible && !softwareRenderer && progress > 0 && progress < 1
    layer.smooth: true
    layer.effect: ShaderEffect {
        property var source
        property real progress: bloom.progress
        property vector2d menuSize: Qt.vector2d(bloom.width, bloom.height)
        property real rail: bloom.docked ? 1 : 0
        fragmentShader: "qrc:/shaders/gentle-bloom.frag.qsb"
    }
}
