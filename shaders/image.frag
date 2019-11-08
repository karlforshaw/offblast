#version 330

in vec2 TexCoord;
uniform sampler2D ourTexture;
uniform float myAlpha;
uniform float whiteMix;
uniform vec2 textureSize;

out vec4 outputColor;


vec3 desaturate(vec3 color, float amount)
{
    vec3 gray = vec3(dot(vec3(0.2126,0.7152,0.0722), color));
    return vec3(mix(color, gray, amount));
}

void main()
{

    vec2 actualTexCoord = vec2(TexCoord.x, TexCoord.y);

    if (textureSize.x != 0.0) { 
        float aspect = textureSize.x / textureSize.y;
        actualTexCoord.x = TexCoord.x * textureSize.x; 
    }

    if (textureSize.y != 0.0) {
        actualTexCoord.y = textureSize.y +  (TexCoord.y*(1.0-textureSize.y)); 
    }

   vec4 mySample = texture(ourTexture, actualTexCoord);

   mySample.rgb = desaturate(vec3(mySample), whiteMix);

   outputColor = myAlpha*mySample;
}
