cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m0 : packoffset(c0);
    uint4 cb0_m1 : packoffset(c1);
    float2 cb0_m2 : packoffset(c2);
    uint2 cb0_m3 : packoffset(c2.z);
    float2 cb0_m4 : packoffset(c3);
    uint2 cb0_m5 : packoffset(c3.z);
    float2 cb0_m6 : packoffset(c4);
    float2 cb0_m7 : packoffset(c4.z);
    uint4 cb0_m8 : packoffset(c5);
    float2 cb0_m9 : packoffset(c6);
    uint2 cb0_m10 : packoffset(c6.z);
};

cbuffer cb1_buf : register(b1)
{
    uint cb1_m0 : packoffset(c0);
    float3 cb1_m1 : packoffset(c0.y);
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
    float _66 = cb0_m2.x * cb0_m9.x;
    float _67 = cb0_m2.y * cb0_m9.y;
    float2 _94 = float2(clamp((TEXCOORD.x * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((TEXCOORD.y * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y));
    float4 _97 = t0.SampleLevel(s0, _94, 0.0f);

    #if ALLOW_AA == 0
        SV_Target = _97;
        return;
    #endif

    float _101 = _97.w;
    float4 _103 = t0.GatherAlpha(s0, _94);
    float _104 = _103.x;
    float _105 = _103.y;
    float _106 = _103.z;
    float4 _117 = t0.GatherAlpha(s0, float2(clamp(((TEXCOORD.x - cb0_m9.x) * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp(((TEXCOORD.y - cb0_m9.y) * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)));
    float _118 = _117.x;
    float _119 = _117.z;
    float _120 = _117.w;
    float _127 = max(max(_106, max(_101, _104)), max(_118, _119));
    float _130 = _127 - min(min(_106, min(_101, _104)), min(_118, _119));
    float _473;
    float _474;
    float _475;
    if (_130 >= max(_127 * 0.16599999368190765380859375f, 0.083300001919269561767578125f))
    {
        float4 _154 = t0.SampleLevel(s0, float2(clamp((mad(cb0_m9.x, 1.0f, TEXCOORD.x) * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((mad(cb0_m9.y, -1.0f, TEXCOORD.y) * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f);
        float _155 = _154.w;
        float4 _158 = t0.SampleLevel(s0, float2(clamp((mad(cb0_m9.x, -1.0f, TEXCOORD.x) * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((mad(cb0_m9.y, 1.0f, TEXCOORD.y) * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f);
        float _159 = _158.w;
        float _160 = _104 + _119;
        float _161 = _106 + _118;
        float _166 = _105 + _155;
        float _170 = _120 + _159;
        bool _185 = (mad(abs(mad(_101, -2.0f, _160)), 2.0f, abs(mad(_106, -2.0f, _166))) + abs(mad(_118, -2.0f, _170))) >= (mad(abs(mad(_101, -2.0f, _161)), 2.0f, abs(mad(_119, -2.0f, _120 + _155))) + abs(mad(_104, -2.0f, _105 + _159)));
        float _187 = _185 ? _119 : _118;
        float _188 = _185 ? _104 : _106;
        float _196 = asfloat(cb1_m0);
        float _197 = _185 ? cb1_m1.x : _196;
        float _204 = abs(_187 - _101);
        float _205 = abs(_188 - _101);
        bool _206 = _204 >= _205;
        float _209 = _206 ? (-_197) : _197;
        float _212 = clamp((1.0f / _130) * abs(mad(mad(_160 + _161, 2.0f, _166 + _170), 0.083333335816860198974609375f, -_101)), 0.0f, 1.0f);
        float _213 = _185 ? 0.0f : cb1_m1.x;
        float _216 = _185 ? TEXCOORD.x : mad(_209, 0.5f, TEXCOORD.x);
        float _217 = _185 ? mad(_209, 0.5f, TEXCOORD.y) : TEXCOORD.y;
        float _218 = _185 ? _196 : 0.0f;
        float _219 = _216 - _218;
        float _220 = _217 - _213;
        float _221 = _218 + _216;
        float _222 = _213 + _217;
        float _245 = _206 ? (_101 + _187) : (_101 + _188);
        float _246 = max(_204, _205) * 0.25f;
        float _248 = mad(_212, -2.0f, 3.0f) * (_212 * _212);
        bool _249 = mad(_245, -0.5f, _101) < 0.0f;
        float _250 = mad(_245, -0.5f, t0.SampleLevel(s0, float2(clamp((_219 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_220 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w);
        float _251 = mad(_245, -0.5f, t0.SampleLevel(s0, float2(clamp((_221 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_222 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w);
        bool _254 = abs(_250) >= _246;
        bool _255 = _246 <= abs(_251);
        float _258 = _254 ? _219 : mad(_218, -1.5f, _219);
        float _260 = _254 ? _220 : mad(_213, -1.5f, _220);
        float _266 = _255 ? _221 : mad(_218, 1.5f, _221);
        float _267 = _255 ? _222 : mad(_213, 1.5f, _222);
        float _420;
        float _421;
        float _422;
        float _423;
        float _424;
        float _425;
        if (!(_254 && _255))
        {
            float _282;
            if (!_254)
            {
                _282 = t0.SampleLevel(s0, float2(clamp((_258 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_260 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w;
            }
            else
            {
                _282 = _250;
            }
            float _295;
            if (!_255)
            {
                _295 = t0.SampleLevel(s0, float2(clamp((_266 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_267 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w;
            }
            else
            {
                _295 = _251;
            }
            float _297 = _254 ? _282 : mad(_245, -0.5f, _282);
            float _299 = _255 ? _295 : mad(_245, -0.5f, _295);
            bool _302 = _246 <= abs(_297);
            bool _303 = _246 <= abs(_299);
            float _306 = _302 ? _258 : mad(_218, -2.0f, _258);
            float _308 = _302 ? _260 : mad(_213, -2.0f, _260);
            float _313 = _303 ? _266 : mad(_218, 2.0f, _266);
            float _315 = _303 ? _267 : mad(_213, 2.0f, _267);
            float _414;
            float _415;
            float _416;
            float _417;
            float _418;
            float _419;
            if (!(_302 && _303))
            {
                float _330;
                if (!_302)
                {
                    _330 = t0.SampleLevel(s0, float2(clamp((_306 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_308 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w;
                }
                else
                {
                    _330 = _297;
                }
                float _343;
                if (!_303)
                {
                    _343 = t0.SampleLevel(s0, float2(clamp((_313 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_315 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w;
                }
                else
                {
                    _343 = _299;
                }
                float _345 = _302 ? _330 : mad(_245, -0.5f, _330);
                float _347 = _303 ? _343 : mad(_245, -0.5f, _343);
                bool _350 = _246 <= abs(_345);
                bool _351 = abs(_347) >= _246;
                float _354 = _350 ? _306 : mad(_218, -4.0f, _306);
                float _356 = _350 ? _308 : mad(_213, -4.0f, _308);
                float _361 = _351 ? _313 : mad(_218, 4.0f, _313);
                float _363 = _351 ? _315 : mad(_213, 4.0f, _315);
                float _408;
                float _409;
                float _410;
                float _411;
                float _412;
                float _413;
                if (!(_351 && _350))
                {
                    float _378;
                    if (!_350)
                    {
                        _378 = t0.SampleLevel(s0, float2(clamp((_354 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_356 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w;
                    }
                    else
                    {
                        _378 = _345;
                    }
                    float _391;
                    if (!_351)
                    {
                        _391 = t0.SampleLevel(s0, float2(clamp((_361 * cb0_m4.x) + _66, cb0_m6.x, cb0_m7.x), clamp((_363 * cb0_m4.y) + _67, cb0_m6.y, cb0_m7.y)), 0.0f).w;
                    }
                    else
                    {
                        _391 = _347;
                    }
                    float _393 = _350 ? _378 : mad(_245, -0.5f, _378);
                    float _395 = _351 ? _391 : mad(_245, -0.5f, _391);
                    bool _398 = _246 <= abs(_393);
                    bool _399 = _246 <= abs(_395);
                    _408 = _395;
                    _409 = _393;
                    _410 = _399 ? _363 : mad(_213, 12.0f, _363);
                    _411 = _399 ? _361 : mad(_218, 12.0f, _361);
                    _412 = _398 ? _356 : mad(_213, -12.0f, _356);
                    _413 = _398 ? _354 : mad(_218, -12.0f, _354);
                }
                else
                {
                    _408 = _347;
                    _409 = _345;
                    _410 = _363;
                    _411 = _361;
                    _412 = _356;
                    _413 = _354;
                }
                _414 = _408;
                _415 = _409;
                _416 = _410;
                _417 = _411;
                _418 = _412;
                _419 = _413;
            }
            else
            {
                _414 = _299;
                _415 = _297;
                _416 = _315;
                _417 = _313;
                _418 = _308;
                _419 = _306;
            }
            _420 = _414;
            _421 = _415;
            _422 = _416;
            _423 = _417;
            _424 = _418;
            _425 = _419;
        }
        else
        {
            _420 = _251;
            _421 = _250;
            _422 = _267;
            _423 = _266;
            _424 = _260;
            _425 = _258;
        }
        float _428 = _185 ? (TEXCOORD.x - _425) : (TEXCOORD.y - _424);
        float _431 = _185 ? (_423 - TEXCOORD.x) : (_422 - TEXCOORD.y);
        bool _432 = _421 < 0.0f;
        bool _433 = _420 < 0.0f;
        bool _435 = !_249;
        bool _445 = _428 < _431;
        float _456 = max((_248 * _248) * 0.25f, (((!_445) && ((_249 && (!_433)) || (_435 && _433))) || (((_435 && _432) || (_249 && (!_432))) && _445)) ? mad(-(1.0f / (_428 + _431)), min(_428, _431), 0.5f) : 0.0f);
        float4 _469 = t0.SampleLevel(s0, float2(clamp(cb0_m6.x, ((_185 ? TEXCOORD.x : mad(_209, _456, TEXCOORD.x)) * cb0_m4.x) + _66, cb0_m7.x), clamp((cb0_m4.y * (_185 ? mad(_209, _456, TEXCOORD.y) : TEXCOORD.y)) + _67, cb0_m6.y, cb0_m7.y)), 0.0f);
        _473 = _469.z;
        _474 = _469.y;
        _475 = _469.x;
    }
    else
    {
        _473 = _97.z;
        _474 = _97.y;
        _475 = _97.x;
    }
    SV_Target.x = _475;
    SV_Target.y = _474;
    SV_Target.z = _473;
    SV_Target.w = _101;
}

#if XBOX360_CURVE == 1
#include "./Includes/Curve360.hlsl"
#endif

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.v1.xy;
    frag_main();

    SV_Target.xyz = max(SV_Target.xyz, 0);
    SV_Target.xyz = sRGB_Encode(SV_Target.xyz);
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
    SV_Target.xyz = sRGB_Decode(SV_Target.xyz);

    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}



/*

cbuffer GlobalPS : register(b0)
{
  float ps_global_mip_bias : packoffset(c0);
  float3 ps_global_mip_bias_pad : packoffset(c0.y);
  float2 ps_global_viewport_res : packoffset(c1);
  float2 ps_global_viewport_res_pad : packoffset(c1.z);
  float2 ps_global_viewport_top_left_pixel : packoffset(c2);
  float2 ps_global_viewport_top_left_pixel_pad : packoffset(c2.z);
  float2 ps_global_viewport_res_multipliers : packoffset(c3);
  float2 ps_global_viewport_res_multipliers_pad : packoffset(c3.z);
  float4 ps_global_viewport_bounds_uv : packoffset(c4);
  float2 ps_global_render_resolution : packoffset(c5);
  float2 ps_global_render_resolution_pad : packoffset(c5.z);
  float2 ps_global_render_pixel_size : packoffset(c6);
  float2 ps_global_render_pixel_size_pad : packoffset(c6.z);
  uint ps_global_is_texture_in_viewport_flags : packoffset(c7);
}

cbuffer PostProcessPS : register(b1)
{
  float4 ps_postprocess_pixel_size : packoffset(c0);
  float4 ps_postprocess_scale : packoffset(c1);
  float4x3 ps_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 ps_postprocess_contrast : packoffset(c5);
}

SamplerState LocalSampler_source_sampler_s : register(s0);
Texture2D<float4> LocalTexture_source_sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{

*/