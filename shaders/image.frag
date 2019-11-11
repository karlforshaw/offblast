#version 330

in vec2 TexCoord;
uniform sampler2D ourTexture;
uniform float myAlpha;
uniform float whiteMix;

out vec4 outputColor;


vec3 desaturate(vec3 color, float amount)
{
    vec3 gray = vec3(dot(vec3(0.2126,0.7152,0.0722), color));
    return vec3(mix(color, gray, amount));
}

void main()
{
   vec4 mySample = texture(ourTexture, TexCoord);
   mySample.rgb = desaturate(vec3(mySample), whiteMix);
   outputColor = myAlpha*mySample;
}
