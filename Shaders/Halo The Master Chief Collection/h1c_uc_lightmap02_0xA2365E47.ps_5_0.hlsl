cbuffer cb0_buf : register(b0)
{
    float3 cb0_m0 : packoffset(c0);
    uint cb0_m1 : packoffset(c0.w);
    float cb0_m2 : packoffset(c1);
    float2 cb0_m3 : packoffset(c1.y);
    uint cb0_m4 : packoffset(c1.w);
    float3 cb0_m5 : packoffset(c2);
    float cb0_m6 : packoffset(c2.w);
};

SamplerState s0 : register(s0);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t2 : register(t2);
TextureCube<float4> t3 : register(t3);

static float4 COLOR;
static float4 TEXCOORD;
static float4 TEXCOORD2;
static float4 TEXCOORD3;
static float4 SV_TARGET;

struct SPIRV_Cross_Input
{
    // float4 COLOR : TEXCOORD1;
    // float4 TEXCOORD : TEXCOORD3;
    // float4 TEXCOORD2 : TEXCOORD5;
    // float4 TEXCOORD3 : TEXCOORD6;
    float4 v0 : SV_POSITION0;
    float4 v1 : COLOR0;
    float4 v2 : COLOR1;
    float4 v3 : TEXCOORD0;
    float4 v4 : TEXCOORD1;
    float4 v5 : TEXCOORD2;
    float4 v6 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 SV_TARGET : SV_Target0;
};

float dp3_f32(float3 a, float3 b)
{
    precise float _47 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _47));
}

void frag_main()
{
    float4 _68 = t0.Sample(s0, float2(TEXCOORD.x, TEXCOORD.y));
    float _72 = _68.w;
    float _100;
    float _101;
    float _102;
    if (cb0_m6 != 1.0f)
    {
        float4 _90 = t2.Sample(s2, float2(TEXCOORD2.x, TEXCOORD2.y));
        _100 = _90.z;
        _101 = _90.y;
        _102 = _90.x;
    }
    else
    {
        _100 = cb0_m5.z;
        _101 = cb0_m5.y;
        _102 = cb0_m5.x;
    }
    float4 _114 = t3.Sample(s3, float3(TEXCOORD3.x, TEXCOORD3.y, TEXCOORD3.z));
    float _117 = _114.z;
    float3 _125 = float3(mad(_114.x, 2.0f, -1.0f), mad(_114.y, 2.0f, -1.0f), mad(_117, 2.0f, -1.0f));
    float _126 = dp3_f32(float3(mad(_68.x, 2.0f, -1.0f), mad(_68.y, 2.0f, -1.0f), mad(_68.z, 2.0f, -1.0f)), _125);
    float _147 = (cb0_m3.y == 0.0f) ? 1.0f : ((cb0_m3.x == 0.0f) ? (mad(_126, COLOR.w, 1.0f) - COLOR.w) : mad(COLOR.w, mad(rsqrt(dp3_f32(_125, _125)), mad(-_117, 2.0f, 1.0f), _126 + 1.0f) - 1.0f, 1.0f));
    SV_TARGET.x = (cb0_m0.x * _102) * _147;
    SV_TARGET.y = (_101 * cb0_m0.y) * _147;
    SV_TARGET.z = (cb0_m0.z * _100) * _147;
    if ((_72 - cb0_m2) < 0.0f)
    {
        discard;
    }
    SV_TARGET.w = _72;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    COLOR = stage_input.v1;
    TEXCOORD = stage_input.v3;
    TEXCOORD2 = stage_input.v5;
    TEXCOORD3 = stage_input.v6;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}


// 
// cbuffer _cb0 : register(b0)
// {
//   float4 c_material_color : packoffset(c0);
//   float c_alpha_ref : packoffset(c1);
//   float c_bump_attenuation_blending_v2 : packoffset(c1.y);
//   float c_lightmap_incident_radiosity_enabled : packoffset(c1.z);
//   float unused1_3 : packoffset(c1.w);
//   float4 c_debug_lightmap_ambient : packoffset(c2);
// }
// 
// SamplerState TexSampler0_s : register(s0);
// SamplerState TexSampler2_s : register(s2);
// SamplerState TexSampler3_s : register(s3);
// Texture2D<float4> Texture0 : register(t0);
// Texture2D<float4> Texture2 : register(t2);
// TextureCube<float4> Texture3 : register(t3);
// 
// 
// // 3Dmigoto declarations
// #define cmp -
// 
// //TODO: Failed decomp
// void main(
//   float4 v0 : SV_POSITION0,
//   float4 v1 : COLOR0,
//   float4 v2 : COLOR1,
//   float4 v3 : TEXCOORD0,
//   float4 v4 : TEXCOORD1,
//   float4 v5 : TEXCOORD2,
//   float4 v6 : TEXCOORD3,
//   out float4 o0 : SV_TARGET0)
// {
//   float4 r0,r1,r2;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.xyzw = Texture0.Sample(TexSampler0_s, v3.xy).xyzw;
//   r1.x = cmp(c_debug_lightmap_ambient.w != 1.000000);
//   if (r1.x != 0) {
//     r1.xyz = Texture2.Sample(TexSampler2_s, v5.xy).xyz;
//   } else {
//     r1.xyz = c_debug_lightmap_ambient.xyz;
//   }
//   r2.xyz = Texture3.Sample(TexSampler3_s, v6.xyz).xyz;
//   r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
//   r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
//   r0.x = dot(r0.xyz, r2.xyz);
//   r0.yz = cmp(c_bump_attenuation_blending_v2 == float2(0,0));
//   r1.w = r0.x * v1.w + 1;
//   r1.w = -v1.w + r1.w;
//   r2.x = dot(r2.xyz, r2.xyz);
//   r2.x = rsqrt(r2.x);
//   r0.x = 1 + r0.x;
//   r0.x = -r2.z * r2.x + r0.x;
//   r0.x = -1 + r0.x;
//   r0.x = v1.w * r0.x + 1;
//   r0.x = r0.y ? r1.w : r0.x;
//   r0.x = r0.z ? 1 : r0.x;
//   r1.xyz = c_material_color.xyz * r1.xyz;
// 
//   o0.xyz = r1.xyz * r0.xxx;
//   r0.x = -c_alpha_ref + r0.w;
//   r0.x = cmp(r0.x < 0);
//   if (r0.x != 0) discard;
//   o0.w = r0.w; o0.w = saturate(o0.w);
// 
//   // o0.xyz = max(o0.xyz, 0);
//   // o0.w = saturate(o0.w);
//   return;
// }