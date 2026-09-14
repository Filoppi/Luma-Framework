cbuffer cb0_buf : register(b0)
{
    float4 cb0_m : packoffset(c0);
};

cbuffer cb13_buf : register(b13)
{
    float4 cb13_m[30] : packoffset(c0);
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
    float3 cb3_m1 : packoffset(c1);
    uint cb3_m2 : packoffset(c1.w);
    uint4 cb3_m3 : packoffset(c2);
    uint4 cb3_m4 : packoffset(c3);
    uint4 cb3_m5 : packoffset(c4);
    uint4 cb3_m6 : packoffset(c5);
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
SamplerState s13 : register(s13);
SamplerState s14 : register(s14);
TextureCube<float4> t0 : register(t0);
TextureCube<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2DArray<float4> t13 : register(t13);
Texture2DArray<float4> t14 : register(t14);
Texture2D<float4> t16 : register(t16);
Texture2D<float4> t17 : register(t17);

static float4 gl_FragCoord;
static float2 TEXCOORD;
static float4 TEXCOORD4;
static float3 TEXCOORD5;
static float3 COLOR;
static float3 COLOR1;
static float4 SV_Target;
static float4 SV_Target1;

struct SPIRV_Cross_Input
{
    float4 gl_FragCoord : SV_Position0;
    float v1 : SV_ClipDistance0;
    float4 TEXCOORD : TEXCOORD0;
    float4 TEXCOORD1 : TEXCOORD1;
    float4 TEXCOORD2 : TEXCOORD2;
    float4 TEXCOORD3 : TEXCOORD3;
    centroid float4 TEXCOORD4 : TEXCOORD4;
    float3 TEXCOORD5 : TEXCOORD5;
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
    precise float _190 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _190));
}

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

#include "./Includes/Common.hlsl"

void frag_main()
{
    float2 _208 = float2(TEXCOORD4.x, TEXCOORD4.y);
    float4 _212 = t13.Sample(s13, float3(_208, 0.0f));
    float _219 = 0.0f + 1.0f;
    float2 _220 = float2(TEXCOORD4.x + 0.0f, TEXCOORD4.y + 0.0f);
    float4 _223 = t13.Sample(s13, float3(_220, _219));
    float4 _231 = t13.Sample(s13, float3(_220, 0.0f + 2.0f));
    float4 _239 = t13.Sample(s13, float3(_220, 0.0f + 3.0f));
    float4 _247 = t13.Sample(s13, float3(_220, 0.0f + 4.0f));
    float4 _255 = t13.Sample(s13, float3(_220, 0.0f + 5.0f));
    float4 _263 = t13.Sample(s13, float3(_220, 0.0f + 6.0f));
    float4 _271 = t13.Sample(s13, float3(_220, 0.0f + 7.0f));
    float4 _280 = t14.Sample(s14, float3(_208, 0.0f));
    float4 _287 = t14.Sample(s14, float3(_220, _219));
    float _297 = (_212.w * _223.w) * cb4_m0.x;
    float _304 = _297 * mad(_212.x + _223.x, 2.0f, -2.0f);
    float _305 = _297 * mad(_212.y + _223.y, 2.0f, -2.0f);
    float _306 = _297 * mad(_212.z + _223.z, 2.0f, -2.0f);
    float _310 = (_231.w * _239.w) * cb4_m0.y;
    float _317 = _310 * mad(_231.x + _239.x, 2.0f, -2.0f);
    float _318 = _310 * mad(_231.z + _239.z, 2.0f, -2.0f);
    float _319 = _310 * mad(_231.y + _239.y, 2.0f, -2.0f);
    float _323 = (_247.w * _255.w) * cb4_m0.z;
    float _330 = _323 * mad(_247.x + _255.x, 2.0f, -2.0f);
    float _331 = _323 * mad(_247.y + _255.y, 2.0f, -2.0f);
    float _332 = _323 * mad(_247.z + _255.z, 2.0f, -2.0f);
    float _336 = (_263.w * _271.w) * cb4_m1.x;
    float _343 = _336 * mad(_263.x + _271.x, 2.0f, -2.0f);
    float _344 = _336 * mad(_263.y + _271.y, 2.0f, -2.0f);
    float _345 = _336 * mad(_263.z + _271.z, 2.0f, -2.0f);
    float _349 = (_280.w * _287.w) * cb4_m1.y;
    float _356 = _349 * mad(_280.x + _287.x, 2.0f, -2.0f);
    float _357 = _349 * mad(_280.y + _287.y, 2.0f, -2.0f);
    float _358 = _349 * mad(_280.z + _287.z, 2.0f, -2.0f);
    float _368 = mad(_345, -0.072185598313808441162109375f, (_343 * (-0.21265600621700286865234375f)) + (_344 * (-0.715157985687255859375f)));
    float _369 = mad(_318, -0.072185598313808441162109375f, (_319 * (-0.715157985687255859375f)) + (_317 * (-0.21265600621700286865234375f)));
    float _370 = mad(_332, 0.072185598313808441162109375f, (_330 * 0.21265600621700286865234375f) + (_331 * 0.715157985687255859375f));
    float3 _371 = float3(_368, _369, _370);
    float _372 = dp3_f32(_371, _371);
    bool _373 = _372 > 0.0f;
    float _374 = rsqrt(_372);
    float4 _400 = t2.SampleBias(s2, float2(mad(TEXCOORD.x, cb13_m[2u].x, cb13_m[2u].z), mad(TEXCOORD.y, cb13_m[2u].y, cb13_m[2u].w)), cb0_m.x);
    float _401 = _400.x;
    float _402 = _400.y;
    float _403 = _400.z;
    float _405 = _403 + (_401 + _402);
    float _406 = _401 / _405;
    float _407 = _402 / _405;
    float _408 = _403 / _405;
    float _418 = mad(_408, cb13_m[21u].x, (_407 * cb13_m[12u].x) + (_406 * cb13_m[3u].x));
    float _429 = 1.0f - cb13_m[11u].x;
    float _435 = _406 * cb13_m[4u].x;
    float _450 = 1.0f - cb13_m[20u].x;
    float _468 = _407 * cb13_m[13u].x;
    float _504 = 1.0f - cb13_m[29u].x;
    float _505 = mad(_408 * cb13_m[24u].x, _504, ((_407 * cb13_m[15u].x) * _450) + ((_406 * cb13_m[6u].x) * _429));
    float _506 = mad(_408 * cb13_m[24u].y, _504, ((_406 * cb13_m[6u].y) * _429) + ((_407 * cb13_m[15u].y) * _450));
    float _507 = mad(_408 * cb13_m[24u].z, _504, ((_406 * cb13_m[6u].z) * _429) + ((_407 * cb13_m[15u].z) * _450));
    float _508 = mad(_408, cb13_m[29u].x, (_407 * cb13_m[20u].x) + (_406 * cb13_m[11u].x));
    float _511 = mad(_408, cb13_m[23u].x, mad(_407, cb13_m[14u].x, mad(_406, cb13_m[5u].x, 0.001000000047497451305389404296875f)));
    float _514 = cb13_m[22u].x * _408;
    float _517 = mad(_514, cb13_m[27u].x, (_468 * cb13_m[18u].x) + (_435 * cb13_m[9u].x));
    float _520 = mad(_514, cb13_m[26u].x, (_468 * cb13_m[17u].x) + (_435 * cb13_m[8u].x));
    float _523 = mad(_514, cb13_m[28u].x, (_468 * cb13_m[19u].x) + (_435 * cb13_m[10u].x));
    float _529 = 1.0f / max(((_406 + _407) + _408) + 0.001000000047497451305389404296875f, 0.001000000047497451305389404296875f);
    float _531 = _511 * _529;
    uint2 _541 = uint2(uint(cvt_f32_i32(gl_FragCoord.x)), uint(cvt_f32_i32(gl_FragCoord.y)));
    float4 _542 = t17.Load(int3(_541, 0u));
    float _546 = mad(_542.x, 2.0f, -1.0f);
    float _547 = mad(_542.y, 2.0f, -1.0f);
    float _548 = mad(_542.z, 2.0f, -1.0f);
    float3 _555 = float3(TEXCOORD5.x, TEXCOORD5.y, TEXCOORD5.z);
    float _557 = rsqrt(dp3_f32(_555, _555));
    float _558 = TEXCOORD5.x * _557;
    float _559 = TEXCOORD5.y * _557;
    float _560 = TEXCOORD5.z * _557;
    float3 _562 = float3(_546, _547, _548);
    float _563 = dp3_f32(float3(_558, _559, _560), _562);
    float _564 = _563 + _563;
    float _568 = _558 - (_546 * _564);
    float _569 = _559 - (_547 * _564);
    float _570 = _560 - (_548 * _564);
    float3 _571 = float3(_568, _569, _570);
    float _573 = rsqrt(dp3_f32(_571, _571));
    float _574 = _568 * _573;
    float _575 = _573 * _569;
    float _576 = _573 * _570;
    float _577 = -_574;
    float _578 = -_575;
    float _579 = -_576;
    bool _587 = _531 == 0.0f;
    uint _589;
    uint _592;
    uint _594;
    uint _596;
    uint _598;
    uint _600;
    _589 = 0u;
    _592 = 0u;
    _594 = 0u;
    _596 = 0u;
    _598 = 0u;
    _600 = 0u;
    uint _590;
    uint _593;
    uint _595;
    uint _597;
    uint _599;
    uint _601;
    uint _721;
    int _602 = 0;
    for (;;)
    {
        if (float(_602) >= cb5_m[0u].x)
        {
            break;
        }
        int _613 = _602 * 5;
        uint _618 = uint(_613 + 1);
        float _624 = (TEXCOORD5.x - cb1_m0.x) + cb5_m[_618].x;
        float _625 = cb5_m[_618].y + (TEXCOORD5.y - cb1_m0.y);
        float _626 = cb5_m[_618].z + (TEXCOORD5.z - cb1_m0.z);
        float3 _627 = float3(_624, _625, _626);
        float _628 = dp3_f32(_627, _627);
        uint _630 = uint(_613 + 5);
        if (cb5_m[_630].x <= _628)
        {
            _721 = uint(_602 + 1);
            _601 = _600;
            _599 = _598;
            _597 = _596;
            _595 = _594;
            _593 = _592;
            _590 = _589;
            int _603 = int(_721);
            _589 = _590;
            _592 = _593;
            _594 = _595;
            _596 = _597;
            _598 = _599;
            _600 = _601;
            _602 = _603;
            continue;
        }
        float _639 = rsqrt(_628);
        float3 _648 = float3(_624 * _639, _639 * _625, _639 * _626);
        uint _650 = uint(_613 + 2);
        uint _659 = uint(_613 + 4);
        uint _671 = uint(_613 + 3);
        float _683 = clamp(cb5_m[_650].w + exp2(cb5_m[_671].w * log2(max(mad(dp3_f32(_648, float3(cb5_m[_650].xyz)), cb5_m[_659].y, cb5_m[_659].w), 9.9999997473787516355514526367188e-05f))), 0.0f, 1.0f) * clamp(mad(1.0f / (_628 + cb5_m[_618].w), cb5_m[_659].x, cb5_m[_659].z), 9.9999997473787516355514526367188e-05f, 1.0f);
        float _689 = _683 * cb5_m[_671].x;
        float _690 = _683 * cb5_m[_671].y;
        float _691 = _683 * cb5_m[_671].z;
        float _693 = max(dp3_f32(_562, _648), 0.0500000007450580596923828125f);
        float _709 = _587 ? 1.0f : exp2(_531 * log2(max(dp3_f32(_648, float3(_577, _578, _579)), 0.0f)));
        _721 = uint(_602 + 1);
        _601 = asuint(mad(_689, _693, asfloat(_600)));
        _599 = asuint(mad(_693, _690, asfloat(_598)));
        _597 = asuint(mad(_693, _691, asfloat(_596)));
        _595 = asuint(mad(_689, _709, asfloat(_594)));
        _593 = asuint(mad(_709, _690, asfloat(_592)));
        _590 = asuint(mad(_709, _691, asfloat(_589)));
        int _603 = int(_721);
        _589 = _590;
        _592 = _593;
        _594 = _595;
        _596 = _597;
        _598 = _599;
        _600 = _601;
        _602 = _603;
        continue;
    }
    float4 _723 = t16.Load(int3(_541, 0u));
    _723 = saturate(_723);
    // _723.xyz = sRGB_Encode(_723.xyz);
    float _724 = _723.x;
    float _725 = _723.y;
    float _726 = _723.z;
    float _727 = _723.w;
    float _728 = _373 ? (_368 * _374) : 0.0f;
    float _729 = _373 ? (_374 * _369) : 0.0f;
    float _730 = _373 ? (_374 * _370) : 0.0f;
    float _731 = _728 * (-0.4886024892330169677734375f);
    float _732 = _729 * (-0.4886024892330169677734375f);
    float _733 = _730 * (-0.4886024892330169677734375f);
    float3 _779 = float3(_728, _729, _730);
    float _780 = dp3_f32(_779, _562);
    float _781 = max(_780, 0.0f);
    float3 _794 = float3(_577, _578, _579);
    bool _799 = (_780 > 0.0f) && (_563 > 0.0f);
    float _805 = (exp2(_531 * log2(max(dp3_f32(_779, _794), 0.0f))) * mad(_511, _529, 1.0f)) * 0.15915457904338836669921875f;
    float _830 = mad(_508, _724 - _505, _505);
    float _831 = mad(_508, _725 - _506, _506);
    float _832 = mad(_508, _726 - _507, _507);
    float _837 = exp2((mad(cb13_m[25u].x, _408, mad(_407, cb13_m[16u].x, mad(_406, cb13_m[7u].x, 0.004999999888241291046142578125f))) * _529) * log2(1.0f - clamp(_563, 0.0f, 1.0f)));
    float _845 = max(((dp3_f32(_794, float3(_343, _317, -_330)) * (-1.02332794666290283203125f)) + (_304 * 0.88622701168060302734375f)) * 0.3183098733425140380859375f, 0.0f);
    float _846 = max(((_305 * 0.88622701168060302734375f) + (dp3_f32(_794, float3(_344, _319, -_331)) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f, 0.0f);
    float _847 = max(((_306 * 0.88622701168060302734375f) + (dp3_f32(_794, float3(_345, _318, -_332)) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f, 0.0f);
    float3 _852 = float3(ddx_coarse(_577), ddx_coarse(_575), ddx_coarse(_579));
    float3 _858 = float3(ddy_coarse(_577), ddy_coarse(_575), ddy_coarse(_579));
    float _868 = max(mad(sqrt(max(sqrt(dp3_f32(_852, _852)), sqrt(dp3_f32(_858, _858)))), 6.0f, -0.60000002384185791015625f), (max(mad(_531, -0.004999999888241291046142578125f, 1.0099999904632568359375f), 0.00999999977648258209228515625f) * cb13_m[1u].x) * 4.0f);
    float3 _874 = float3(_574 * (-1.0f), _575 * 1.0f, _576 * (-1.0f));
    float4 _877 = t0.SampleLevel(s0, _874, _868);
    float4 _885 = t1.SampleLevel(s1, _874, _868);
    float _991 = cb2_m.x * mad(((_727 * mad(_837, 1.0f - _830, _830)) * mad(_517, mad(_531, asfloat(_594), _799 ? (_356 * _805) : 0.0f), (_520 * _845) + (_523 * (_845 * (cb13_m[0u].x * (((1.0f - cb3_m1.x) * ((_885.x * _885.w) * 256.0f)) + (((_877.x * _877.w) * cb3_m1.x) * 256.0f))))))) + (_418 * (_724 * ((((((_304 + (_356 * (-0.2820948064327239990234375f))) * 0.88622701168060302734375f) + (dp3_f32(_562, float3(_343 - (_356 * _731), _317 - (_356 * _732), -(_330 + (_356 * _733)))) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f) + ((_356 * _781) * 0.2809999883174896240234375f)) + asfloat(_600)))), COLOR.x, COLOR1.x);
    float _992 = cb2_m.x * mad((_418 * (_725 * (asfloat(_598) + (((((_305 + (_357 * (-0.2820948064327239990234375f))) * 0.88622701168060302734375f) + (dp3_f32(_562, float3(_344 - (_357 * _731), _319 - (_357 * _732), -((_357 * _733) + _331))) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f) + ((_357 * _781) * 0.2809999883174896240234375f))))) + (mad(_517, mad(_531, asfloat(_592), _799 ? (_357 * _805) : 0.0f), (_523 * ((((((_877.y * _877.w) * cb3_m1.y) * 256.0f) + ((1.0f - cb3_m1.y) * ((_885.y * _885.w) * 256.0f))) * cb13_m[0u].y) * _846)) + (_520 * _846)) * (_727 * mad(_837, 1.0f - _831, _831))), COLOR.y, COLOR1.y);
    float _993 = cb2_m.x * mad((_418 * (_726 * (asfloat(_596) + (((((_306 + (_358 * (-0.2820948064327239990234375f))) * 0.88622701168060302734375f) + (dp3_f32(_562, float3(_345 - (_358 * _731), _318 - (_358 * _732), -((_358 * _733) + _332))) * (-1.02332794666290283203125f))) * 0.3183098733425140380859375f) + ((_358 * _781) * 0.2809999883174896240234375f))))) + (mad(_517, mad(_531, asfloat(_589), _799 ? (_358 * _805) : 0.0f), (_523 * ((((((_877.z * _877.w) * cb3_m1.z) * 256.0f) + ((1.0f - cb3_m1.z) * ((_885.z * _885.w) * 256.0f))) * cb13_m[0u].z) * _847)) + (_520 * _847)) * (_727 * mad(_837, 1.0f - _832, _832))), COLOR.z, COLOR1.z);
    float _994 = max(_991, 0.0f);
    float _995 = max(_992, 0.0f);
    float _996 = max(_993, 0.0f);
    float _999 = _994 / cb2_m.y;
    float _1000 = _995 / cb2_m.y;
    float _1001 = _996 / cb2_m.y;
    bool _1014 = cb3_m6.x != 0u;
    SV_Target.x = _1014 ? ((_994 <= 0.0f) ? 0.0f : sqrt(_994)) : _994;
    SV_Target.y = _1014 ? ((_995 <= 0.0f) ? 0.0f : sqrt(_995)) : _995;
    SV_Target.z = _1014 ? ((_996 <= 0.0f) ? 0.0f : sqrt(_996)) : _996;
    bool _1033 = cb3_m6.y != 0u;
    SV_Target1.x = _1033 ? ((_999 <= 0.0f) ? 0.0f : sqrt(_999)) : _999;
    SV_Target1.y = _1033 ? ((_1000 <= 0.0f) ? 0.0f : sqrt(_1000)) : _1000;
    SV_Target1.z = _1033 ? ((_1001 <= 0.0f) ? 0.0f : sqrt(_1001)) : _1001;
    SV_Target.w = cb2_m.w;
    SV_Target1.w = cb2_m.z;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    TEXCOORD = stage_input.TEXCOORD.xy;
    TEXCOORD4 = stage_input.TEXCOORD4;
    TEXCOORD5 = stage_input.TEXCOORD5;
    COLOR = stage_input.COLOR;
    COLOR1 = stage_input.COLOR1;
    frag_main();
    SPIRV_Cross_Output stage_output;
        SV_Target = max(0, SV_Target);
        SV_Target1 = max(0, SV_Target1);
        SV_Target.w = min(1, SV_Target.w);
        SV_Target1.w = min(1, SV_Target1.w);
        // SV_Target = saturate(SV_Target);
        // SV_Target1 = saturate(SV_Target1);
    stage_output.SV_Target = SV_Target;
    stage_output.SV_Target1 = SV_Target1;
    return stage_output;
}
