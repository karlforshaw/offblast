#version 330

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 aTexcoord;
uniform vec2 myOffset;

out vec2 TexCoord;

void main()
{
   gl_Position = position;
   TexCoord = aTexcoord;
};
