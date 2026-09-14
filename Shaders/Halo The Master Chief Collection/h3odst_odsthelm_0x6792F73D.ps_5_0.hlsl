cbuffer cb0_buf : register(b0)
{
    float4 cb0_m : packoffset(c0);
};

cbuffer cb13_buf : register(b13)
{
    uint4 cb13_m[22] : packoffset(c0);
};

cbuffer cb1_buf : register(b1)
{
    float3 cb1_m0 : packoffset(c0);
    uint cb1_m1 : packoffset(c0.w);
};

cbuffer cb2_buf : register(b2)
{
    float4 cb2_m0 : packoffset(c0);
    float4 cb2_m1 : packoffset(c1);
};

cbuffer cb3_buf : register(b3)
{
    float3 cb3_m0 : packoffset(c0);
    uint cb3_m1 : packoffset(c0.w);
    float3 cb3_m2 : packoffset(c1);
    uint cb3_m3 : packoffset(c1.w);
    float3 cb3_m4 : packoffset(c2);
    uint cb3_m5 : packoffset(c2.w);
    float3 cb3_m6 : packoffset(c3);
    uint cb3_m7 : packoffset(c3.w);
    float3 cb3_m8 : packoffset(c4);
    uint cb3_m9 : packoffset(c4.w);
    float3 cb3_m10 : packoffset(c5);
    uint cb3_m11 : packoffset(c5.w);
    float3 cb3_m12 : packoffset(c6);
    uint cb3_m13 : packoffset(c6.w);
    float4 cb3_m14 : packoffset(c7);
    float4 cb3_m15 : packoffset(c8);
    float4 cb3_m16 : packoffset(c9);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m0 : packoffset(c0);
    float3 cb4_m1 : packoffset(c1);
    uint cb4_m2 : packoffset(c1.w);
    uint4 cb4_m3 : packoffset(c2);
    uint4 cb4_m4 : packoffset(c3);
};

cbuffer cb5_buf : register(b5)
{
    float4 cb5_m[4096] : packoffset(c0);
};

cbuffer cb6_buf : register(b6)
{
    float3 cb6_m0 : packoffset(c0);
    uint cb6_m1 : packoffset(c0.w);
    float3 cb6_m2 : packoffset(c1);
    uint cb6_m3 : packoffset(c1.w);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);
SamplerState s5 : register(s5);
SamplerState s6 : register(s6);
SamplerState s7 : register(s7);
SamplerState s8 : register(s8);
SamplerState s9 : register(s9);
SamplerState s10 : register(s10);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t7 : register(t7);
Texture2D<float4> t8 : register(t8);
TextureCube<float4> t9 : register(t9);
TextureCube<float4> t10 : register(t10);
Texture2D<float4> t16 : register(t16);
Texture2D<float4> t17 : register(t17);

static float4 gl_FragCoord;
static float2 TEXCOORD;
static float3 TEXCOORD3;
static float3 TEXCOORD4;
static float3 TEXCOORD5;
static float3 TEXCOORD6;
static float4 TEXCOORD7;
static float3 COLOR;
static float3 COLOR1;
static float4 SV_Target;
static float4 SV_Target1;

struct SPIRV_Cross_Input
{
    float4 gl_FragCoord : SV_Position0;
    float4 v1 : SV_ClipDistance0;

    float2 TEXCOORD : TEXCOORD0;
    float3 TEXCOORD3 : TEXCOORD3;
    float3 TEXCOORD4 : TEXCOORD4;
    float3 TEXCOORD5 : TEXCOORD5;
    float3 TEXCOORD6 : TEXCOORD6;
    float4 TEXCOORD7 : TEXCOORD7;
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
    precise float _212 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _212));
}

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

float dp4_f32(float4 a, float4 b)
{
    precise float _183 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _183)));
}

float dp2_f32(float2 a, float2 b)
{
    precise float _171 = a.x * b.x;
    return mad(a.y, b.y, _171);
}

void frag_main()
{
    float3 _229 = float3(TEXCOORD6.x, TEXCOORD6.y, TEXCOORD6.z);
    float _231 = rsqrt(dp3_f32(_229, _229));
    float _232 = TEXCOORD6.x * _231;
    float _233 = TEXCOORD6.y * _231;
    float _234 = TEXCOORD6.z * _231;
    float _401;
    float _402;
    float _403;
    float _404;
    float _405;
    float _406;
    float _407;
    if (cb4_m4.x != 0u)
    {
        float4 _270 = t2.SampleBias(s2, float2(mad(TEXCOORD.x, asfloat(cb13_m[3u].x), asfloat(cb13_m[3u].z)), mad(TEXCOORD.y, asfloat(cb13_m[3u].y), asfloat(cb13_m[3u].w))), cb0_m.x);
        float _271 = _270.x;
        float _272 = _270.y;
        float _278 = sqrt(1.0f - min((_271 * _271) + (_272 * _272), 1.0f));
        float _306 = mad(_278, TEXCOORD3.x, (_272 * TEXCOORD4.x) + (_271 * TEXCOORD5.x));
        float _307 = mad(_278, TEXCOORD3.y, (_271 * TEXCOORD5.y) + (_272 * TEXCOORD4.y));
        float _308 = mad(_278, TEXCOORD3.z, (_271 * TEXCOORD5.z) + (_272 * TEXCOORD4.z));
        float3 _309 = float3(_306, _307, _308);
        float _311 = rsqrt(dp3_f32(_309, _309));
        float4 _331 = t0.SampleBias(s0, float2(mad(TEXCOORD.x, asfloat(cb13_m[1u].x), asfloat(cb13_m[1u].z)), mad(TEXCOORD.y, asfloat(cb13_m[1u].y), asfloat(cb13_m[1u].w))), cb0_m.x);
        float4 _352 = t1.SampleBias(s1, float2(mad(TEXCOORD.x, asfloat(cb13_m[2u].x), asfloat(cb13_m[2u].z)), mad(TEXCOORD.y, asfloat(cb13_m[2u].y), asfloat(cb13_m[2u].w))), cb0_m.x);
        _401 = asfloat(cb13_m[0u].w) * (_331.w * _352.w);
        _402 = (asfloat(cb13_m[0u].z) * (_331.z * _352.z)) * 4.594789981842041015625f;
        _403 = (asfloat(cb13_m[0u].y) * (_331.y * _352.y)) * 4.594789981842041015625f;
        _404 = ((_331.x * _352.x) * asfloat(cb13_m[0u].x)) * 4.594789981842041015625f;
        _405 = _311 * _308;
        _406 = _311 * _307;
        _407 = _306 * _311;
    }
    else
    {
        uint2 _387 = uint2(uint(cvt_f32_i32(gl_FragCoord.x)), uint(cvt_f32_i32(gl_FragCoord.y)));
        float4 _388 = t17.Load(int3(_387, 0u));
        float4 _396 = t16.Load(int3(_387, 0u));
        _401 = _396.w;
        _402 = _396.z;
        _403 = _396.y;
        _404 = _396.x;
        _405 = mad(_388.z, 2.0f, -1.0f);
        _406 = mad(_388.y, 2.0f, -1.0f);
        _407 = mad(_388.x, 2.0f, -1.0f);
    }
    float3 _408 = float3(_407, _406, _405);
    float _410 = sqrt(dp3_f32(_408, _408));
    float _411 = _407 / _410;
    float _412 = _406 / _410;
    float _413 = _405 / _410;
    float4 _435 = t4.Sample(s4, float2(mad(TEXCOORD.x, asfloat(cb13_m[7u].x), asfloat(cb13_m[7u].z)), mad(TEXCOORD.y, asfloat(cb13_m[7u].y), asfloat(cb13_m[7u].w))));
    float _436 = _435.w;
    float3 _437 = float3(_232, _233, _234);
    float3 _438 = float3(_411, _412, _413);
    float _439 = dp3_f32(_437, _438);
    float _440 = _411 * _439;
    float _441 = _439 * _412;
    float _442 = _439 * _413;
    float _449 = ((_440 - _232) * 2.0f) + _232;
    float _450 = _233 + ((_441 - _233) * 2.0f);
    float _451 = _234 + ((_442 - _234) * 2.0f);
    float _456 = dp3_f32(_438, cb3_m2);
    float _459 = dp3_f32(_438, cb3_m4);
    float _462 = dp3_f32(_438, cb3_m6);
    float _463 = _411 * _412;
    float _464 = _413 * _412;
    float _465 = _411 * _413;
    float3 _466 = float3(_463, _464, _465);
    float _476 = _411 * _411;
    float _477 = _412 * _412;
    float _478 = _413 * _413;
    float4 _479 = float4(_476, _477, _478, 0.3333333432674407958984375f);
    bool _517 = cb13_m[21u].x != 0u;
    float _557;
    float _558;
    float _559;
    float _560;
    if (_517)
    {
        float4 _540 = t5.SampleBias(s5, float2(mad(TEXCOORD.x, asfloat(cb13_m[13u].x), asfloat(cb13_m[13u].z)), mad(TEXCOORD.y, asfloat(cb13_m[13u].y), asfloat(cb13_m[13u].w))), cb0_m.x);
        _557 = _540.z;
        _558 = _540.x;
        _559 = _540.w;
        _560 = _540.y;
    }
    else
    {
        _557 = asfloat(cb13_m[12u].x);
        _558 = asfloat(cb13_m[9u].x);
        _559 = asfloat(cb13_m[15u].x);
        _560 = asfloat(cb13_m[16u].x);
    }
    float _561 = 1.0f - _560;
    float _576 = (_561 * asfloat(cb13_m[14u].x)) + (_560 * _404);
    float _577 = (_560 * _403) + (_561 * asfloat(cb13_m[14u].y));
    float _578 = (_560 * _402) + (_561 * asfloat(cb13_m[14u].z));
    float _583 = min(_439, dp3_f32(_438, cb6_m0));
    bool _584 = _583 > 0.0f;
    float _588 = mad(TEXCOORD6.x, _231, cb6_m0.x);
    float _589 = mad(TEXCOORD6.y, _231, cb6_m0.y);
    float _590 = mad(TEXCOORD6.z, _231, cb6_m0.z);
    float3 _591 = float3(_588, _589, _590);
    float _593 = rsqrt(dp3_f32(_591, _591));
    float3 _597 = float3(_588 * _593, _593 * _589, _593 * _590);
    float _598 = dp3_f32(_438, _597);
    float _599 = dp3_f32(_437, _597);
    float _610 = sqrt(min(_576, 0.999000012874603271484375f));
    float _611 = sqrt(min(_577, 0.999000012874603271484375f));
    float _612 = sqrt(min(_578, 0.999000012874603271484375f));
    float _619 = (_610 + 1.0f) / (1.0f - _610);
    float _620 = (_611 + 1.0f) / (1.0f - _611);
    float _621 = (_612 + 1.0f) / (1.0f - _612);
    float _622 = _599 * _599;
    float _632 = sqrt(((_619 * _619) + _622) - 1.0f);
    float _633 = sqrt((_622 + (_620 * _620)) - 1.0f);
    float _634 = sqrt((_622 + (_621 * _621)) - 1.0f);
    float _635 = _599 + _632;
    float _636 = _599 + _633;
    float _637 = _599 + _634;
    float _638 = _632 - _599;
    float _639 = _633 - _599;
    float _640 = _634 - _599;
    float _647 = mad(_599, _635, -1.0f) / mad(_599, _638, 1.0f);
    float _648 = mad(_599, _636, -1.0f) / mad(_599, _639, 1.0f);
    float _649 = mad(_599, _637, -1.0f) / mad(_599, _640, 1.0f);
    float _668 = max(_559, 0.0500000007450580596923828125f);
    float _670 = _598 * _598;
    float _672 = (_668 * _668) * _670;
    float _680 = (clamp(dp2_f32(_583.xx, _598.xx) / (clamp(_599, 0.0f, 1.0f) + 9.9999997473787516355514526367188e-06f), 0.0f, 1.0f) * (exp2((mad(_598, _598, -1.0f) / _672) * 1.44269502162933349609375f) / mad(_670, _672, 9.9999997473787516355514526367188e-06f))) / mad(_439, 3.1415927410125732421875f, 9.9999997473787516355514526367188e-06f);
    float _686 = TEXCOORD7.w + 1.0f;
    float _698 = _584 ? (min((mad(_647, _647, 1.0f) * (((_638 * _638) / mad(_635, _635, 9.9999997473787516355514526367188e-06f)) * 0.5f)) * _680, _686) * cb6_m2.x) : 9.9999997473787516355514526367188e-06f;
    float _699 = _584 ? (min(_686, _680 * (mad(_648, _648, 1.0f) * (((_639 * _639) / mad(_636, _636, 9.9999997473787516355514526367188e-06f)) * 0.5f))) * cb6_m2.y) : 9.9999997473787516355514526367188e-06f;
    float _700 = _584 ? (min(_686, _680 * (mad(_649, _649, 1.0f) * (((_640 * _640) / mad(_637, _637, 9.9999997473787516355514526367188e-06f)) * 0.5f))) * cb6_m2.z) : 9.9999997473787516355514526367188e-06f;
    float _703 = asfloat(cb13_m[18u].x);
    bool _704 = _703 > 0.0f;
    float4 _719 = float4(mad(cb6_m2.y * cb6_m0.x, 0.4886024892330169677734375f, cb3_m4.x), mad(cb6_m2.y * cb6_m0.y, 0.4886024892330169677734375f, cb3_m4.y), mad(cb6_m2.y * cb6_m0.z, 0.4886024892330169677734375f, cb3_m4.z), mad(cb6_m2.y, -0.2820948064327239990234375f, cb3_m0.y));
    float _729 = exp2((_703 * 100.0f) * log2(1.0f - min(dp4_f32(_719, _719) / mad(cb3_m0.y, cb3_m0.y, dp3_f32(cb3_m4, cb3_m4)), 1.0f)));
    float _891;
    float _892;
    float _893;
    float _894;
    float _895;
    float _896;
    if (cb13_m[21u].z == 0u)
    {
        float _750 = asfloat(cb13_m[15u].x);
        float _756 = (_750 != 0.0f) ? (exp2(log2(_750) * (-2.197299957275390625f)) * 0.27290999889373779296875f) : 0.0f;
        bool _757 = _756 == 0.0f;
        uint _759;
        uint _762;
        uint _764;
        _759 = 0u;
        _762 = 0u;
        _764 = 0u;
        uint _760;
        uint _763;
        uint _765;
        uint _767;
        uint _769;
        uint _771;
        uint _773;
        uint _766 = 0u;
        uint _768 = 0u;
        uint _770 = 0u;
        uint _772 = 0u;
        float _774;
        float _775;
        float _776;
        for (;;)
        {
            _774 = asfloat(_770);
            _775 = asfloat(_768);
            _776 = asfloat(_766);
            if (float(int(_772)) >= cb5_m[0u].x)
            {
                break;
            }
            uint _787 = _772 * 5u;
            uint _791 = _787 + 1u;
            float _797 = (TEXCOORD6.x - cb1_m0.x) + cb5_m[_791].x;
            float _798 = (TEXCOORD6.y - cb1_m0.y) + cb5_m[_791].y;
            float _799 = (TEXCOORD6.z - cb1_m0.z) + cb5_m[_791].z;
            float3 _800 = float3(_797, _798, _799);
            float _801 = dp3_f32(_800, _800);
            uint _802 = _787 + 5u;
            if (cb5_m[_802].x <= _801)
            {
                _773 = _772 + 1u;
                _771 = _770;
                _769 = _768;
                _767 = _766;
                _765 = _764;
                _763 = _762;
                _760 = _759;
                _759 = _760;
                _762 = _763;
                _764 = _765;
                _766 = _767;
                _768 = _769;
                _770 = _771;
                _772 = _773;
                continue;
            }
            float _810 = rsqrt(_801);
            float3 _819 = float3(_797 * _810, _798 * _810, _799 * _810);
            uint _820 = _787 + 2u;
            uint _828 = _787 + 4u;
            uint _839 = _787 + 3u;
            float _851 = clamp(cb5_m[_820].w + exp2(cb5_m[_839].w * log2(max(mad(dp3_f32(_819, float3(cb5_m[_820].xyz)), cb5_m[_828].y, cb5_m[_828].w), 0.0f))), 0.0f, 1.0f) * clamp(mad(1.0f / (_801 + cb5_m[_791].w), cb5_m[_828].x, cb5_m[_828].z), 0.0f, 1.0f);
            float _857 = _851 * cb5_m[_839].x;
            float _858 = _851 * cb5_m[_839].y;
            float _859 = _851 * cb5_m[_839].z;
            float _861 = max(dp3_f32(_438, _819), 0.0500000007450580596923828125f);
            float _874 = _757 ? 1.0f : exp2(_756 * log2(max(dp3_f32(_819, float3(_449, _450, _451)), 0.0f)));
            _773 = _772 + 1u;
            _771 = asuint(mad(_857, _861, _774));
            _769 = asuint(mad(_861, _858, _775));
            _767 = asuint(mad(_861, _859, _776));
            _765 = asuint(mad(_857, _874, asfloat(_764)));
            _763 = asuint(mad(_874, _858, asfloat(_762)));
            _760 = asuint(mad(_874, _859, asfloat(_759)));
            _759 = _760;
            _762 = _763;
            _764 = _765;
            _766 = _767;
            _768 = _769;
            _770 = _771;
            _772 = _773;
            continue;
        }
        _891 = _776;
        _892 = _775;
        _893 = _774;
        _894 = _756 * asfloat(_759);
        _895 = _756 * asfloat(_762);
        _896 = _756 * asfloat(_764);
    }
    else
    {
        _891 = 0.0f;
        _892 = 0.0f;
        _893 = 0.0f;
        _894 = 0.0f;
        _895 = 0.0f;
        _896 = 0.0f;
    }
    float _900 = mad(max(dp3_f32(cb6_m0, float3(_449, _450, _451)), 0.0f), 0.64999997615814208984375f, 0.3499999940395355224609375f);
    float _1147;
    float _1148;
    float _1149;
    float _1150;
    float _1151;
    float _1152;
    if (cb13_m[21u].y != 0u)
    {
        float _907 = _232 - _440;
        float _908 = _233 - _441;
        float _909 = _234 - _442;
        float3 _910 = float3(_907, _908, _909);
        float _912 = rsqrt(dp3_f32(_910, _910));
        float _913 = _907 * _912;
        float _914 = _912 * _908;
        float _915 = _912 * _909;
        float _916 = _913 * _412;
        float _917 = _413 * _914;
        float _918 = _411 * _915;
        float _919 = _412 * _915;
        float _920 = _913 * _413;
        float _921 = _411 * _914;
        float _922 = _919 - _917;
        float _923 = _920 - _918;
        float _924 = _921 - _916;
        float3 _925 = float3(_913, _914, _915);
        float2 _930 = float2(dp3_f32(_437, _925) + 0.015625f, _668);
        float4 _932 = t6.Sample(s6, _930);
        float4 _936 = t7.Sample(s7, _930);
        float3 _944 = float3(_463 * (-1.73205077648162841796875f), _464 * (-1.73205077648162841796875f), _465 * (-1.73205077648162841796875f));
        float4 _946 = float4(_476 * (-0.866025388240814208984375f), _477 * (-0.866025388240814208984375f), _478 * (-0.866025388240814208984375f), -0.288675129413604736328125f);
        float4 _956 = float4(cb3_m0.x * 1.0f, -(_456 * _900), _900 * dp3_f32(_925, cb3_m2), _900 * (dp3_f32(_944, cb3_m8) + dp4_f32(_946, cb3_m14)));
        float4 _968 = float4(cb3_m0.y * 1.0f, -(_459 * _900), _900 * dp3_f32(_925, cb3_m4), _900 * (dp3_f32(_944, cb3_m10) + dp4_f32(_946, cb3_m15)));
        float4 _982 = float4(cb3_m0.z * 1.0f, -(_462 * _900), _900 * dp3_f32(_925, cb3_m6), _900 * (dp3_f32(_944, cb3_m12) + dp4_f32(_946, cb3_m16)));
        float4 _988 = t8.Sample(s8, _930);
        float _989 = _988.x;
        float _990 = _988.y;
        float _991 = _988.z;
        float _992 = _988.w;
        float3 _999 = float3(_916 + _921, _919 + _917, _920 + _918);
        float3 _1001 = float3(_411 * _913, _412 * _914, _413 * _915);
        float3 _1007 = float3(cb3_m14.xyz);
        float3 _1016 = float3(cb3_m15.xyz);
        float3 _1025 = float3(cb3_m16.xyz);
        float _1028 = _900 * (dp3_f32(_999, cb3_m8) + dp3_f32(_1001, _1007));
        float _1029 = _900 * (dp3_f32(_999, cb3_m10) + dp3_f32(_1001, _1016));
        float _1030 = _900 * (dp3_f32(_999, cb3_m12) + dp3_f32(_1001, _1025));
        float3 _1058 = float3((_913 * _914) - (_922 * _923), (_915 * _914) - (_924 * _923), (_913 * _915) - (_922 * _924));
        float3 _1060 = float3(((_913 * _913) - (_922 * _922)) * 0.5f, ((_914 * _914) - (_923 * _923)) * 0.5f, ((_915 * _915) - (_924 * _924)) * 0.5f);
        float _1069 = (dp3_f32(_1058, cb3_m8) + dp3_f32(_1060, _1007)) * _900;
        float _1070 = (dp3_f32(_1058, cb3_m10) + dp3_f32(_1060, _1016)) * _900;
        float _1071 = (dp3_f32(_1058, cb3_m12) + dp3_f32(_1060, _1025)) * _900;
        _1147 = mad(-_1071, _990, mad(_989, _1030, dp4_f32(_932, _982)));
        _1148 = mad(-_1070, _990, mad(_989, _1029, dp4_f32(_932, _968)));
        _1149 = mad(-_1069, _990, mad(_989, _1028, dp4_f32(_932, _956)));
        _1150 = mad(-_1071, _992, mad(_991, _1030, dp4_f32(_936, _982))) * 0.00999999977648258209228515625f;
        _1151 = mad(-_1070, _992, mad(_991, _1029, dp4_f32(_936, _968))) * 0.00999999977648258209228515625f;
        _1152 = mad(-_1069, _992, mad(_991, _1028, dp4_f32(_936, _956))) * 0.00999999977648258209228515625f;
    }
    else
    {
        float _1087 = _232 - _440;
        float _1088 = _233 - _441;
        float _1089 = _234 - _442;
        float3 _1090 = float3(_1087, _1088, _1089);
        float _1092 = rsqrt(dp3_f32(_1090, _1090));
        float3 _1096 = float3(_1087 * _1092, _1092 * _1088, _1092 * _1089);
        float2 _1101 = float2(dp3_f32(_437, _1096) + 0.015625f, _668);
        float3 _1121 = float3(t6.Sample(s6, _1101).xyz);
        float3 _1122 = float3(cb3_m0.x * 1.0f, -(_456 * _900), _900 * dp3_f32(_1096, cb3_m2));
        float3 _1124 = float3(t7.Sample(s7, _1101).xyz);
        float3 _1131 = float3(cb3_m0.y * 1.0f, -(_459 * _900), _900 * dp3_f32(_1096, cb3_m4));
        float3 _1141 = float3(cb3_m0.z * 1.0f, -(_462 * _900), _900 * dp3_f32(_1096, cb3_m6));
        _1147 = dp3_f32(_1121, _1141);
        _1148 = dp3_f32(_1121, _1131);
        _1149 = dp3_f32(_1121, _1122);
        _1150 = dp3_f32(_1124, _1141) * 0.00999999977648258209228515625f;
        _1151 = dp3_f32(_1124, _1131) * 0.00999999977648258209228515625f;
        _1152 = dp3_f32(_1124, _1122) * 0.00999999977648258209228515625f;
    }
    float _1162 = (_1152 * (1.0f - _576)) + (_576 * _1149);
    float _1163 = (_577 * _1148) + (_1151 * (1.0f - _577));
    float _1164 = (_578 * _1147) + ((1.0f - _578) * _1150);
    float _1175 = asfloat(cb13_m[17u].x);
    float _1176 = asfloat(cb13_m[17u].y);
    float _1177 = asfloat(cb13_m[17u].z);
    float _1181 = _436 * _558;
    float _1194 = asfloat(cb13_m[10u].x);
    float _1200 = asfloat(cb13_m[11u].x);
    float _1210 = (_436 * _557) * _558;
    float _1215 = _517 ? (1.0f - _558) : asfloat(cb13_m[8u].x);
    float _1228 = -_450;
    float3 _1232 = float3(ddx_coarse(_449), ddx_coarse(_1228), ddx_coarse(_451));
    float3 _1238 = float3(ddy_coarse(_449), ddy_coarse(_1228), ddy_coarse(_451));
    float _1249 = max(mad(sqrt(max(sqrt(dp3_f32(_1232, _1232)), sqrt(dp3_f32(_1238, _1238)))), 6.0f, -0.60000002384185791015625f), (asfloat(cb13_m[20u].x) * _559) * 4.0f);
    float3 _1252 = float3(_449, _1228, _451);
    float4 _1255 = t9.SampleLevel(s9, _1252, _1249);
    float _1259 = _1255.w;
    float4 _1263 = t10.SampleLevel(s10, _1252, _1249);
    float _1267 = _1263.w;
    float4 _1327 = t3.Sample(s3, float2(mad(TEXCOORD.x, asfloat(cb13_m[5u].x), asfloat(cb13_m[5u].z)), mad(TEXCOORD.y, asfloat(cb13_m[5u].y), asfloat(cb13_m[5u].w))));
    float _1344 = asfloat(cb13_m[4u].x);
    float _1375 = mad(mad(max((_1162 * TEXCOORD7.z) * _1175, 0.001000000047497451305389404296875f), (_1210 * ((((_1263.x * _1267) * 256.0f) * (1.0f - cb4_m1.x)) + ((cb4_m1.x * (_1255.x * _1259)) * 256.0f))) * asfloat(cb13_m[19u].x), mad(cb2_m1.x, (_1327.x * asfloat(cb13_m[6u].x)) * _1344, (TEXCOORD7.z * ((_1175 * _1181) * ((max(_1162, 0.0f) * _1194) + (mad(_576, _896, _704 ? (_729 * _698) : _698) * _1200)))) + ((_1215 * mad(mad(dp4_f32(_479, cb3_m14), -0.429042994976043701171875f, mad(dp3_f32(_466, cb3_m8), -0.85808598995208740234375f, (cb3_m0.x * 0.88622701168060302734375f) + (_456 * (-1.02332794666290283203125f)))) * TEXCOORD7.x, 0.3183098733425140380859375f, _893)) * _404))), COLOR.x, COLOR1.x);
    float _1376 = mad(mad((((((_1255.y * _1259) * cb4_m1.y) * 256.0f) + ((1.0f - cb4_m1.y) * ((_1263.y * _1267) * 256.0f))) * _1210) * asfloat(cb13_m[19u].y), max(_1176 * (TEXCOORD7.z * _1163), 0.001000000047497451305389404296875f), mad(cb2_m1.x, (_1327.y * asfloat(cb13_m[6u].y)) * _1344, ((_1215 * mad(TEXCOORD7.x * mad(dp4_f32(_479, cb3_m15), -0.429042994976043701171875f, mad(dp3_f32(_466, cb3_m10), -0.85808598995208740234375f, (cb3_m0.y * 0.88622701168060302734375f) + (_459 * (-1.02332794666290283203125f)))), 0.3183098733425140380859375f, _892)) * _403) + (TEXCOORD7.z * (((_1200 * mad(_577, _895, _704 ? (_729 * _699) : _699)) + (_1194 * max(_1163, 0.0f))) * (_1176 * _1181))))), COLOR.y, COLOR1.y);
    float _1377 = mad(mad((((((_1255.z * _1259) * cb4_m1.z) * 256.0f) + ((1.0f - cb4_m1.z) * ((_1263.z * _1267) * 256.0f))) * _1210) * asfloat(cb13_m[19u].z), max(_1177 * (TEXCOORD7.z * _1164), 0.001000000047497451305389404296875f), mad(cb2_m1.x, (_1327.z * asfloat(cb13_m[6u].z)) * _1344, ((_1215 * mad(TEXCOORD7.x * mad(dp4_f32(_479, cb3_m16), -0.429042994976043701171875f, mad(dp3_f32(_466, cb3_m12), -0.85808598995208740234375f, (cb3_m0.z * 0.88622701168060302734375f) + (_462 * (-1.02332794666290283203125f)))), 0.3183098733425140380859375f, _891)) * _402) + (TEXCOORD7.z * (((_1200 * mad(_578, _894, _704 ? (_729 * _700) : _700)) + (_1194 * max(_1164, 0.0f))) * (_1177 * _1181))))), COLOR.z, COLOR1.z);
    float _1383 = max(cb2_m0.x * _1375, 0.0f);
    float _1384 = max(cb2_m0.x * _1376, 0.0f);
    float _1385 = max(cb2_m0.x * _1377, 0.0f);
    SV_Target.w = _401 * cb2_m0.w;
    SV_Target1.x = _1383 / cb2_m0.y;
    SV_Target1.y = _1384 / cb2_m0.y;
    SV_Target1.z = _1385 / cb2_m0.y;
    SV_Target1.w = _401 * cb2_m0.z;
    SV_Target.x = _1383;
    SV_Target.y = _1384;
    SV_Target.z = _1385;
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
      stage_output.SV_Target = max(0, stage_output.SV_Target);
      stage_output.SV_Target.w = min(stage_output.SV_Target.w, 1);
    stage_output.SV_Target1 = SV_Target1;
      stage_output.SV_Target1 = max(0, stage_output.SV_Target1);
      stage_output.SV_Target1.w = min(stage_output.SV_Target1.w, 1);
    return stage_output;
}
