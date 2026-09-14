cbuffer cb0_buf : register(b0)
{
    float3 cb0_m0 : packoffset(c0);
    uint cb0_m1 : packoffset(c0.w);
    float4 cb0_m2 : packoffset(c1);
    float3 cb0_m3 : packoffset(c2);
    uint cb0_m4 : packoffset(c2.w);
    float3 cb0_m5 : packoffset(c3);
    uint cb0_m6 : packoffset(c3.w);
    float3 cb0_m7 : packoffset(c4);
    uint cb0_m8 : packoffset(c4.w);
    float3 cb0_m9 : packoffset(c5);
    uint cb0_m10 : packoffset(c5.w);
    float cb0_m11 : packoffset(c6);
    float2 cb0_m12 : packoffset(c6.y);
    uint cb0_m13 : packoffset(c6.w);
    float3 cb0_m14 : packoffset(c7);
    float cb0_m15 : packoffset(c7.w);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
TextureCube<float4> t3 : register(t3);

static float4 COLOR;
static float4 TEXCOORD;
static float4 TEXCOORD1;
static float4 TEXCOORD2;
static float4 TEXCOORD3;
static float4 SV_TARGET;

struct SPIRV_Cross_Input
{
//     float4 COLOR : TEXCOORD1;
//     float4 TEXCOORD : TEXCOORD3;
//     float4 TEXCOORD1 : TEXCOORD4;
//     float4 TEXCOORD2 : TEXCOORD5;
//     float4 TEXCOORD3 : TEXCOORD6;
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
    precise float _59 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _59));
}

void frag_main()
{
    float4 _80 = t0.Sample(s0, float2(TEXCOORD.x, TEXCOORD.y));
    float _84 = _80.w;
    float4 _93 = t1.Sample(s1, float2(TEXCOORD1.x, TEXCOORD1.y));
    float _94 = _93.x;
    float _95 = _93.y;
    float _96 = _93.z;
    float _97 = _93.w;
    float _125;
    float _126;
    float _127;
    if (cb0_m15 != 1.0f)
    {
        float4 _115 = t2.Sample(s2, float2(TEXCOORD2.x, TEXCOORD2.y));
        _125 = _115.z;
        _126 = _115.y;
        _127 = _115.x;
    }
    else
    {
        _125 = cb0_m14.z;
        _126 = cb0_m14.y;
        _127 = cb0_m14.x;
    }
    float4 _139 = t3.Sample(s3, float3(TEXCOORD3.x, TEXCOORD3.y, TEXCOORD3.z));
    float _142 = _139.z;
    float _147 = (cb0_m2.x + 0.5f) - max(_97, 0.0f);
    float _152 = (_147 < 0.5f) ? _147 : ((_97 + 0.5f) - max(cb0_m2.x, 0.0f));
    float _155 = min((_152 * _152) * 4.0f, 1.0f);
    float _156 = _155 * _155;
    bool _157 = _156 < 0.5f;
    float _158 = mad(_156, 2.0f, -1.0f);
    float _159 = _158 * _158;
    float _160 = mad(_80.x, 2.0f, -1.0f);
    float _161 = mad(_80.y, 2.0f, -1.0f);
    float _162 = mad(_80.z, 2.0f, -1.0f);
    float3 _163 = float3(_160, _161, _162);
    float _165 = rsqrt(dp3_f32(_163, _163));
    float _169 = mad(_139.x, 2.0f, -1.0f);
    float _170 = mad(_139.y, 2.0f, -1.0f);
    float _171 = mad(_142, 2.0f, -1.0f);
    float3 _172 = float3(_169, _170, _171);
    float _174 = rsqrt(dp3_f32(_172, _172));
    float _180 = dp3_f32(float3(_160 * _165, _161 * _165, _162 * _165), float3(_169 * _174, _170 * _174, _171 * _174));
    float _198 = (cb0_m12.y == 0.0f) ? 1.0f : mad(((cb0_m12.x == 0.0f) ? clamp(_180, 0.0f, 10000000) : mad(_174, mad(-_142, 2.0f, 1.0f), _180 + 1.0f)) - 1.0f, COLOR.w, 1.0f);
    SV_TARGET.x = mad(_127, cb0_m0.x * _198, clamp(mad(_96, clamp(cb0_m9.x + (_157 ? 0.0f : (_159 * cb0_m7.x)), 0.0f, 10000000), clamp((_95 * cb0_m5.x) + (_94 * cb0_m3.x), 0.0f, 10000000)), 0.0f, 10000000));
    SV_TARGET.y = mad(_126, cb0_m0.y * _198, clamp(mad(_96, clamp(cb0_m9.y + (_157 ? 0.0f : (cb0_m7.y * _159)), 0.0f, 10000000), clamp((_94 * cb0_m3.y) + (_95 * cb0_m5.y), 0.0f, 10000000)), 0.0f, 10000000));
    SV_TARGET.z = mad(cb0_m0.z * _198, _125, clamp(mad(_96, clamp(cb0_m9.z + (_157 ? 0.0f : (_159 * cb0_m7.z)), 0.0f, 10000000), clamp((_94 * cb0_m3.z) + (_95 * cb0_m5.z), 0.0f, 10000000)), 0.0f, 10000000));
    if ((_84 - cb0_m11) < 0.0f)
    {
        discard;
    }
    SV_TARGET.w = _84;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    COLOR = stage_input.v1;
    TEXCOORD = stage_input.v3;
    TEXCOORD1 = stage_input.v4;
    TEXCOORD2 = stage_input.v5;
    TEXCOORD3 = stage_input.v6;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}


// cbuffer _cb0 : register(b0)
// {
//   float4 c_material_color : packoffset(c0);
//   float4 c_plasma_animation : packoffset(c1);
//   float4 c_primary_color : packoffset(c2);
//   float4 c_secondary_color : packoffset(c3);
//   float4 c_plasma_on_color : packoffset(c4);
//   float4 c_plasma_off_color : packoffset(c5);
//   float c_alpha_ref : packoffset(c6);
//   float c_bump_attenuation_blending_v2 : packoffset(c6.y);
//   float c_lightmap_incident_radiosity_enabled : packoffset(c6.z);
//   float unused6_3 : packoffset(c6.w);
//   float4 c_debug_lightmap_ambient : packoffset(c7);
// }
// 
// SamplerState TexSampler0_s : register(s0);
// SamplerState TexSampler1_s : register(s1);
// SamplerState TexSampler2_s : register(s2);
// SamplerState TexSampler3_s : register(s3);
// Texture2D<float4> Texture0 : register(t0);
// Texture2D<float4> Texture1 : register(t1);
// Texture2D<float4> Texture2 : register(t2);
// TextureCube<float4> Texture3 : register(t3);
// 
// 
// // 3Dmigoto declarations
// #define cmp -
// 
// 
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
//   float4 r0,r1,r2,r3,r4;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.xyzw = Texture0.Sample(TexSampler0_s, v3.xy).xyzw;
//   r1.xyzw = Texture1.Sample(TexSampler1_s, v4.xy).xyzw;
//   
//   r2.x = cmp(c_debug_lightmap_ambient.w != 1.000000);
//   if (r2.x != 0) {
//     r2.xyz = Texture2.Sample(TexSampler2_s, v5.xy).xyz;
//   } else {
//     r2.xyz = c_debug_lightmap_ambient.xyz;
//   }
//   r3.xyz = Texture3.Sample(TexSampler3_s, v6.xyz).xyz;
//   r2.w = 0.5 + c_plasma_animation.x;
//   r3.w = max(0, r1.w);
//   r2.w = -r3.w + r2.w;
//   r1.w = 0.5 + r1.w;
//   r3.w = max(0, c_plasma_animation.x);
//   r1.w = -r3.w + r1.w;
//   r3.w = cmp(r2.w < 0.5);
//   r1.w = r3.w ? r2.w : r1.w;
//   r1.w = r1.w * r1.w;
//   r1.w = 4 * r1.w;
//   r1.w = min(1, r1.w);
//   r1.w = r1.w * r1.w;
//   r2.w = cmp(r1.w < 0.5);
//   r1.w = r1.w * 2 + -1;
//   r1.w = r1.w * r1.w;
//   r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
//   r3.w = dot(r0.xyz, r0.xyz);
//   r3.w = rsqrt(r3.w);
//   r0.xyz = r3.www * r0.xyz;
//   r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
//   r3.w = dot(r3.xyz, r3.xyz);
//   r3.w = rsqrt(r3.w);
//   r4.xyz = r3.xyz * r3.www;
//   r0.x = dot(r0.xyz, r4.xyz);
//   r0.yz = cmp(c_bump_attenuation_blending_v2 == float2(0,0));
//   r3.x = saturate(r0.x);
//   r0.x = 1 + r0.x;
//   r0.x = -r3.z * r3.w + r0.x;
//   r0.x = r0.y ? r3.x : r0.x;
//   r0.x = -1 + r0.x;
//   r0.x = v1.w * r0.x + 1;
//   r0.x = r0.z ? 1 : r0.x;
//   r3.xyz = c_secondary_color.xyz * r1.yyy;
//   r3.xyz = /* saturate */(c_primary_color.xyz * r1.xxx + r3.xyz); r3.xyz = max(r3.xyz, 0);
//   r1.xyw = c_plasma_on_color.xyz * r1.www;
//   r1.xyw = r2.www ? float3(0,0,0) : r1.xyw;
//   r1.xyw = /* saturate */(c_plasma_off_color.xyz + r1.xyw); r1.xyw = max(r1.xyw, 0);
//   r1.xyz = /* saturate */(r1.xyw * r1.zzz + r3.xyz); r1.xyz = max(r1.xyz, 0);
//   r0.xyz = c_material_color.xyz * r0.xxx;
//   o0.xyz = r0.xyz * r2.xyz + r1.xyz;
// 
//   r0.x = -c_alpha_ref + r0.w;
//   r0.x = cmp(r0.x < 0);
//   if (r0.x != 0) discard;
//   o0.w = r0.w; o0.w = saturate(o0.w);
// 
//   return;
// }