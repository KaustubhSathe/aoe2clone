#version 330 core
in float vVisibility;
out vec4 FragColor;
uniform vec4 uColor;

void main()
{
    FragColor = uColor * vec4(vVisibility, vVisibility, vVisibility, 1.0);
}