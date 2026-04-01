#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

uniform mat4 uProjection;
uniform mat4 uView;
uniform vec2 uSpritePos;
uniform vec2 uSpriteSize;

out vec2 vUV;

void main()
{
    vec2 worldPos = uSpritePos + (aPos * uSpriteSize);
    gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
    vUV = aUV;
}