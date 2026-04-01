#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform float uVisibility;

void main()
{
    vec4 color = texture(uTexture, vUV);
    if (color.a < 0.01)
    {
        discard;
    }
    FragColor = color * vec4(uVisibility, uVisibility, uVisibility, 1.0);
}