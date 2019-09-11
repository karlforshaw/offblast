#version 330

in vec2 gradientCoord;
uniform float startPos;

out vec4 outputColor;

void main()
{
    float actualStartPos = startPos;
    float newWhole = 1 - actualStartPos;

    if (1 - gradientCoord.y < actualStartPos) {
        outputColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else {
        outputColor = (gradientCoord.y/newWhole) * vec4(0.0, 0.0, 0.0, 1.0);
    }
}
