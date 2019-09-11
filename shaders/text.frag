#version 330

in vec2 TexCoord;
uniform sampler2D ourTexture;
uniform float myAlpha;
uniform vec2 textureSize;

out vec4 outputColor;

void main()
{

    vec2 actualTexCoord = vec2(TexCoord.x, TexCoord.y);

    if (textureSize.x != 0.0) { 
        actualTexCoord.x = TexCoord.x * textureSize.x; 
    }

    if (textureSize.y != 0.0) {
        actualTexCoord.y = textureSize.y +  (TexCoord.y*(1-textureSize.y)); 
    }

   vec4 mySample = texture(ourTexture, actualTexCoord);
   //outputColor = mix(mySample, vec4(1,1,1,1), 0.3);
   outputColor = myAlpha*texture(ourTexture, actualTexCoord);

}
