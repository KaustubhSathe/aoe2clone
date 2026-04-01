#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aOffset;
layout (location = 2) in float aVisibility;

uniform mat4 uProjection;
uniform mat4 uView;

out float vVisibility;

void main()
{
    vec2 worldPos = aPos + aOffset;
    gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
    vVisibility = aVisibility;
}