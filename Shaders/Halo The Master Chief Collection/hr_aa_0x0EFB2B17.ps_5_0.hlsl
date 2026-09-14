/*
// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:39:33 2026

cbuffer PostProcessPS : register(b0)
{
  float4 pixel_size : packoffset(c0);
  float4 scale : packoffset(c1);
  float4x3 p_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 p_postprocess_contrast : packoffset(c5);
}

SamplerState LocalSampler_source_sampler_s : register(s0);
Texture2D<float4> LocalTexture_source_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, v1.xy, 0).xyzw;
  r1.xyz = LocalTexture_source_sampler.Gather(LocalSampler_source_sampler_s, v1.xy).xyz;
  r2.xyz = LocalTexture_source_sampler.Gather(LocalSampler_source_sampler_s, v1.xy, int2(-1, -1)).xzw;
  r1.w = max(r1.x, r0.w);
  r2.w = min(r1.x, r0.w);
  r1.w = max(r1.z, r1.w);
  r2.w = min(r2.w, r1.z);
  r3.x = max(r2.y, r2.x);
  r3.y = min(r2.y, r2.x);
  r1.w = max(r3.x, r1.w);
  r2.w = min(r3.y, r2.w);
  r3.x = 0.165999994 * r1.w;
  r1.w = -r2.w + r1.w;
  r2.w = max(0.0833000019, r3.x);
  r2.w = cmp(r1.w >= r2.w);
  if (r2.w != 0) {
    r2.w = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, v1.xy, 0, int2(1, -1)).w;
    r3.x = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, v1.xy, 0, int2(-1, 1)).w;
    r3.yz = r2.yx + r1.xz;
    r1.w = 1 / r1.w;
    r3.w = r3.y + r3.z;
    r3.yz = r0.ww * float2(-2,-2) + r3.yz;
    r4.x = r2.w + r1.y;
    r2.w = r2.z + r2.w;
    r4.y = r1.z * -2 + r4.x;
    r2.w = r2.y * -2 + r2.w;
    r2.z = r3.x + r2.z;
    r1.y = r3.x + r1.y;
    r3.x = abs(r3.y) * 2 + abs(r4.y);
    r2.w = abs(r3.z) * 2 + abs(r2.w);
    r3.y = r2.x * -2 + r2.z;
    r1.y = r1.x * -2 + r1.y;
    r3.x = abs(r3.y) + r3.x;
    r1.y = abs(r1.y) + r2.w;
    r2.z = r2.z + r4.x;
    r1.y = cmp(r3.x >= r1.y);
    r2.z = r3.w * 2 + r2.z;
    r2.x = r1.y ? r2.y : r2.x;
    r1.x = r1.y ? r1.x : r1.z;
    r1.z = r1.y ? pixel_size.y : pixel_size.x;
    r2.y = r2.z * 0.0833333358 + -r0.w;
    r2.z = r2.x + -r0.w;
    r2.w = r1.x + -r0.w;
    r2.x = r2.x + r0.w;
    r1.x = r1.x + r0.w;
    r3.x = cmp(abs(r2.z) >= abs(r2.w));
    r2.z = max(abs(r2.z), abs(r2.w));
    r1.z = r3.x ? -r1.z : r1.z;
    r1.w = saturate(abs(r2.y) * r1.w);
    r2.y = r1.y ? pixel_size.x : 0;
    r2.w = r1.y ? 0 : pixel_size.y;
    r3.yz = r1.zz * float2(0.5,0.5) + v1.xy;
    r3.y = r1.y ? v1.x : r3.y;
    r3.z = r1.y ? r3.z : v1.y;
    r4.x = -r2.y * 1.5 + r3.y;
    r4.y = -r2.w * 1.5 + r3.z;
    r5.x = r2.y * 1.5 + r3.y;
    r5.y = r2.w * 1.5 + r3.z;
    r3.y = r1.w * -2 + 3;
    r3.z = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, r4.xy, 0).w;
    r1.w = r1.w * r1.w;
    r3.w = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, r5.xy, 0).w;
    r1.x = r3.x ? r2.x : r1.x;
    r2.x = 0.25 * r2.z;
    r2.z = -r1.x * 0.5 + r0.w;
    r1.w = r3.y * r1.w;
    r2.z = cmp(r2.z < 0);
    r3.x = -r1.x * 0.5 + r3.z;
    r3.y = -r1.x * 0.5 + r3.w;
    r3.zw = cmp(abs(r3.xy) >= r2.xx);
    r4.z = -r2.y * 3 + r4.x;
    r4.x = r3.z ? r4.x : r4.z;
    r4.w = -r2.w * 3 + r4.y;
    r4.z = r3.z ? r4.y : r4.w;
    r4.yw = ~(int2)r3.zw;
    r4.y = (int)r4.w | (int)r4.y;
    r4.w = r2.y * 3 + r5.x;
    r5.x = r3.w ? r5.x : r4.w;
    r4.w = r2.w * 3 + r5.y;
    r5.z = r3.w ? r5.y : r4.w;
    if (r4.y != 0) {
      if (r3.z == 0) {
        r3.x = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, r4.xz, 0).w;
      }
      if (r3.w == 0) {
        r3.y = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, r5.xz, 0).w;
      }
      r4.y = -r1.x * 0.5 + r3.x;
      r3.x = r3.z ? r3.x : r4.y;
      r1.x = -r1.x * 0.5 + r3.y;
      r3.y = r3.w ? r3.y : r1.x;
      r3.zw = cmp(abs(r3.xy) >= r2.xx);
      r1.x = -r2.y * 12 + r4.x;
      r4.x = r3.z ? r4.x : r1.x;
      r1.x = -r2.w * 12 + r4.z;
      r4.z = r3.z ? r4.z : r1.x;
      r1.x = r2.y * 12 + r5.x;
      r5.x = r3.w ? r5.x : r1.x;
      r1.x = r2.w * 12 + r5.z;
      r5.z = r3.w ? r5.z : r1.x;
    }
    r1.x = v1.x + -r4.x;
    r2.y = v1.y + -r4.z;
    r1.x = r1.y ? r1.x : r2.y;
    r2.xy = -v1.xy + r5.xz;
    r2.x = r1.y ? r2.x : r2.y;
    r2.yw = cmp(r3.xy < float2(0,0));
    r3.x = r2.x + r1.x;
    r2.yz = cmp((int2)r2.zz != (int2)r2.yw);
    r2.w = 1 / r3.x;
    r3.x = cmp(r1.x < r2.x);
    r1.x = min(r2.x, r1.x);
    r2.x = r3.x ? r2.y : r2.z;
    r1.w = r1.w * r1.w;
    r1.x = r1.x * -r2.w + 0.5;
    r1.w = 0.25 * r1.w;
    r1.x = (int)r1.x & (int)r2.x;
    r1.x = max(r1.x, r1.w);
    r1.xz = r1.xx * r1.zz + v1.xy;
    r2.x = r1.y ? v1.x : r1.x;
    r2.y = r1.y ? r1.z : v1.y;
    r0.xyz = LocalTexture_source_sampler.SampleLevel(LocalSampler_source_sampler_s, r2.xy, 0).xyz;
  }
  o0.xyzw = r0.xyzw;
  return;
}
*/

cbuffer cb0_buf : register(b0)
{
    uint cb0_m0 : packoffset(c0);
    float3 cb0_m1 : packoffset(c0.y);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);

static float2 TEXCOORD;
static float4 SV_Target;

#include "./Includes/Common.hlsl"

struct SPIRV_Cross_Input
{
  float4 v0 : SV_Position0;
  float4 v1 : TEXCOORD0;
  float4 v2 : TEXCOORD1;
  float4 v3 : TEXCOORD2;
  float4 v4 : TEXCOORD3;
  float4 v5 : TEXCOORD4;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

void frag_main()
{
    float2 _60 = float2(TEXCOORD.x, TEXCOORD.y);
    float4 _63 = t0.SampleLevel(s0, _60, 0.0f);
    
    #if ALLOW_AA == 0
        SV_Target = _63;
        return;
    #endif

    float _67 = _63.w;
    float4 _69 = t0.GatherAlpha(s0, _60);
    float _70 = _69.x;
    float _71 = _69.y;
    float _72 = _69.z;
    float4 _74 = t0.GatherAlpha(s0, _60, int2(-1, -1));
    float _75 = _74.x;
    float _76 = _74.z;
    float _77 = _74.w;
    float _85 = max(max(_72, max(_67, _70)), max(_75, _76));
    float _88 = _85 - min(min(_72, min(_67, _70)), min(_75, _76));
    float _275;
    float _276;
    float _277;
    if (_88 >= max(_85 * 0.16599999368190765380859375f, 0.083300001919269561767578125f))
    {
        float4 _95 = t0.SampleLevel(s0, _60, 0.0f, int2(1, -1));
        float _96 = _95.w;
        float4 _98 = t0.SampleLevel(s0, _60, 0.0f, int2(-1, 1));
        float _99 = _98.w;
        float _100 = _70 + _76;
        float _101 = _72 + _75;
        float _106 = _71 + _96;
        float _110 = _77 + _99;
        bool _125 = (mad(abs(mad(_67, -2.0f, _100)), 2.0f, abs(mad(_72, -2.0f, _106))) + abs(mad(_75, -2.0f, _110))) >= (mad(abs(mad(_67, -2.0f, _101)), 2.0f, abs(mad(_76, -2.0f, _77 + _96))) + abs(mad(_70, -2.0f, _71 + _99)));
        float _127 = _125 ? _76 : _75;
        float _128 = _125 ? _70 : _72;
        float _136 = asfloat(cb0_m0);
        float _137 = _125 ? cb0_m1.x : _136;
        float _144 = abs(_127 - _67);
        float _145 = abs(_128 - _67);
        bool _146 = _144 >= _145;
        float _149 = _146 ? (-_137) : _137;
        float _152 = clamp((1.0f / _88) * abs(mad(mad(_101 + _100, 2.0f, _106 + _110), 0.083333335816860198974609375f, -_67)), 0.0f, 1.0f);
        float _153 = _125 ? 0.0f : cb0_m1.x;
        float _156 = _125 ? TEXCOORD.x : mad(_149, 0.5f, TEXCOORD.x);
        float _157 = _125 ? mad(_149, 0.5f, TEXCOORD.y) : TEXCOORD.y;
        float _158 = _125 ? _136 : 0.0f;
        float _159 = mad(_158, -1.5f, _156);
        float _160 = mad(_153, -1.5f, _157);
        float _161 = mad(_158, 1.5f, _156);
        float _162 = mad(_153, 1.5f, _157);
        float _173 = _146 ? (_67 + _127) : (_67 + _128);
        float _174 = max(_144, _145) * 0.25f;
        float _176 = mad(_152, -2.0f, 3.0f) * (_152 * _152);
        bool _177 = mad(_173, -0.5f, _67) < 0.0f;
        float _178 = mad(_173, -0.5f, t0.SampleLevel(s0, float2(_159, _160), 0.0f).w);
        float _179 = mad(_173, -0.5f, t0.SampleLevel(s0, float2(_161, _162), 0.0f).w);
        bool _182 = abs(_178) >= _174;
        bool _183 = _174 <= abs(_179);
        float _186 = _182 ? _159 : mad(_158, -3.0f, _159);
        float _188 = _182 ? _160 : mad(_153, -3.0f, _160);
        float _193 = _183 ? _161 : mad(_158, 3.0f, _161);
        float _195 = _183 ? _162 : mad(_153, 3.0f, _162);
        float _228;
        float _229;
        float _230;
        float _231;
        float _232;
        float _233;
        if (!(_182 && _183))
        {
            float _204;
            if (!_182)
            {
                _204 = t0.SampleLevel(s0, float2(_186, _188), 0.0f).w;
            }
            else
            {
                _204 = _178;
            }
            float _211;
            if (!_183)
            {
                _211 = t0.SampleLevel(s0, float2(_193, _195), 0.0f).w;
            }
            else
            {
                _211 = _179;
            }
            float _213 = _182 ? _204 : mad(_173, -0.5f, _204);
            float _215 = _183 ? _211 : mad(_173, -0.5f, _211);
            bool _218 = _174 <= abs(_213);
            bool _219 = _174 <= abs(_215);
            _228 = _215;
            _229 = _213;
            _230 = _219 ? _195 : mad(_153, 12.0f, _195);
            _231 = _219 ? _193 : mad(_158, 12.0f, _193);
            _232 = _218 ? _188 : mad(_153, -12.0f, _188);
            _233 = _218 ? _186 : mad(_158, -12.0f, _186);
        }
        else
        {
            _228 = _179;
            _229 = _178;
            _230 = _195;
            _231 = _193;
            _232 = _188;
            _233 = _186;
        }
        float _236 = _125 ? (TEXCOORD.x - _233) : (TEXCOORD.y - _232);
        float _239 = _125 ? (_231 - TEXCOORD.x) : (_230 - TEXCOORD.y);
        bool _240 = _229 < 0.0f;
        bool _241 = _228 < 0.0f;
        bool _243 = !_177;
        bool _253 = _236 < _239;
        float _264 = max((_176 * _176) * 0.25f, ((((_177 && (!_240)) || (_243 && _240)) && _253) || (((_177 && (!_241)) || (_243 && _241)) && (!_253))) ? mad(-(1.0f / (_236 + _239)), min(_236, _239), 0.5f) : 0.0f);
        float4 _271 = t0.SampleLevel(s0, float2(_125 ? TEXCOORD.x : mad(_149, _264, TEXCOORD.x), _125 ? mad(_149, _264, TEXCOORD.y) : TEXCOORD.y), 0.0f);
        _275 = _271.z;
        _276 = _271.y;
        _277 = _271.x;
    }
    else
    {
        _275 = _63.z;
        _276 = _63.y;
        _277 = _63.x;
    }
    SV_Target.x = _277;
    SV_Target.y = _276;
    SV_Target.z = _275;
    SV_Target.w = _67;
}

#if XBOX360_CURVE == 1
#include "./Includes/Curve360.hlsl"
#endif

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.v1.xy;
    frag_main();

    SV_Target.xyz = max(SV_Target.xyz, 0);

    if (HDR_ENABLED) {
        SV_Target.xyz = RenderIntermediatePass_Decode(SV_Target.xyz);
    }
    
    #if XBOX360_CURVE == 1
        SV_Target.xyz = Curve360::FullCorrect(SV_Target.xyz);
    #endif

    if (HDR_ENABLED) {
        SV_Target.xyz *= HDR_INTSCALING;
        SV_Target.xyz = RenderIntermediatePass_Encode(SV_Target.xyz);
    }

    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
