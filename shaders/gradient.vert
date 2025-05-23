#version 330

layout(location = 0) in vec2 position;

out vec2 uv;

uniform vec2 gradientPos;
uniform vec2 gradientSize;

void main()
{
    uv = position;
    vec2 scaled = gradientPos + position * gradientSize;
    gl_Position = vec4(scaled, 0.0, 1.0);
}
