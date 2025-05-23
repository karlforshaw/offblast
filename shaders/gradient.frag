#version 330

in vec2 uv;
out vec4 outputColor;

uniform vec4 gradientColorStart;
uniform vec4 gradientColorEnd;
uniform int gradientHorizontal;

void main()
{
    float t = gradientHorizontal == 1 ? uv.x : uv.y;
    outputColor = mix(gradientColorStart, gradientColorEnd, t);
}
