cbuffer cb0_buf : register(b0)
{
    float4 cb0_m : packoffset(c0);
};

cbuffer cb13_buf : register(b13)
{
    float4 cb13_m0 : packoffset(c0);
    float2 cb13_m1 : packoffset(c1);
    float2 cb13_m2 : packoffset(c1.z);
    float2 cb13_m3 : packoffset(c2);
    float2 cb13_m4 : packoffset(c2.z);
    float2 cb13_m5 : packoffset(c3);
    float2 cb13_m6 : packoffset(c3.z);
    float4 cb13_m7 : packoffset(c4);
    float4 cb13_m8 : packoffset(c5);
    float4 cb13_m9 : packoffset(c6);
    float4 cb13_m10 : packoffset(c7);
    float4 cb13_m11 : packoffset(c8);
    float4 cb13_m12 : packoffset(c9);
    float3 cb13_m13 : packoffset(c10);
    uint cb13_m14 : packoffset(c10.w);
    float4 cb13_m15 : packoffset(c11);
    float3 cb13_m16 : packoffset(c12);
    uint cb13_m17 : packoffset(c12.w);
    float4 cb13_m18 : packoffset(c13);
    float4 cb13_m19 : packoffset(c14);
    float4 cb13_m20 : packoffset(c15);
    float3 cb13_m21 : packoffset(c16);
    uint cb13_m22 : packoffset(c16.w);
    uint4 cb13_m23 : packoffset(c17);
};

cbuffer cb1_buf : register(b1)
{
    float3 cb1_m0 : packoffset(c0);
    uint cb1_m1 : packoffset(c0.w);
};

cbuffer cb2_buf : register(b2)
{
    float4 cb2_m : packoffset(c0);
};

cbuffer cb3_buf : register(b3)
{
    uint4 cb3_m0 : packoffset(c0);
    uint4 cb3_m1 : packoffset(c1);
    uint4 cb3_m2 : packoffset(c2);
    uint4 cb3_m3 : packoffset(c3);
};

cbuffer cb4_buf : register(b4)
{
    float4 cb4_m0 : packoffset(c0);
    float4 cb4_m1 : packoffset(c1);
};

cbuffer cb5_buf : register(b5)
{
    float4 cb5_m[4096] : packoffset(c0);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s13 : register(s13);
SamplerState s14 : register(s14);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
TextureCube<float4> t3 : register(t3);
Texture2DArray<float4> t13 : register(t13);
Texture2DArray<float4> t14 : register(t14);
Texture2D<float4> t16 : register(t16);
Texture2D<float4> t17 : register(t17);

static float4 gl_FragCoord;
static float2 TEXCOORD;
static float3 TEXCOORD3;
static float3 TEXCOORD4;
static float3 TEXCOORD5;
static float4 TEXCOORD6;
static float3 TEXCOORD7;
static float3 COLOR;
static float3 COLOR1;
static float4 SV_Target;
static float4 SV_Target1;

struct SPIRV_Cross_Input
{
    float4 gl_FragCoord : SV_Position0;
    float v1 : SV_ClipDistance0;
    float2 TEXCOORD : TEXCOORD0;
    float3 TEXCOORD3 : TEXCOORD3;
    float3 TEXCOORD4 : TEXCOORD4;
    float3 TEXCOORD5 : TEXCOORD5;
    centroid float4 TEXCOORD6 : TEXCOORD6;
    float3 TEXCOORD7 : TEXCOORD7;
    float3 COLOR : COLOR0;
    float3 COLOR1 : COLOR1;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
    float4 SV_Target1 : SV_Target1;
};

float dp3_f32(float3 a, float3 b)
{
    precise float _180 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _180));
}

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

float dp4_f32(float4 a, float4 b)
{
    precise float _150 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _150)));
}

void frag_main()
{
    float2 _197 = float2(TEXCOORD6.x, TEXCOORD6.y);
    float4 _201 = t13.Sample(s13, float3(_197, 0.0f));
    float _208 = 0.0f + 1.0f;
    float2 _209 = float2(TEXCOORD6.x + 0.0f, TEXCOORD6.y + 0.0f);
    float4 _212 = t13.Sample(s13, float3(_209, _208));
    float4 _220 = t13.Sample(s13, float3(_209, 0.0f + 2.0f));
    float4 _228 = t13.Sample(s13, float3(_209, 0.0f + 3.0f));
    float4 _236 = t13.Sample(s13, float3(_209, 0.0f + 4.0f));
    float4 _244 = t13.Sample(s13, float3(_209, 0.0f + 5.0f));
    float4 _252 = t13.Sample(s13, float3(_209, 0.0f + 6.0f));
    float4 _260 = t13.Sample(s13, float3(_209, 0.0f + 7.0f));
    float4 _269 = t14.Sample(s14, float3(_197, 0.0f));
    float4 _276 = t14.Sample(s14, float3(_209, _208));
    float _286 = (_201.w * _212.w) * cb4_m0.x;
    float _293 = _286 * mad(_201.x + _212.x, 2.0f, -2.0f);
    float _294 = _286 * mad(_201.y + _212.y, 2.0f, -2.0f);
    float _299 = (_220.w * _228.w) * cb4_m0.y;
    float _306 = _299 * mad(_220.x + _228.x, 2.0f, -2.0f);
    float _307 = _299 * mad(_220.z + _228.z, 2.0f, -2.0f);
    float _308 = _299 * mad(_220.y + _228.y, 2.0f, -2.0f);
    float _312 = (_236.w * _244.w) * cb4_m0.z;
    float _319 = _312 * mad(_236.x + _244.x, 2.0f, -2.0f);
    float _320 = _312 * mad(_236.y + _244.y, 2.0f, -2.0f);
    float _321 = _312 * mad(_236.z + _244.z, 2.0f, -2.0f);
    float _325 = (_252.w * _260.w) * cb4_m1.x;
    float _332 = _325 * mad(_252.x + _260.x, 2.0f, -2.0f);
    float _333 = _325 * mad(_252.y + _260.y, 2.0f, -2.0f);
    float _334 = _325 * mad(_252.z + _260.z, 2.0f, -2.0f);
    float _338 = (_269.w * _276.w) * cb4_m1.y;
    float _345 = _338 * mad(_269.x + _276.x, 2.0f, -2.0f);
    float _346 = _338 * mad(_269.y + _276.y, 2.0f, -2.0f);
    float _347 = _338 * mad(_269.z + _276.z, 2.0f, -2.0f);
    float _357 = mad(_334, -0.072185598313808441162109375f, (_332 * (-0.21265600621700286865234375f)) + (_333 * (-0.715157985687255859375f)));
    float _358 = mad(_307, -0.072185598313808441162109375f, (_308 * (-0.715157985687255859375f)) + (_306 * (-0.21265600621700286865234375f)));
    float _359 = mad(_321, 0.072185598313808441162109375f, (_320 * 0.715157985687255859375f) + (_319 * 0.21265600621700286865234375f));
    float3 _360 = float3(_357, _358, _359);
    float _361 = dp3_f32(_360, _360);
    bool _362 = _361 > 0.0f;
    float _363 = rsqrt(_361);
    float3 _373 = float3(TEXCOORD7.x, TEXCOORD7.y, TEXCOORD7.z);
    float _375 = rsqrt(dp3_f32(_373, _373));
    float _376 = TEXCOORD7.x * _375;
    float _377 = TEXCOORD7.y * _375;
    float _378 = TEXCOORD7.z * _375;
    float _535;
    float _536;
    float _537;
    float _538;
    float _539;
    float _540;
    float _541;
    if (cb3_m3.x != 0u)
    {
        float4 _411 = t2.SampleBias(s2, float2(mad(TEXCOORD.x, cb13_m5.x, cb13_m6.x), mad(TEXCOORD.y, cb13_m5.y, cb13_m6.y)), cb0_m.x);
        float _412 = _411.x;
        float _413 = _411.y;
        float _419 = sqrt(1.0f - min((_412 * _412) + (_413 * _413), 1.0f));
        float _447 = mad(_419, TEXCOORD3.x, (_413 * TEXCOORD4.x) + (_412 * TEXCOORD5.x));
        float _448 = mad(_419, TEXCOORD3.y, (_412 * TEXCOORD5.y) + (_413 * TEXCOORD4.y));
        float _449 = mad(_419, TEXCOORD3.z, (_412 * TEXCOORD5.z) + (_413 * TEXCOORD4.z));
        float3 _450 = float3(_447, _448, _449);
        float _452 = rsqrt(dp3_f32(_450, _450));
        float4 _470 = t0.SampleBias(s0, float2(mad(TEXCOORD.x, cb13_m1.x, cb13_m2.x), mad(TEXCOORD.y, cb13_m1.y, cb13_m2.y)), cb0_m.x);
        float4 _489 = t1.SampleBias(s1, float2(mad(TEXCOORD.x, cb13_m3.x, cb13_m4.x), mad(TEXCOORD.y, cb13_m3.y, cb13_m4.y)), cb0_m.x);
        _535 = (_470.w * _489.w) * cb13_m0.w;
        _536 = ((_470.z * _489.z) * cb13_m0.z) * 4.594789981842041015625f;
        _537 = ((_470.y * _489.y) * cb13_m0.y) * 4.594789981842041015625f;
        _538 = ((_470.x * _489.x) * cb13_m0.x) * 4.594789981842041015625f;
        _539 = _449 * _452;
        _540 = _448 * _452;
        _541 = _447 * _452;
    }
    else
    {
        uint2 _521 = uint2(uint(cvt_f32_i32(gl_FragCoord.x)), uint(cvt_f32_i32(gl_FragCoord.y)));
        float4 _522 = t17.Load(int3(_521, 0u));
        float4 _530 = t16.Load(int3(_521, 0u));
        _535 = _530.w;
        _536 = _530.z;
        _537 = _530.y;
        _538 = _530.x;
        _539 = mad(_522.z, 2.0f, -1.0f);
        _540 = mad(_522.y, 2.0f, -1.0f);
        _541 = mad(_522.x, 2.0f, -1.0f);
    }
    float3 _542 = float3(_541, _540, _539);
    float _544 = sqrt(dp3_f32(_542, _542));
    float _545 = _541 / _544;
    float _546 = _540 / _544;
    float _547 = _539 / _544;
    float3 _549 = float3(_545, _546, _547);
    float _550 = dp3_f32(float3(_376, _377, _378), _549);
    float _560 = _376 + (((_545 * _550) - _376) * 2.0f);
    float _561 = _377 + (((_550 * _546) - _377) * 2.0f);
    float _562 = _378 + (((_550 * _547) - _378) * 2.0f);
    float _563 = _362 ? (_357 * _363) : 0.0f;
    float _564 = _362 ? (_363 * _358) : 0.0f;
    float _565 = _362 ? (_363 * _359) : 0.0f;
    float _566 = _563 * (-0.4886024892330169677734375f);
    float _567 = _564 * (-0.4886024892330169677734375f);
    float _568 = _565 * (-0.4886024892330169677734375f);
    float _581 = _333 - (_346 * _566);
    float _582 = _308 - (_346 * _567);
    float _584 = -((_346 * _568) + _320);
    float _597 = _294 + (_346 * (-0.2820948064327239990234375f));
    float3 _614 = float3(_563, _564, _565);
    float _616 = max(dp3_f32(_614, _549), 0.0f);
    float _636 = exp2(log2(1.0f - max(_550, 0.0f)) * cb13_m18.x);
    float _642 = mad(_636, cb13_m15.x - cb13_m12.x, cb13_m12.x);
    float _657 = mad(_636, cb13_m16.x - cb13_m13.x, cb13_m13.x);
    float _658 = mad(_636, cb13_m16.y - cb13_m13.y, cb13_m13.y);
    float _659 = mad(_636, cb13_m16.z - cb13_m13.z, cb13_m13.z);
    float _665 = mad(cb13_m19.x, _538 - _657, _657);
    float _666 = mad(cb13_m19.x, _537 - _658, _658);
    float _667 = mad(cb13_m19.x, _536 - _659, _659);
    float3 _668 = float3(_560, _561, _562);
    float _669 = dp3_f32(_614, _668);
    bool _670 = _669 > 0.0f;
    float _676 = (exp2(_642 * log2(_669)) * (_642 + 1.0f)) * 0.15915457904338836669921875f;
    bool _685 = cb13_m20.x > 0.0f;
    float4 _686 = float4(_581, _582, _584, _597);
    float3 _688 = float3(_333, _308, -_320);
    float _697 = exp2((cb13_m20.x * 100.0f) * log2(1.0f - min(dp4_f32(_686, _686) / mad(_294, _294, dp3_f32(_688, _688)), 1.0f)));
    float _698 = _670 ? (_345 * (_665 * _676)) : 0.0f;
    float _699 = _670 ? (_346 * (_676 * _666)) : 0.0f;
    float _700 = _670 ? (_347 * (_676 * _667)) : 0.0f;
    float _849;
    float _850;
    float _851;
    float _852;
    float _853;
    float _854;
    if (cb13_m23.y == 0u)
    {
        bool _716 = _642 == 0.0f;
        uint _718;
        uint _721;
        uint _723;
        _718 = 0u;
        _721 = 0u;
        _723 = 0u;
        uint _719;
        uint _722;
        uint _724;
        uint _726;
        uint _728;
        uint _730;
        uint _732;
        uint _725 = 0u;
        uint _727 = 0u;
        uint _729 = 0u;
        uint _731 = 0u;
        float _733;
        float _734;
        float _735;
        for (;;)
        {
            _733 = asfloat(_729);
            _734 = asfloat(_727);
            _735 = asfloat(_725);
            if (float(int(_731)) >= cb5_m[0u].x)
            {
                break;
            }
            uint _746 = _731 * 5u;
            uint _750 = _746 + 1u;
            float _756 = (TEXCOORD7.x - cb1_m0.x) + cb5_m[_750].x;
            float _757 = (TEXCOORD7.y - cb1_m0.y) + cb5_m[_750].y;
            float _758 = (TEXCOORD7.z - cb1_m0.z) + cb5_m[_750].z;
            float3 _759 = float3(_756, _757, _758);
            float _760 = dp3_f32(_759, _759);
            uint _761 = _746 + 5u;
            if (cb5_m[_761].x <= _760)
            {
                _732 = _731 + 1u;
                _730 = _729;
                _728 = _727;
                _726 = _725;
                _724 = _723;
                _722 = _721;
                _719 = _718;
                _718 = _719;
                _721 = _722;
                _723 = _724;
                _725 = _726;
                _727 = _728;
                _729 = _730;
                _731 = _732;
                continue;
            }
            float _769 = rsqrt(_760);
            float3 _778 = float3(_756 * _769, _769 * _757, _769 * _758);
            uint _779 = _746 + 2u;
            uint _787 = _746 + 4u;
            uint _798 = _746 + 3u;
            float _810 = clamp(cb5_m[_779].w + exp2(cb5_m[_798].w * log2(max(mad(dp3_f32(_778, float3(cb5_m[_779].xyz)), cb5_m[_787].y, cb5_m[_787].w), 0.0f))), 0.0f, 1.0f) * clamp(mad(1.0f / (_760 + cb5_m[_750].w), cb5_m[_787].x, cb5_m[_787].z), 0.0f, 1.0f);
            float _816 = _810 * cb5_m[_798].x;
            float _817 = _810 * cb5_m[_798].y;
            float _818 = _810 * cb5_m[_798].z;
            float _820 = max(dp3_f32(_549, _778), 0.0500000007450580596923828125f);
            float _832 = _716 ? 1.0f : exp2(_642 * log2(max(dp3_f32(_778, _668), 0.0f)));
            _732 = _731 + 1u;
            _730 = asuint(mad(_816, _820, _733));
            _728 = asuint(mad(_817, _820, _734));
            _726 = asuint(mad(_818, _820, _735));
            _724 = asuint(mad(_816, _832, asfloat(_723)));
            _722 = asuint(mad(_817, _832, asfloat(_721)));
            _719 = asuint(mad(_818, _832, asfloat(_718)));
            _718 = _719;
            _721 = _722;
            _723 = _724;
            _725 = _726;
            _727 = _728;
            _729 = _730;
            _731 = _732;
            continue;
        }
        _849 = _735;
        _850 = _734;
        _851 = _733;
        _852 = _642 * asfloat(_718);
        _853 = _642 * asfloat(_721);
        _854 = _642 * asfloat(_723);
    }
    else
    {
        _849 = 0.0f;
        _850 = 0.0f;
        _851 = 0.0f;
        _852 = 0.0f;
        _853 = 0.0f;
        _854 = 0.0f;
    }
    float _863 = _293 * 0.423142492771148681640625f;
    float _867 = _665 * (_863 + (dp3_f32(_668, float3(_332, _306, -_319)) * (-0.3805235922336578369140625f)));
    float _868 = (_863 + (dp3_f32(_668, _688) * (-0.3805235922336578369140625f))) * _666;
    float _869 = (_863 + (dp3_f32(_668, float3(_334, _307, -_321)) * (-0.3805235922336578369140625f))) * _667;
    float _872 = cb13_m8.x * _535;
    float _901 = cb13_m8.x * (cb13_m11.x * _535);
    float4 _919 = t3.Sample(s3, float3(_560, -_561, _562));
    float _923 = _919.w;
    float _968 = max(cb2_m.x * mad(mad(_923, cb13_m21.x * (max(_867, 0.001000000047497451305389404296875f) * (_901 * _919.x)), (_872 * mad(max(_685 ? (_697 * _698) : _698, 0.0f) + _854, cb13_m10.x, max(cb13_m9.x * _867, 0.0f))) + ((((((((_293 + (_345 * (-0.2820948064327239990234375f))) * 0.88622701168060302734375f) + (dp3_f32(_549, float3(_332 - (_345 * _566), _306 - (_345 * _567), -((_345 * _568) + _319))) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f) + ((_345 * _616) * 0.2809999883174896240234375f)) + _851) * cb13_m7.x) * _538)), COLOR.x, COLOR1.x), 0.0f);
    float _969 = max(mad(mad(cb13_m21.y * ((_901 * _919.y) * max(_868, 0.001000000047497451305389404296875f)), _923, (((((((_597 * 0.88622701168060302734375f) + (dp3_f32(_549, float3(_581, _582, _584)) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f) + ((_616 * _346) * 0.2809999883174896240234375f)) + _850) * cb13_m7.x) * _537) + (mad(max(_685 ? (_697 * _699) : _699, 0.0f) + _853, cb13_m10.x, max(cb13_m9.x * _868, 0.0f)) * _872)), COLOR.y, COLOR1.y) * cb2_m.x, 0.0f);
    float _970 = max(mad(mad(cb13_m21.z * ((_901 * _919.z) * max(_869, 0.001000000047497451305389404296875f)), _923, (((((((((_286 * mad(_201.z + _212.z, 2.0f, -2.0f)) + (_347 * (-0.2820948064327239990234375f))) * 0.88622701168060302734375f) + (dp3_f32(_549, float3(_334 - (_347 * _566), _307 - (_347 * _567), -((_347 * _568) + _321))) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f) + ((_616 * _347) * 0.2809999883174896240234375f)) + _849) * cb13_m7.x) * _536) + (mad(max(_685 ? (_697 * _700) : _700, 0.0f) + _852, cb13_m10.x, max(cb13_m9.x * _869, 0.0f)) * _872)), COLOR.z, COLOR1.z) * cb2_m.x, 0.0f);
    SV_Target.w = cb2_m.w * _535;
    SV_Target1.x = _968 / cb2_m.y;
    SV_Target1.y = _969 / cb2_m.y;
    SV_Target1.z = _970 / cb2_m.y;
    SV_Target1.w = cb2_m.z * _535;
    SV_Target.x = _968;
    SV_Target.y = _969;
    SV_Target.z = _970;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    TEXCOORD = stage_input.TEXCOORD;
    TEXCOORD3 = stage_input.TEXCOORD3;
    TEXCOORD4 = stage_input.TEXCOORD4;
    TEXCOORD5 = stage_input.TEXCOORD5;
    TEXCOORD6 = stage_input.TEXCOORD6;
    TEXCOORD7 = stage_input.TEXCOORD7;
    COLOR = stage_input.COLOR;
    COLOR1 = stage_input.COLOR1;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
        stage_output.SV_Target.xyz = max(0, stage_output.SV_Target.xyz);
        stage_output.SV_Target.w = saturate(stage_output.SV_Target.w);
    stage_output.SV_Target1 = SV_Target1;
        stage_output.SV_Target1.xyz = max(0, stage_output.SV_Target1.xyz);
        stage_output.SV_Target1.w = saturate(stage_output.SV_Target1.w);
    return stage_output;
}
