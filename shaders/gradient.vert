#version 330

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 aTexcoord;

uniform float yOffset;
uniform float yFlip;

out vec2 gradientCoord;

void main()
{

    vec4 actualPosition = position;
    vec2 actualTexCoord = aTexcoord;

    if (yOffset != 0.0) {
        actualPosition.y += yOffset;
    }

    if (yFlip != 0) {
        actualTexCoord.y = 1- actualTexCoord.y;
    }

    gl_Position = actualPosition;
    gradientCoord = actualTexCoord;
}
