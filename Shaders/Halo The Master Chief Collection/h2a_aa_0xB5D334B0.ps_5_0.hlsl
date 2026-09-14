/*
cbuffer CB_PS_COMMON : register(b0)
{
  float4 COMMON_LBUF_PARAMS : packoffset(c0);
  float4 COMMON_VIEW_POSITION : packoffset(c1);
  float4 COMMON_VIEWPROJ_MATRIX[4] : packoffset(c2);
  float4 COMMON_VIEW_POSITION_COLORCAM : packoffset(c6);
  float4 COMMON_VIEWPROJ_MATRIX_COLORCAM[4] : packoffset(c7);
  float4 COMMON_VIEWPROJ_REFLECTION[4] : packoffset(c11);
  float4 COMMON_OBLIQUE_MATR[4] : packoffset(c15);
  float4 VS_REG_COMMON_FOG_PARAMS[2] : packoffset(c19);
  float4 REG_COMMON_CLIPPING_PLANE : packoffset(c21);
  float4 COMMON_VP_PARAMS[2] : packoffset(c22);
  float4 PS_REG_COMMON_HDR_PARAMS : packoffset(c24);
  float4 PS_REG_COMMON_FOG_SUN_DIR : packoffset(c25);
  float4 PS_REG_COMMON_FOG_RAYLEIGH_FACTOR : packoffset(c26);
  float4 PS_REG_COMMON_FOG_COLOR : packoffset(c27);
  float4 PS_REG_COMMON_FOG_PLANE_MIRROR : packoffset(c28);
  float4 PS_REG_COMMON_FOG_ATMOSPHERE_0[6] : packoffset(c29);
  float4 PS_REG_COMMON_FOG_ATMOSPHERE_EXTRA : packoffset(c35);
  float4 PS_REG_COMMON_ELAPSED_TIME : packoffset(c36);
  float4 PS_REG_COMMON_AMBIENT : packoffset(c37);
  float4 PS_REG_COMMON_DEBUG_SHOW_LIGHTING : packoffset(c38);
  float4 VS_REG_COMMON_FOG_COLOR : packoffset(c39);
  float4 VS_REG_COMMON_FOG_SUN_DIR : packoffset(c40);
  float4 VS_REG_COMMON_FOG_RAYLEIGH_FACTOR : packoffset(c41);
  float4 VS_REG_COMMON_FOG_VOLUME_COUNT : packoffset(c42);
  float4 VS_REG_COMMON_FOG_VOL_START[32] : packoffset(c43);
}

cbuffer CB_PASS_EDGE_AA : register(b7)
{
  float4 PS_REG_EDGE_AA_PARAMS : packoffset(c0);
}

SamplerState PS_SAMPLERS_4__s : register(s4);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0);
*/

#include "./Includes/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    uint4 cb0_m1 : packoffset(c1);
    uint4 cb0_m2 : packoffset(c2);
    uint4 cb0_m3 : packoffset(c3);
    uint4 cb0_m4 : packoffset(c4);
    uint4 cb0_m5 : packoffset(c5);
    uint4 cb0_m6 : packoffset(c6);
    uint4 cb0_m7 : packoffset(c7);
    uint4 cb0_m8 : packoffset(c8);
    uint4 cb0_m9 : packoffset(c9);
    uint4 cb0_m10 : packoffset(c10);
    uint4 cb0_m11 : packoffset(c11);
    uint4 cb0_m12 : packoffset(c12);
    uint4 cb0_m13 : packoffset(c13);
    uint4 cb0_m14 : packoffset(c14);
    uint4 cb0_m15 : packoffset(c15);
    uint4 cb0_m16 : packoffset(c16);
    uint4 cb0_m17 : packoffset(c17);
    uint4 cb0_m18 : packoffset(c18);
    uint4 cb0_m19 : packoffset(c19);
    uint4 cb0_m20 : packoffset(c20);
    uint4 cb0_m21 : packoffset(c21);
    uint cb0_m22 : packoffset(c22);
    float3 cb0_m23 : packoffset(c22.y);
};

cbuffer cb7_buf : register(b7)
{
    float4 cb7_m : packoffset(c0);
};

SamplerState s4 : register(s4);
Texture2D<float4> t0 : register(t0);




static float2 TEXCOORD;
static float4 SV_Target;

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






float dp3_f32(float3 a, float3 b)
{
    precise float _72 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _72));
}

void frag_main()
{
    float2 _90 = float2(TEXCOORD.x, TEXCOORD.y);
    float4 _93 = t0.SampleLevel(s4, _90, 0.0f);

    #if ALLOW_AA == 0
        SV_Target = _93;
        return;
    #endif

    float _94 = _93.x;
    float _99 = dp3_f32(float3(_94, _93.yz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
    float _106 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(0, 1)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
    float _113 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(1, 0)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
    float _120 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(0, -1)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
    float _127 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(-1, 0)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
    float _134 = max(max(_120, _127), max(_113, max(_99, _106)));
    float _141 = _134 - min(min(_113, min(_99, _106)), min(_120, _127));
    if (_141 >= max(cb7_m.z, _134 * cb7_m.y))
    {
        float _156 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(-1, -1)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
        float _163 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(1, 1)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
        float _170 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(1, -1)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
        float _177 = dp3_f32(float3(t0.SampleLevel(s4, _90, 0.0f, int2(-1, 1)).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
        float _178 = _106 + _120;
        float _179 = _113 + _127;
        float _184 = _163 + _170;
        float _188 = _156 + _177;
        bool _203 = (mad(abs(mad(_99, -2.0f, _178)), 2.0f, abs(mad(_113, -2.0f, _184))) + abs(mad(_127, -2.0f, _188))) >= (mad(abs(mad(_99, -2.0f, _179)), 2.0f, abs(mad(_120, -2.0f, _156 + _170))) + abs(mad(_106, -2.0f, _163 + _177)));
        float _205 = _203 ? _120 : _127;
        float _206 = _203 ? _106 : _113;
        float _213 = asfloat(cb0_m22);
        float _214 = _203 ? cb0_m23.x : _213;
        float _221 = abs(_205 - _99);
        float _222 = abs(_206 - _99);
        bool _223 = _221 >= _222;
        float _226 = _223 ? (-_214) : _214;
        float _229 = clamp((1.0f / _141) * abs(mad(mad(_178 + _179, 2.0f, _184 + _188), 0.083333335816860198974609375f, -_99)), 0.0f, 1.0f);
        float _230 = _203 ? 0.0f : cb0_m23.x;
        float _233 = _203 ? TEXCOORD.x : mad(_226, 0.5f, TEXCOORD.x);
        float _234 = _203 ? mad(_226, 0.5f, TEXCOORD.y) : TEXCOORD.y;
        float _235 = _203 ? _213 : 0.0f;
        float _236 = _233 - _235;
        float _237 = _234 - _230;
        float _238 = _235 + _233;
        float _239 = _230 + _234;
        float _258 = _223 ? (_99 + _205) : (_99 + _206);
        float _259 = max(_221, _222) * 0.25f;
        float _261 = mad(_229, -2.0f, 3.0f) * (_229 * _229);
        bool _262 = mad(_258, -0.5f, _99) < 0.0f;
        float _263 = mad(_258, -0.5f, dp3_f32(float3(t0.SampleLevel(s4, float2(_236, _237), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f)));
        float _264 = mad(_258, -0.5f, dp3_f32(float3(t0.SampleLevel(s4, float2(_238, _239), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f)));
        bool _267 = abs(_263) >= _259;
        bool _268 = _259 <= abs(_264);
        float _271 = _267 ? _236 : mad(_235, -1.5f, _236);
        float _273 = _267 ? _237 : mad(_230, -1.5f, _237);
        float _278 = _268 ? _238 : mad(_235, 1.5f, _238);
        float _280 = _268 ? _239 : mad(_230, 1.5f, _239);
        float _421;
        float _422;
        float _423;
        float _424;
        float _425;
        float _426;
        if (!(_267 && _268))
        {
            float _293;
            if (!_267)
            {
                _293 = dp3_f32(float3(t0.SampleLevel(s4, float2(_271, _273), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
            }
            else
            {
                _293 = _263;
            }
            float _304;
            if (!_268)
            {
                _304 = dp3_f32(float3(t0.SampleLevel(s4, float2(_278, _280), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
            }
            else
            {
                _304 = _264;
            }
            float _306 = _267 ? _293 : mad(_258, -0.5f, _293);
            float _308 = _268 ? _304 : mad(_258, -0.5f, _304);
            bool _311 = _259 <= abs(_306);
            bool _312 = _259 <= abs(_308);
            float _315 = _311 ? _271 : mad(_235, -2.0f, _271);
            float _317 = _311 ? _273 : mad(_230, -2.0f, _273);
            float _322 = _312 ? _278 : mad(_235, 2.0f, _278);
            float _324 = _312 ? _280 : mad(_230, 2.0f, _280);
            float _415;
            float _416;
            float _417;
            float _418;
            float _419;
            float _420;
            if (!(_312 && _311))
            {
                float _337;
                if (!_311)
                {
                    _337 = dp3_f32(float3(t0.SampleLevel(s4, float2(_315, _317), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
                }
                else
                {
                    _337 = _306;
                }
                float _348;
                if (!_312)
                {
                    _348 = dp3_f32(float3(t0.SampleLevel(s4, float2(_322, _324), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
                }
                else
                {
                    _348 = _308;
                }
                float _350 = _311 ? _337 : mad(_258, -0.5f, _337);
                float _352 = _312 ? _348 : mad(_258, -0.5f, _348);
                bool _355 = _259 <= abs(_350);
                bool _356 = _259 <= abs(_352);
                float _359 = _355 ? _315 : mad(_235, -4.0f, _315);
                float _361 = _355 ? _317 : mad(_230, -4.0f, _317);
                float _366 = _356 ? _322 : mad(_235, 4.0f, _322);
                float _368 = _356 ? _324 : mad(_230, 4.0f, _324);
                float _409;
                float _410;
                float _411;
                float _412;
                float _413;
                float _414;
                if (!(_356 && _355))
                {
                    float _381;
                    if (!_355)
                    {
                        _381 = dp3_f32(float3(t0.SampleLevel(s4, float2(_359, _361), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
                    }
                    else
                    {
                        _381 = _350;
                    }
                    float _392;
                    if (!_356)
                    {
                        _392 = dp3_f32(float3(t0.SampleLevel(s4, float2(_366, _368), 0.0f).xyz), float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
                    }
                    else
                    {
                        _392 = _352;
                    }
                    float _394 = _355 ? _381 : mad(_258, -0.5f, _381);
                    float _396 = _356 ? _392 : mad(_258, -0.5f, _392);
                    bool _399 = _259 <= abs(_394);
                    bool _400 = _259 <= abs(_396);
                    _409 = _396;
                    _410 = _394;
                    _411 = _400 ? _368 : mad(_230, 12.0f, _368);
                    _412 = _399 ? _361 : mad(_230, -12.0f, _361);
                    _413 = _400 ? _366 : mad(_235, 12.0f, _366);
                    _414 = _399 ? _359 : mad(_235, -12.0f, _359);
                }
                else
                {
                    _409 = _352;
                    _410 = _350;
                    _411 = _368;
                    _412 = _361;
                    _413 = _366;
                    _414 = _359;
                }
                _415 = _409;
                _416 = _410;
                _417 = _411;
                _418 = _412;
                _419 = _413;
                _420 = _414;
            }
            else
            {
                _415 = _308;
                _416 = _306;
                _417 = _324;
                _418 = _317;
                _419 = _322;
                _420 = _315;
            }
            _421 = _415;
            _422 = _416;
            _423 = _417;
            _424 = _418;
            _425 = _419;
            _426 = _420;
        }
        else
        {
            _421 = _264;
            _422 = _263;
            _423 = _280;
            _424 = _273;
            _425 = _278;
            _426 = _271;
        }
        float _430 = _203 ? (TEXCOORD.x - _426) : (TEXCOORD.y - _424);
        float _432 = _203 ? (_425 - TEXCOORD.x) : (_423 - TEXCOORD.y);
        bool _433 = _422 < 0.0f;
        bool _434 = _421 < 0.0f;
        bool _436 = !_262;
        bool _446 = _430 < _432;
        float _459 = max(cb7_m.x * (_261 * _261), (((!_446) && ((_262 && (!_434)) || (_434 && _436))) || (((_262 && (!_433)) || (_433 && _436)) && _446)) ? mad(-(1.0f / (_430 + _432)), min(_430, _432), 0.5f) : 0.0f);
        float4 _466 = t0.SampleLevel(s4, float2(_203 ? TEXCOORD.x : mad(_226, _459, TEXCOORD.x), _203 ? mad(_226, _459, TEXCOORD.y) : TEXCOORD.y), 0.0f);
        SV_Target.x = _466.x;
        SV_Target.y = _466.y;
        SV_Target.z = _466.z;
        SV_Target.w = _99;
    }
    else
    {
        SV_Target.x = _94;
        SV_Target.y = _93.y;
        SV_Target.z = _93.z;
        SV_Target.w = _93.w;
    }
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.v1.xy;
    frag_main();

    SV_Target.xyz = sRGB_Encode(SV_Target.xyz);
    SV_Target.xyz = RenderIntermediatePass(SV_Target.xyz);
    
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
