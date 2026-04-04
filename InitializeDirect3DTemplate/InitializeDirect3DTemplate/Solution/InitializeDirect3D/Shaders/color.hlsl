// color.hlsl — based on Frank Luna (C) 2015.
// Textured Blinn-Phong: gDiffuseMap (t0), gLights and gAmbientLight from cbPass.
// Transparent water (gAlpha < 1): same texture; UVs are animated in the pixel shader using gTotalTime (see below).

#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 4
#define NUM_SPOT_LIGHTS 0

#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    float3 Position;    // point light only
    float SpotPower;    // spot light only
};

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform; // scales UVs (e.g. grass tiling); identity for most meshes
    float gAlpha;            // 1 = opaque; less than 1 = alpha-blended (water). Set from C++.
    float3 gBaseColorMul;    // multiply albedo after sampling (e.g. darken trench floor)
    float gBaseColorMulPad;
    float gWaterSurfaceUv;   // unused here; keeps cbuffer layout aligned with treeBillboard.hlsl
    float gPadCb0;
    float gPadCb1;
};

// Per-pass data (camera + lighting).
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;

    float4 gAmbientLight;
    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights.
    Light gLights[MaxLights];
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear falloff.
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);
    float3 specAlbedo = fresnelFactor * roughnessFactor;

    specAlbedo = specAlbedo / (specAlbedo + 1.0f); // LDR scaling
    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    float3 lightVec = -L.Direction;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 lightVec = L.Position - pos;
    float d = length(lightVec);

    // Range test.
    if (d > L.FalloffEnd)
        return 0.0f;

    lightVec /= d; // normalize

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // Not used in this project (NUM_SPOT_LIGHTS = 0).
    float3 lightVec = L.Position - pos;
    float d = length(lightVec);
    if (d > L.FalloffEnd)
        return 0.0f;

    lightVec /= d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float4 ComputeLighting(Light gLights[MaxLights], Material mat,
    float3 pos, float3 normal, float3 toEye,
    float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;
#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

    return float4(result, 0.0f);
}

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Assumes non-uniform scaling; for a production pipeline you would use inverse-transpose.
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);

    vout.PosH = mul(posW, gViewProj);
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = texC.xy;
    return vout;
}

Texture2D gDiffuseMap : register(t0);
SamplerState gsamAnisotropicWrap : register(s0);

float4 PS(VertexOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float4 diffuseAlbedo;
    if (gAlpha < 1.0f)
    {
        // Moving water: sample world XZ so motion matches across moat/ocean tiles; mip 0 avoids streaky LOD on thin strips.
        // gTotalTime scrolls the pattern; tweak the float constants for speed or scale. rgb tint adjusts perceived water color.
        float2 flow = pin.PosW.xz * 0.09f + float2(gTotalTime * 0.11f, gTotalTime * 0.085f);
        float2 ripple = 0.028f * float2(
            sin(flow.x * 2.0f + gTotalTime * 1.7f),
            cos(flow.y * 1.85f + gTotalTime * 1.5f));
        uv = flow + ripple;
        diffuseAlbedo = gDiffuseMap.SampleLevel(gsamAnisotropicWrap, uv, 0.0f);
        diffuseAlbedo.rgb *= float3(0.66f, 0.76f, 0.91f);
    }
    else
    {
        diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, uv);
        // Grass (boost green via gBaseColorMul on ground tiles): world-space variation so flats look less like a single sheet.
        float3 nUp = normalize(pin.NormalW);
        float grassish = step(1.02f, gBaseColorMul.g) * step(gBaseColorMul.r, 0.93f);
        float flatGround = saturate((nUp.y - 0.78f) * 4.5f) * grassish;
        if (flatGround > 0.001f)
        {
            // Wild / unkempt: very large low-frequency patches + strong wobble + heavy thatch + dead/brown flecks.
            float2 gw = pin.PosW.xz * 0.11f;
            float wobble = 0.38f * sin(gw.x * 1.05f + gTotalTime * 1.35f) * sin(gw.y * 0.88f - gTotalTime * 1.05f);
            float tallTuft = 0.26f * sin(pin.PosW.x * 0.18f + pin.PosW.z * 0.16f) * sin(pin.PosW.z * 0.21f - pin.PosW.x * 0.09f);
            float thatch = 0.22f * (0.5f + 0.5f * sin(dot(pin.PosW.xz, float2(1.15f, 0.86f)) * 1.85f + gTotalTime * 0.12f));
            float weedClump = 0.12f * sin(dot(pin.PosW.xz, float2(2.1f, 1.7f)) * 0.65f + gTotalTime * 0.08f);
            float speck = 0.14f * (frac(sin(dot(pin.PosW.xz, float2(12.9898f, 78.233f))) * 43758.5453f) - 0.5f);
            float brownDead = 0.18f * saturate(0.5f + 0.5f * sin(pin.PosW.x * 0.41f + pin.PosW.z * 0.37f + gTotalTime * 0.02f));
            diffuseAlbedo.rgb *= (1.0f + (wobble + tallTuft + speck + weedClump - thatch - brownDead) * flatGround);
            diffuseAlbedo.rgb = lerp(diffuseAlbedo.rgb, diffuseAlbedo.rgb * float3(0.72f, 1.22f, 0.55f), 0.42f * flatGround);
            diffuseAlbedo.rgb = lerp(diffuseAlbedo.rgb, diffuseAlbedo.rgb * float3(0.62f, 0.88f, 0.42f), 0.14f * brownDead * flatGround);
        }
    }
    diffuseAlbedo.rgb *= gBaseColorMul;

    // Interpolating normal can unnormalize it.
    float3 normalW = normalize(pin.NormalW);

    // Vector from point being lit to eye (normalized).
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;

    // Fixed material constants (default lit path).
    static const float3 gFresnelR0 = float3(0.02f, 0.02f, 0.02f);
    static const float gRoughness = 0.35f;
    const float shininess = 1.0f - gRoughness;

    Material mat;
    mat.DiffuseAlbedo = diffuseAlbedo;
    mat.FresnelR0 = gFresnelR0;
    mat.Shininess = shininess;

    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);

    float4 ambient = gAmbientLight * diffuseAlbedo;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, normalW, toEyeW, shadowFactor);
    float4 litColor = ambient + directLight;

    litColor.a = gAlpha;
    return litColor;
}

