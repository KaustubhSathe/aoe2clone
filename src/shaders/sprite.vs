#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec2 aSpritePos;
layout (location = 3) in vec2 aSpriteSize;
layout (location = 4) in float aVisibility;

uniform mat4 uProjection;
uniform mat4 uView;

out vec2 vUV;
out float vVisibility;

void main()
{
    vec2 worldPos = aSpritePos + (aPos * aSpriteSize);
    gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
    vUV = aUV;
    vVisibility = aVisibility;
}
