#version 330

layout(location = 0) in vec4 position;
layout(location = 1) in vec4 color;

out vec4 outColor;

void main()
{
    gl_Position = position;
    outColor = color;
}
