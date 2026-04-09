#version 330 core
in vec2 vUV;
in float vVisibility;
out vec4 FragColor;

uniform sampler2D uTexture;

void main()
{
    vec4 color = texture(uTexture, vUV);
    if (color.a < 0.01)
    {
        discard;
    }
    FragColor = color * vec4(vVisibility, vVisibility, vVisibility, 1.0);
}
