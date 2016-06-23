#version 120
#define MAX_LIGHTS 8

uniform vec4 FogColor = vec4(0.0, 0.0, 0.0, 0.0);
uniform float Brightness = 1.0;
uniform int NumLights;
uniform vec3 LightAmbient[MAX_LIGHTS];
uniform vec3 LightDiffuse[MAX_LIGHTS];
uniform vec3 LightSpecular[MAX_LIGHTS];
/*varying float LightDistV[MAX_LIGHTS];*/
uniform float LightDist[MAX_LIGHTS];
varying vec3 LightDir[MAX_LIGHTS];

/*varying vec3 VertexPosition;*/
varying vec3 Position;
varying vec2 Wrap;
varying vec3 Normal;
varying float Depth;
/*varying vec3 Eye;*/
/*varying vec4 LightPosEye;*/
/*varying vec3 LightDir;*/

uniform sampler2D Texture;
/*uniform vec3 LightAmbient;*/
/*uniform vec3 LightDiffuse;*/
/*uniform vec3 LightSpecular;*/
/*uniform vec3 LightAtten;*/
/*uniform float LightDist;*/
/*uniform mat4 NormalMatrix;*/
uniform mat4 View;

uniform vec3 MaterialAmbient = vec3(0.1, 0.1, 0.1);
uniform vec4 MaterialDiffuse = vec4(1.0, 1.0, 1.0, 1.0);
uniform vec3 MaterialSpecular = vec3(1.0, 1.0, 1.0);
uniform vec3 MaterialEmissive = vec3(0.0, 0.0, 0.0);
uniform float MaterialShininess = 64.0;

#define M_PI 3.1415926535897932384626433832795
#define M_TAU (M_PI * 2.0)

bool floatcmp(float a, float b, float e)
{
    return abs(a-b) < e;
}

void main()
{
    vec4 color = texture2D(Texture, Wrap);
    float e = 0.1; // threshold
    if(floatcmp(color.r, 1.0, e) &&
        floatcmp(color.g, 0.0, e) &&
        floatcmp(color.b, 1.0, e))
    {
        discard;
    }

    if(floatcmp(color.a, 0.0, e)) {
        discard;
    }
    
    vec3 n = normalize(Normal);
    vec4 fragcolor = vec4(0.0, 0.0, 0.0, 0.0);
    vec3 v = normalize(vec3(-Position));

    for(int i=0; i<NumLights; i++){
        vec3 s = normalize(LightDir[i]);
        float dist = length(LightDir[i]);
        vec3 r = reflect(-s,n);
        float atten = cos(clamp(dist/LightDist[i],0.0,1.0) * M_TAU / 4.0);
        float diff = max(dot(s,n),0.0);
        float spec = pow(max(dot(r,v), 0.0), MaterialShininess);
        fragcolor += color * atten * vec4(
            MaterialAmbient * LightAmbient[i] +
            MaterialDiffuse.rga * LightDiffuse[i] * diff +
            MaterialSpecular * LightSpecular[i] * spec,
            MaterialDiffuse.a
        );
    }
    
    /*gl_FragColor = fragcolor;*/
    gl_FragColor = mix(fragcolor, vec4(FogColor.rgb,1.0), FogColor.a * Depth) * Brightness;
}

