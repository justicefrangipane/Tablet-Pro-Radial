#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float progress;
    vec2 menuSize;
    float rail;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    float p = clamp(progress, 0.0, 1.0);
    float q = p * p * (3.0 - 2.0 * p);
    vec2 origin = rail > 0.5 ? vec2(menuSize.x * 0.5, 22.0) : menuSize * 0.5;
    vec2 point = qt_TexCoord0 * menuSize - origin;
    // The rail shares the bloom's contour, stretched down from its ellipsis.
    point.y /= rail > 0.5 ? 2.9 : 1.0;
    float a = atan(point.y, point.x);
    float distance = length(point - vec2(9.0 * q, -5.0 * q));
    float ripple = (1.0 - q) * (sin(a * 3.0 + p * 4.0) * 7.0
                                            + sin(a * 5.0 - p * 5.0) * 3.0);
    float edge = 132.0 * q - distance + ripple;
    float cover = smoothstep(-0.65, 0.65, edge);
    float glint = max(0.0, 1.0 - abs(edge - 1.3) / 2.3) * sin(3.14159265 * p);
    vec4 color = texture(source, qt_TexCoord0);
    color.rgb += color.a * vec3(0.20, 0.14, 0.08) * glint;
    fragColor = color * cover * qt_Opacity;
}
