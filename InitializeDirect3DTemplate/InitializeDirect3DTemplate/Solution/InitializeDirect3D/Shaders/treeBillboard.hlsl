// treeBillboard.hlsl — foliage as billboards (point list, geometry shader expands to two crossed quads).
// C++: BuildTreeBillboardGeometry stores half-width and half-height in vertex TexC; lighting matches color.hlsl.

#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 3
#define NUM_SPOT_LIGHTS 0
#define MaxLights 16

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float gAlpha;
    float3 gBaseColorMul;
    float gBaseColorMulPad;
    float gWaterSurfaceUv; // unused; matches color.hlsl / C++ padding
    float gPadCb0;
    float gPadCb1;
};

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
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);
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
    if (d > L.FalloffEnd)
        return 0.0f;
    lightVec /= d;
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;
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
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
#endif
#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
#endif
    return float4(result, 0.0f);
}

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct GSIn
{
    float3 CenterW : POSITION;
    float2 HalfSize : TEXCOORD0;
};

GSIn VS(VertexIn vin)
{
    GSIn o;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    o.CenterW = posW.xyz;
    o.HalfSize = vin.TexC;
    return o;
}

struct PSIn
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gsamAnisotropicWrap : register(s0);

void EmitQuad(inout TriangleStream<PSIn> triStream, float3 c, float3 axisW, float3 axisH, float3 nFace)
{
    float3 p0 = c - axisW - axisH;
    float3 p1 = c + axisW - axisH;
    float3 p2 = c - axisW + axisH;
    float3 p3 = c + axisW + axisH;
    float2 uv0 = float2(0.0f, 1.0f);
    float2 uv1 = float2(1.0f, 1.0f);
    float2 uv2 = float2(0.0f, 0.0f);
    float2 uv3 = float2(1.0f, 0.0f);
    PSIn o0, o1, o2, o3, o4, o5;
    o0.PosW = p0; o0.PosH = mul(float4(p0, 1.0f), gViewProj); o0.NormalW = nFace; o0.TexC = uv0;
    o1.PosW = p1; o1.PosH = mul(float4(p1, 1.0f), gViewProj); o1.NormalW = nFace; o1.TexC = uv1;
    o2.PosW = p2; o2.PosH = mul(float4(p2, 1.0f), gViewProj); o2.NormalW = nFace; o2.TexC = uv2;
    o3.PosW = p1; o3.PosH = mul(float4(p1, 1.0f), gViewProj); o3.NormalW = nFace; o3.TexC = uv1;
    o4.PosW = p3; o4.PosH = mul(float4(p3, 1.0f), gViewProj); o4.NormalW = nFace; o4.TexC = uv3;
    o5.PosW = p2; o5.PosH = mul(float4(p2, 1.0f), gViewProj); o5.NormalW = nFace; o5.TexC = uv2;
    triStream.Append(o0);
    triStream.Append(o1);
    triStream.Append(o2);
    triStream.RestartStrip();
    triStream.Append(o3);
    triStream.Append(o4);
    triStream.Append(o5);
}

[maxvertexcount(12)]
void GS(point GSIn input[1], inout TriangleStream<PSIn> triStream)
{
    float3 c = input[0].CenterW;
    float hw = input[0].HalfSize.x;
    float hh = input[0].HalfSize.y;
    float3 look = normalize(gEyePosW - c);
    float3 upW = float3(0.0f, 1.0f, 0.0f);
    float3 right = normalize(cross(upW, look));
    if (dot(right, right) < 1e-8f)
        right = float3(1.0f, 0.0f, 0.0f);
    float3 right2 = float3(-right.z, 0.0f, right.x);
    if (dot(right2, right2) < 1e-8f)
        right2 = float3(0.0f, 0.0f, 1.0f);
    else
        right2 = normalize(right2);

    float3 n1 = normalize(cross(right, upW));
    if (dot(n1, look) < 0.0f)
        n1 = -n1;
    float3 n2 = normalize(cross(right2, upW));
    if (dot(n2, look) < 0.0f)
        n2 = -n2;

    EmitQuad(triStream, c, right * hw, upW * hh, n1);
    EmitQuad(triStream, c, right2 * hw, upW * hh, n2);
}

float4 PS(PSIn pin) : SV_Target
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC);
    if (diffuseAlbedo.a < 0.25f)
        discard;
    diffuseAlbedo.rgb *= gBaseColorMul;
    float crown = saturate(pin.TexC.y);
    diffuseAlbedo.rgb *= lerp(0.45f, 1.0f, crown);
    diffuseAlbedo.rgb = lerp(diffuseAlbedo.rgb, diffuseAlbedo.rgb * float3(0.55f, 0.75f, 0.35f), 0.22f * (1.0f - crown));
    float3 normalW = normalize(gEyePosW - pin.PosW);
    float3 toEyeW = gEyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;

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
