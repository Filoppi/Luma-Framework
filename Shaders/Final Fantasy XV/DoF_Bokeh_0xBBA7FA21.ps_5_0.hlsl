#include "Includes/Common.hlsl"

cbuffer cb0_buf : register(b0)
{
    float4 cb0_m[48] : packoffset(c0);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m0 : packoffset(c0);
    float4 cb1_m1 : packoffset(c1);
    uint4 cb1_m2 : packoffset(c2);
    uint4 cb1_m3 : packoffset(c3);
    uint4 cb1_m4 : packoffset(c4);
    uint2 cb1_m5 : packoffset(c5);
    float2 cb1_m6 : packoffset(c5.z);
};

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);

static float2 TEXCOORD;
static float4 SV_TARGET;

struct SPIRV_Cross_Input
{
    float4 Position : SV_Position;

    float2 TEXCOORD : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 SV_TARGET : SV_Target0;
};

void frag_main()
{
    float2 _166 = float2(TEXCOORD.x, TEXCOORD.y);
    float4 _168 = t2.SampleLevel(s1, _166, 0.0f);
    float _169 = _168.x;
    float _170 = max(t1.SampleLevel(s1, float2(mad(cb1_m6.x, -0.5f, TEXCOORD.x), mad(cb1_m6.y, -0.5f, TEXCOORD.y)), 0.0f).y, t1.SampleLevel(s1, float2(mad(cb1_m6.x, 0.5f, TEXCOORD.x), mad(cb1_m6.y, 0.5f, TEXCOORD.y)), 0.0f).y);
    float4 _173 = t0.SampleLevel(s1, _166, 0.0f);
    float _174 = _173.x;
    float _175 = _173.y;
    float _176 = _173.z;
    float _179 = max(_169, max(_169, _170));
    float _183 = _179 * cb1_m1.w;
    float resolutionScale = 1.0f;
    if (LumaData.GameData.IsUpscaling != 0)
    {
        resolutionScale = LumaData.RenderResolutionScale.x;
    }

    float _5881;
    float _5882;
    float _5883;
    if (_183 > 1.5f)
    {
        float _189 = _169 * cb1_m1.w;
        float _5878;
        float _5879;
        float _5880;
        if (max(_170 * cb1_m1.w, _189) == _189)
        {
            float2 _206 = float2(mad(_179, cb0_m[0u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[0u].y / resolutionScale, TEXCOORD.y));
            float4 _208 = t1.SampleLevel(s1, _206, 0.0f);
            float _212 = cb1_m1.w * 0.5f;
            float _215 = clamp((max(_208.x, _208.y) * _212) - (_179 * cb0_m[0u].z / resolutionScale), 0.0f, 1.0f);
            float _233;
            float _234;
            float _235;
            float _236;
            if (_215 > 0.0f)
            {
                float4 _222 = t0.SampleLevel(s0, _206, 0.0f);
                _233 = _215 + 1.0f;
                _234 = mad(_215, _222.z, _176);
                _235 = mad(_215, _222.y, _175);
                _236 = mad(_215, _222.x, _174);
            }
            else
            {
                _233 = 2.0f;
                _234 = _176 + _176;
                _235 = _175 + _175;
                _236 = _174 + _174;
            }
            float2 _246 = float2(mad(_179, cb0_m[1u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[1u].y / resolutionScale, TEXCOORD.y));
            float4 _248 = t1.SampleLevel(s1, _246, 0.0f);
            float _254 = clamp((_212 * max(_248.x, _248.y)) - (_179 * cb0_m[1u].z / resolutionScale), 0.0f, 1.0f);
            float _273;
            float _274;
            float _275;
            float _276;
            if (_254 > 0.0f)
            {
                float4 _261 = t0.SampleLevel(s0, _246, 0.0f);
                _273 = _233 + _254;
                _274 = mad(_254, _261.z, _234);
                _275 = mad(_254, _261.y, _235);
                _276 = mad(_254, _261.x, _236);
            }
            else
            {
                _273 = _233 + 1.0f;
                _274 = _176 + _234;
                _275 = _175 + _235;
                _276 = _174 + _236;
            }
            float2 _286 = float2(mad(_179, cb0_m[2u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[2u].y / resolutionScale, TEXCOORD.y));
            float4 _288 = t1.SampleLevel(s1, _286, 0.0f);
            float _294 = clamp((_212 * max(_288.x, _288.y)) - (_179 * cb0_m[2u].z / resolutionScale), 0.0f, 1.0f);
            float _313;
            float _314;
            float _315;
            float _316;
            if (_294 > 0.0f)
            {
                float4 _301 = t0.SampleLevel(s0, _286, 0.0f);
                _313 = _294 + _273;
                _314 = mad(_294, _301.z, _274);
                _315 = mad(_294, _301.y, _275);
                _316 = mad(_294, _301.x, _276);
            }
            else
            {
                _313 = _273 + 1.0f;
                _314 = _176 + _274;
                _315 = _175 + _275;
                _316 = _174 + _276;
            }
            float2 _326 = float2(mad(_179, cb0_m[3u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[3u].y / resolutionScale, TEXCOORD.y));
            float4 _328 = t1.SampleLevel(s1, _326, 0.0f);
            float _334 = clamp((_212 * max(_328.x, _328.y)) - (_179 * cb0_m[3u].z / resolutionScale), 0.0f, 1.0f);
            float _353;
            float _354;
            float _355;
            float _356;
            if (_334 > 0.0f)
            {
                float4 _341 = t0.SampleLevel(s0, _326, 0.0f);
                _353 = _334 + _313;
                _354 = mad(_334, _341.z, _314);
                _355 = mad(_334, _341.y, _315);
                _356 = mad(_334, _341.x, _316);
            }
            else
            {
                _353 = _313 + 1.0f;
                _354 = _176 + _314;
                _355 = _175 + _315;
                _356 = _174 + _316;
            }
            float2 _366 = float2(mad(_179, cb0_m[4u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[4u].y / resolutionScale, TEXCOORD.y));
            float4 _368 = t1.SampleLevel(s1, _366, 0.0f);
            float _374 = clamp((_212 * max(_368.x, _368.y)) - (_179 * cb0_m[4u].z / resolutionScale), 0.0f, 1.0f);
            float _393;
            float _394;
            float _395;
            float _396;
            if (_374 > 0.0f)
            {
                float4 _381 = t0.SampleLevel(s0, _366, 0.0f);
                _393 = _374 + _353;
                _394 = mad(_374, _381.z, _354);
                _395 = mad(_374, _381.y, _355);
                _396 = mad(_374, _381.x, _356);
            }
            else
            {
                _393 = _353 + 1.0f;
                _394 = _176 + _354;
                _395 = _175 + _355;
                _396 = _174 + _356;
            }
            float2 _406 = float2(mad(_179, cb0_m[5u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[5u].y / resolutionScale, TEXCOORD.y));
            float4 _408 = t1.SampleLevel(s1, _406, 0.0f);
            float _414 = clamp((_212 * max(_408.x, _408.y)) - (_179 * cb0_m[5u].z / resolutionScale), 0.0f, 1.0f);
            float _433;
            float _434;
            float _435;
            float _436;
            if (_414 > 0.0f)
            {
                float4 _421 = t0.SampleLevel(s0, _406, 0.0f);
                _433 = _414 + _393;
                _434 = mad(_414, _421.z, _394);
                _435 = mad(_414, _421.y, _395);
                _436 = mad(_414, _421.x, _396);
            }
            else
            {
                _433 = _393 + 1.0f;
                _434 = _176 + _394;
                _435 = _175 + _395;
                _436 = _174 + _396;
            }
            float2 _446 = float2(mad(_179, cb0_m[6u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[6u].y / resolutionScale, TEXCOORD.y));
            float4 _448 = t1.SampleLevel(s1, _446, 0.0f);
            float _454 = clamp((_212 * max(_448.x, _448.y)) - (_179 * cb0_m[6u].z / resolutionScale), 0.0f, 1.0f);
            float _473;
            float _474;
            float _475;
            float _476;
            if (_454 > 0.0f)
            {
                float4 _461 = t0.SampleLevel(s0, _446, 0.0f);
                _473 = _454 + _433;
                _474 = mad(_454, _461.z, _434);
                _475 = mad(_454, _461.y, _435);
                _476 = mad(_454, _461.x, _436);
            }
            else
            {
                _473 = _433 + 1.0f;
                _474 = _176 + _434;
                _475 = _175 + _435;
                _476 = _174 + _436;
            }
            float2 _486 = float2(mad(_179, cb0_m[7u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[7u].y / resolutionScale, TEXCOORD.y));
            float4 _488 = t1.SampleLevel(s1, _486, 0.0f);
            float _494 = clamp((_212 * max(_488.x, _488.y)) - (_179 * cb0_m[7u].z / resolutionScale), 0.0f, 1.0f);
            float _513;
            float _514;
            float _515;
            float _516;
            if (_494 > 0.0f)
            {
                float4 _501 = t0.SampleLevel(s0, _486, 0.0f);
                _513 = _494 + _473;
                _514 = mad(_494, _501.z, _474);
                _515 = mad(_494, _501.y, _475);
                _516 = mad(_494, _501.x, _476);
            }
            else
            {
                _513 = _473 + 1.0f;
                _514 = _176 + _474;
                _515 = _175 + _475;
                _516 = _174 + _476;
            }
            float2 _526 = float2(mad(_179, cb0_m[8u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[8u].y / resolutionScale, TEXCOORD.y));
            float4 _528 = t1.SampleLevel(s1, _526, 0.0f);
            float _534 = clamp((_212 * max(_528.x, _528.y)) - (_179 * cb0_m[8u].z / resolutionScale), 0.0f, 1.0f);
            float _553;
            float _554;
            float _555;
            float _556;
            if (_534 > 0.0f)
            {
                float4 _541 = t0.SampleLevel(s0, _526, 0.0f);
                _553 = _534 + _513;
                _554 = mad(_534, _541.z, _514);
                _555 = mad(_534, _541.y, _515);
                _556 = mad(_534, _541.x, _516);
            }
            else
            {
                _553 = _513 + 1.0f;
                _554 = _176 + _514;
                _555 = _175 + _515;
                _556 = _174 + _516;
            }
            float2 _566 = float2(mad(_179, cb0_m[9u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[9u].y / resolutionScale, TEXCOORD.y));
            float4 _568 = t1.SampleLevel(s1, _566, 0.0f);
            float _574 = clamp((_212 * max(_568.x, _568.y)) - (_179 * cb0_m[9u].z / resolutionScale), 0.0f, 1.0f);
            float _593;
            float _594;
            float _595;
            float _596;
            if (_574 > 0.0f)
            {
                float4 _581 = t0.SampleLevel(s0, _566, 0.0f);
                _593 = _574 + _553;
                _594 = mad(_574, _581.z, _554);
                _595 = mad(_574, _581.y, _555);
                _596 = mad(_574, _581.x, _556);
            }
            else
            {
                _593 = _553 + 1.0f;
                _594 = _176 + _554;
                _595 = _175 + _555;
                _596 = _174 + _556;
            }
            float2 _606 = float2(mad(_179, cb0_m[10u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[10u].y / resolutionScale, TEXCOORD.y));
            float4 _608 = t1.SampleLevel(s1, _606, 0.0f);
            float _614 = clamp((_212 * max(_608.x, _608.y)) - (_179 * cb0_m[10u].z / resolutionScale), 0.0f, 1.0f);
            float _633;
            float _634;
            float _635;
            float _636;
            if (_614 > 0.0f)
            {
                float4 _621 = t0.SampleLevel(s0, _606, 0.0f);
                _633 = _614 + _593;
                _634 = mad(_614, _621.z, _594);
                _635 = mad(_614, _621.y, _595);
                _636 = mad(_614, _621.x, _596);
            }
            else
            {
                _633 = _593 + 1.0f;
                _634 = _176 + _594;
                _635 = _175 + _595;
                _636 = _174 + _596;
            }
            float2 _646 = float2(mad(_179, cb0_m[11u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[11u].y / resolutionScale, TEXCOORD.y));
            float4 _648 = t1.SampleLevel(s1, _646, 0.0f);
            float _654 = clamp((_212 * max(_648.x, _648.y)) - (_179 * cb0_m[11u].z / resolutionScale), 0.0f, 1.0f);
            float _673;
            float _674;
            float _675;
            float _676;
            if (_654 > 0.0f)
            {
                float4 _661 = t0.SampleLevel(s0, _646, 0.0f);
                _673 = _654 + _633;
                _674 = mad(_654, _661.z, _634);
                _675 = mad(_654, _661.y, _635);
                _676 = mad(_654, _661.x, _636);
            }
            else
            {
                _673 = _633 + 1.0f;
                _674 = _176 + _634;
                _675 = _175 + _635;
                _676 = _174 + _636;
            }
            float2 _686 = float2(mad(_179, cb0_m[12u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[12u].y / resolutionScale, TEXCOORD.y));
            float4 _688 = t1.SampleLevel(s1, _686, 0.0f);
            float _694 = clamp((_212 * max(_688.x, _688.y)) - (_179 * cb0_m[12u].z / resolutionScale), 0.0f, 1.0f);
            float _713;
            float _714;
            float _715;
            float _716;
            if (_694 > 0.0f)
            {
                float4 _701 = t0.SampleLevel(s0, _686, 0.0f);
                _713 = _694 + _673;
                _714 = mad(_694, _701.z, _674);
                _715 = mad(_694, _701.y, _675);
                _716 = mad(_694, _701.x, _676);
            }
            else
            {
                _713 = _673 + 1.0f;
                _714 = _176 + _674;
                _715 = _175 + _675;
                _716 = _174 + _676;
            }
            float2 _726 = float2(mad(_179, cb0_m[13u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[13u].y / resolutionScale, TEXCOORD.y));
            float4 _728 = t1.SampleLevel(s1, _726, 0.0f);
            float _734 = clamp((_212 * max(_728.x, _728.y)) - (_179 * cb0_m[13u].z / resolutionScale), 0.0f, 1.0f);
            float _753;
            float _754;
            float _755;
            float _756;
            if (_734 > 0.0f)
            {
                float4 _741 = t0.SampleLevel(s0, _726, 0.0f);
                _753 = _734 + _713;
                _754 = mad(_734, _741.z, _714);
                _755 = mad(_734, _741.y, _715);
                _756 = mad(_734, _741.x, _716);
            }
            else
            {
                _753 = _713 + 1.0f;
                _754 = _176 + _714;
                _755 = _175 + _715;
                _756 = _174 + _716;
            }
            float2 _766 = float2(mad(_179, cb0_m[14u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[14u].y / resolutionScale, TEXCOORD.y));
            float4 _768 = t1.SampleLevel(s1, _766, 0.0f);
            float _774 = clamp((_212 * max(_768.x, _768.y)) - (_179 * cb0_m[14u].z / resolutionScale), 0.0f, 1.0f);
            float _793;
            float _794;
            float _795;
            float _796;
            if (_774 > 0.0f)
            {
                float4 _781 = t0.SampleLevel(s0, _766, 0.0f);
                _793 = _774 + _753;
                _794 = mad(_774, _781.z, _754);
                _795 = mad(_774, _781.y, _755);
                _796 = mad(_774, _781.x, _756);
            }
            else
            {
                _793 = _753 + 1.0f;
                _794 = _176 + _754;
                _795 = _175 + _755;
                _796 = _174 + _756;
            }
            float2 _806 = float2(mad(_179, cb0_m[15u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[15u].y / resolutionScale, TEXCOORD.y));
            float4 _808 = t1.SampleLevel(s1, _806, 0.0f);
            float _814 = clamp((_212 * max(_808.x, _808.y)) - (_179 * cb0_m[15u].z / resolutionScale), 0.0f, 1.0f);
            float _833;
            float _834;
            float _835;
            float _836;
            if (_814 > 0.0f)
            {
                float4 _821 = t0.SampleLevel(s0, _806, 0.0f);
                _833 = _814 + _793;
                _834 = mad(_814, _821.z, _794);
                _835 = mad(_814, _821.y, _795);
                _836 = mad(_814, _821.x, _796);
            }
            else
            {
                _833 = _793 + 1.0f;
                _834 = _176 + _794;
                _835 = _175 + _795;
                _836 = _174 + _796;
            }
            float2 _846 = float2(mad(_179, cb0_m[16u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[16u].y / resolutionScale, TEXCOORD.y));
            float4 _848 = t1.SampleLevel(s1, _846, 0.0f);
            float _854 = clamp((_212 * max(_848.x, _848.y)) - (_179 * cb0_m[16u].z / resolutionScale), 0.0f, 1.0f);
            float _873;
            float _874;
            float _875;
            float _876;
            if (_854 > 0.0f)
            {
                float4 _861 = t0.SampleLevel(s0, _846, 0.0f);
                _873 = _854 + _833;
                _874 = mad(_854, _861.z, _834);
                _875 = mad(_854, _861.y, _835);
                _876 = mad(_854, _861.x, _836);
            }
            else
            {
                _873 = _833 + 1.0f;
                _874 = _176 + _834;
                _875 = _175 + _835;
                _876 = _174 + _836;
            }
            float2 _886 = float2(mad(_179, cb0_m[17u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[17u].y / resolutionScale, TEXCOORD.y));
            float4 _888 = t1.SampleLevel(s1, _886, 0.0f);
            float _894 = clamp((_212 * max(_888.x, _888.y)) - (_179 * cb0_m[17u].z / resolutionScale), 0.0f, 1.0f);
            float _913;
            float _914;
            float _915;
            float _916;
            if (_894 > 0.0f)
            {
                float4 _901 = t0.SampleLevel(s0, _886, 0.0f);
                _913 = _894 + _873;
                _914 = mad(_894, _901.z, _874);
                _915 = mad(_894, _901.y, _875);
                _916 = mad(_894, _901.x, _876);
            }
            else
            {
                _913 = _873 + 1.0f;
                _914 = _176 + _874;
                _915 = _175 + _875;
                _916 = _174 + _876;
            }
            float2 _926 = float2(mad(_179, cb0_m[18u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[18u].y / resolutionScale, TEXCOORD.y));
            float4 _928 = t1.SampleLevel(s1, _926, 0.0f);
            float _934 = clamp((_212 * max(_928.x, _928.y)) - (_179 * cb0_m[18u].z / resolutionScale), 0.0f, 1.0f);
            float _953;
            float _954;
            float _955;
            float _956;
            if (_934 > 0.0f)
            {
                float4 _941 = t0.SampleLevel(s0, _926, 0.0f);
                _953 = _934 + _913;
                _954 = mad(_934, _941.z, _914);
                _955 = mad(_934, _941.y, _915);
                _956 = mad(_934, _941.x, _916);
            }
            else
            {
                _953 = _913 + 1.0f;
                _954 = _176 + _914;
                _955 = _175 + _915;
                _956 = _174 + _916;
            }
            float2 _966 = float2(mad(_179, cb0_m[19u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[19u].y / resolutionScale, TEXCOORD.y));
            float4 _968 = t1.SampleLevel(s1, _966, 0.0f);
            float _974 = clamp((_212 * max(_968.x, _968.y)) - (_179 * cb0_m[19u].z / resolutionScale), 0.0f, 1.0f);
            float _993;
            float _994;
            float _995;
            float _996;
            if (_974 > 0.0f)
            {
                float4 _981 = t0.SampleLevel(s0, _966, 0.0f);
                _993 = _974 + _953;
                _994 = mad(_974, _981.z, _954);
                _995 = mad(_974, _981.y, _955);
                _996 = mad(_974, _981.x, _956);
            }
            else
            {
                _993 = _953 + 1.0f;
                _994 = _176 + _954;
                _995 = _175 + _955;
                _996 = _174 + _956;
            }
            float2 _1006 = float2(mad(_179, cb0_m[20u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[20u].y / resolutionScale, TEXCOORD.y));
            float4 _1008 = t1.SampleLevel(s1, _1006, 0.0f);
            float _1014 = clamp((_212 * max(_1008.x, _1008.y)) - (_179 * cb0_m[20u].z / resolutionScale), 0.0f, 1.0f);
            float _1033;
            float _1034;
            float _1035;
            float _1036;
            if (_1014 > 0.0f)
            {
                float4 _1021 = t0.SampleLevel(s0, _1006, 0.0f);
                _1033 = _1014 + _993;
                _1034 = mad(_1014, _1021.z, _994);
                _1035 = mad(_1014, _1021.y, _995);
                _1036 = mad(_1014, _1021.x, _996);
            }
            else
            {
                _1033 = _993 + 1.0f;
                _1034 = _176 + _994;
                _1035 = _175 + _995;
                _1036 = _174 + _996;
            }
            float2 _1046 = float2(mad(_179, cb0_m[21u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[21u].y / resolutionScale, TEXCOORD.y));
            float4 _1048 = t1.SampleLevel(s1, _1046, 0.0f);
            float _1054 = clamp((_212 * max(_1048.x, _1048.y)) - (_179 * cb0_m[21u].z / resolutionScale), 0.0f, 1.0f);
            float _1073;
            float _1074;
            float _1075;
            float _1076;
            if (_1054 > 0.0f)
            {
                float4 _1061 = t0.SampleLevel(s0, _1046, 0.0f);
                _1073 = _1054 + _1033;
                _1074 = mad(_1054, _1061.z, _1034);
                _1075 = mad(_1054, _1061.y, _1035);
                _1076 = mad(_1054, _1061.x, _1036);
            }
            else
            {
                _1073 = _1033 + 1.0f;
                _1074 = _176 + _1034;
                _1075 = _175 + _1035;
                _1076 = _174 + _1036;
            }
            float2 _1086 = float2(mad(_179, cb0_m[22u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[22u].y / resolutionScale, TEXCOORD.y));
            float4 _1088 = t1.SampleLevel(s1, _1086, 0.0f);
            float _1094 = clamp((_212 * max(_1088.x, _1088.y)) - (_179 * cb0_m[22u].z / resolutionScale), 0.0f, 1.0f);
            float _1113;
            float _1114;
            float _1115;
            float _1116;
            if (_1094 > 0.0f)
            {
                float4 _1101 = t0.SampleLevel(s0, _1086, 0.0f);
                _1113 = _1094 + _1073;
                _1114 = mad(_1094, _1101.z, _1074);
                _1115 = mad(_1094, _1101.y, _1075);
                _1116 = mad(_1094, _1101.x, _1076);
            }
            else
            {
                _1113 = _1073 + 1.0f;
                _1114 = _176 + _1074;
                _1115 = _175 + _1075;
                _1116 = _174 + _1076;
            }
            float2 _1126 = float2(mad(_179, cb0_m[23u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[23u].y / resolutionScale, TEXCOORD.y));
            float4 _1128 = t1.SampleLevel(s1, _1126, 0.0f);
            float _1134 = clamp((_212 * max(_1128.x, _1128.y)) - (_179 * cb0_m[23u].z / resolutionScale), 0.0f, 1.0f);
            float _1153;
            float _1154;
            float _1155;
            float _1156;
            if (_1134 > 0.0f)
            {
                float4 _1141 = t0.SampleLevel(s0, _1126, 0.0f);
                _1153 = _1134 + _1113;
                _1154 = mad(_1134, _1141.z, _1114);
                _1155 = mad(_1134, _1141.y, _1115);
                _1156 = mad(_1134, _1141.x, _1116);
            }
            else
            {
                _1153 = _1113 + 1.0f;
                _1154 = _176 + _1114;
                _1155 = _175 + _1115;
                _1156 = _174 + _1116;
            }
            float2 _1166 = float2(mad(_179, cb0_m[24u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[24u].y / resolutionScale, TEXCOORD.y));
            float4 _1168 = t1.SampleLevel(s1, _1166, 0.0f);
            float _1174 = clamp((_212 * max(_1168.x, _1168.y)) - (_179 * cb0_m[24u].z / resolutionScale), 0.0f, 1.0f);
            float _1193;
            float _1194;
            float _1195;
            float _1196;
            if (_1174 > 0.0f)
            {
                float4 _1181 = t0.SampleLevel(s0, _1166, 0.0f);
                _1193 = _1174 + _1153;
                _1194 = mad(_1174, _1181.z, _1154);
                _1195 = mad(_1174, _1181.y, _1155);
                _1196 = mad(_1174, _1181.x, _1156);
            }
            else
            {
                _1193 = _1153 + 1.0f;
                _1194 = _176 + _1154;
                _1195 = _175 + _1155;
                _1196 = _174 + _1156;
            }
            float2 _1206 = float2(mad(_179, cb0_m[25u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[25u].y / resolutionScale, TEXCOORD.y));
            float4 _1208 = t1.SampleLevel(s1, _1206, 0.0f);
            float _1214 = clamp((_212 * max(_1208.x, _1208.y)) - (_179 * cb0_m[25u].z / resolutionScale), 0.0f, 1.0f);
            float _1233;
            float _1234;
            float _1235;
            float _1236;
            if (_1214 > 0.0f)
            {
                float4 _1221 = t0.SampleLevel(s0, _1206, 0.0f);
                _1233 = _1214 + _1193;
                _1234 = mad(_1214, _1221.z, _1194);
                _1235 = mad(_1214, _1221.y, _1195);
                _1236 = mad(_1214, _1221.x, _1196);
            }
            else
            {
                _1233 = _1193 + 1.0f;
                _1234 = _176 + _1194;
                _1235 = _175 + _1195;
                _1236 = _174 + _1196;
            }
            float2 _1246 = float2(mad(_179, cb0_m[26u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[26u].y / resolutionScale, TEXCOORD.y));
            float4 _1248 = t1.SampleLevel(s1, _1246, 0.0f);
            float _1254 = clamp((_212 * max(_1248.x, _1248.y)) - (_179 * cb0_m[26u].z / resolutionScale), 0.0f, 1.0f);
            float _1273;
            float _1274;
            float _1275;
            float _1276;
            if (_1254 > 0.0f)
            {
                float4 _1261 = t0.SampleLevel(s0, _1246, 0.0f);
                _1273 = _1254 + _1233;
                _1274 = mad(_1254, _1261.z, _1234);
                _1275 = mad(_1254, _1261.y, _1235);
                _1276 = mad(_1254, _1261.x, _1236);
            }
            else
            {
                _1273 = _1233 + 1.0f;
                _1274 = _176 + _1234;
                _1275 = _175 + _1235;
                _1276 = _174 + _1236;
            }
            float2 _1286 = float2(mad(_179, cb0_m[27u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[27u].y / resolutionScale, TEXCOORD.y));
            float4 _1288 = t1.SampleLevel(s1, _1286, 0.0f);
            float _1294 = clamp((_212 * max(_1288.x, _1288.y)) - (_179 * cb0_m[27u].z / resolutionScale), 0.0f, 1.0f);
            float _1313;
            float _1314;
            float _1315;
            float _1316;
            if (_1294 > 0.0f)
            {
                float4 _1301 = t0.SampleLevel(s0, _1286, 0.0f);
                _1313 = _1294 + _1273;
                _1314 = mad(_1294, _1301.z, _1274);
                _1315 = mad(_1294, _1301.y, _1275);
                _1316 = mad(_1294, _1301.x, _1276);
            }
            else
            {
                _1313 = _1273 + 1.0f;
                _1314 = _176 + _1274;
                _1315 = _175 + _1275;
                _1316 = _174 + _1276;
            }
            float2 _1326 = float2(mad(_179, cb0_m[28u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[28u].y / resolutionScale, TEXCOORD.y));
            float4 _1328 = t1.SampleLevel(s1, _1326, 0.0f);
            float _1334 = clamp((_212 * max(_1328.x, _1328.y)) - (_179 * cb0_m[28u].z / resolutionScale), 0.0f, 1.0f);
            float _1353;
            float _1354;
            float _1355;
            float _1356;
            if (_1334 > 0.0f)
            {
                float4 _1341 = t0.SampleLevel(s0, _1326, 0.0f);
                _1353 = _1334 + _1313;
                _1354 = mad(_1334, _1341.z, _1314);
                _1355 = mad(_1334, _1341.y, _1315);
                _1356 = mad(_1334, _1341.x, _1316);
            }
            else
            {
                _1353 = _1313 + 1.0f;
                _1354 = _176 + _1314;
                _1355 = _175 + _1315;
                _1356 = _174 + _1316;
            }
            float2 _1366 = float2(mad(_179, cb0_m[29u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[29u].y / resolutionScale, TEXCOORD.y));
            float4 _1368 = t1.SampleLevel(s1, _1366, 0.0f);
            float _1374 = clamp((_212 * max(_1368.x, _1368.y)) - (_179 * cb0_m[29u].z / resolutionScale), 0.0f, 1.0f);
            float _1393;
            float _1394;
            float _1395;
            float _1396;
            if (_1374 > 0.0f)
            {
                float4 _1381 = t0.SampleLevel(s0, _1366, 0.0f);
                _1393 = _1374 + _1353;
                _1394 = mad(_1374, _1381.z, _1354);
                _1395 = mad(_1374, _1381.y, _1355);
                _1396 = mad(_1374, _1381.x, _1356);
            }
            else
            {
                _1393 = _1353 + 1.0f;
                _1394 = _176 + _1354;
                _1395 = _175 + _1355;
                _1396 = _174 + _1356;
            }
            float2 _1406 = float2(mad(_179, cb0_m[30u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[30u].y / resolutionScale, TEXCOORD.y));
            float4 _1408 = t1.SampleLevel(s1, _1406, 0.0f);
            float _1414 = clamp((_212 * max(_1408.x, _1408.y)) - (_179 * cb0_m[30u].z / resolutionScale), 0.0f, 1.0f);
            float _1433;
            float _1434;
            float _1435;
            float _1436;
            if (_1414 > 0.0f)
            {
                float4 _1421 = t0.SampleLevel(s0, _1406, 0.0f);
                _1433 = _1414 + _1393;
                _1434 = mad(_1414, _1421.z, _1394);
                _1435 = mad(_1414, _1421.y, _1395);
                _1436 = mad(_1414, _1421.x, _1396);
            }
            else
            {
                _1433 = _1393 + 1.0f;
                _1434 = _176 + _1394;
                _1435 = _175 + _1395;
                _1436 = _174 + _1396;
            }
            float2 _1446 = float2(mad(_179, cb0_m[31u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[31u].y / resolutionScale, TEXCOORD.y));
            float4 _1448 = t1.SampleLevel(s1, _1446, 0.0f);
            float _1454 = clamp((_212 * max(_1448.x, _1448.y)) - (_179 * cb0_m[31u].z / resolutionScale), 0.0f, 1.0f);
            float _1473;
            float _1474;
            float _1475;
            float _1476;
            if (_1454 > 0.0f)
            {
                float4 _1461 = t0.SampleLevel(s0, _1446, 0.0f);
                _1473 = _1454 + _1433;
                _1474 = mad(_1454, _1461.z, _1434);
                _1475 = mad(_1454, _1461.y, _1435);
                _1476 = mad(_1454, _1461.x, _1436);
            }
            else
            {
                _1473 = _1433 + 1.0f;
                _1474 = _176 + _1434;
                _1475 = _175 + _1435;
                _1476 = _174 + _1436;
            }
            float2 _1486 = float2(mad(_179, cb0_m[32u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[32u].y / resolutionScale, TEXCOORD.y));
            float4 _1488 = t1.SampleLevel(s1, _1486, 0.0f);
            float _1494 = clamp((_212 * max(_1488.x, _1488.y)) - (_179 * cb0_m[32u].z / resolutionScale), 0.0f, 1.0f);
            float _1513;
            float _1514;
            float _1515;
            float _1516;
            if (_1494 > 0.0f)
            {
                float4 _1501 = t0.SampleLevel(s0, _1486, 0.0f);
                _1513 = _1494 + _1473;
                _1514 = mad(_1494, _1501.z, _1474);
                _1515 = mad(_1494, _1501.y, _1475);
                _1516 = mad(_1494, _1501.x, _1476);
            }
            else
            {
                _1513 = _1473 + 1.0f;
                _1514 = _176 + _1474;
                _1515 = _175 + _1475;
                _1516 = _174 + _1476;
            }
            float2 _1526 = float2(mad(_179, cb0_m[33u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[33u].y / resolutionScale, TEXCOORD.y));
            float4 _1528 = t1.SampleLevel(s1, _1526, 0.0f);
            float _1534 = clamp((_212 * max(_1528.x, _1528.y)) - (_179 * cb0_m[33u].z / resolutionScale), 0.0f, 1.0f);
            float _1553;
            float _1554;
            float _1555;
            float _1556;
            if (_1534 > 0.0f)
            {
                float4 _1541 = t0.SampleLevel(s0, _1526, 0.0f);
                _1553 = _1534 + _1513;
                _1554 = mad(_1534, _1541.z, _1514);
                _1555 = mad(_1534, _1541.y, _1515);
                _1556 = mad(_1534, _1541.x, _1516);
            }
            else
            {
                _1553 = _1513 + 1.0f;
                _1554 = _176 + _1514;
                _1555 = _175 + _1515;
                _1556 = _174 + _1516;
            }
            float2 _1566 = float2(mad(_179, cb0_m[34u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[34u].y / resolutionScale, TEXCOORD.y));
            float4 _1568 = t1.SampleLevel(s1, _1566, 0.0f);
            float _1574 = clamp((_212 * max(_1568.x, _1568.y)) - (_179 * cb0_m[34u].z / resolutionScale), 0.0f, 1.0f);
            float _1593;
            float _1594;
            float _1595;
            float _1596;
            if (_1574 > 0.0f)
            {
                float4 _1581 = t0.SampleLevel(s0, _1566, 0.0f);
                _1593 = _1574 + _1553;
                _1594 = mad(_1574, _1581.z, _1554);
                _1595 = mad(_1574, _1581.y, _1555);
                _1596 = mad(_1574, _1581.x, _1556);
            }
            else
            {
                _1593 = _1553 + 1.0f;
                _1594 = _176 + _1554;
                _1595 = _175 + _1555;
                _1596 = _174 + _1556;
            }
            float2 _1606 = float2(mad(_179, cb0_m[35u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[35u].y / resolutionScale, TEXCOORD.y));
            float4 _1608 = t1.SampleLevel(s1, _1606, 0.0f);
            float _1614 = clamp((_212 * max(_1608.x, _1608.y)) - (_179 * cb0_m[35u].z / resolutionScale), 0.0f, 1.0f);
            float _1633;
            float _1634;
            float _1635;
            float _1636;
            if (_1614 > 0.0f)
            {
                float4 _1621 = t0.SampleLevel(s0, _1606, 0.0f);
                _1633 = _1614 + _1593;
                _1634 = mad(_1614, _1621.z, _1594);
                _1635 = mad(_1614, _1621.y, _1595);
                _1636 = mad(_1614, _1621.x, _1596);
            }
            else
            {
                _1633 = _1593 + 1.0f;
                _1634 = _176 + _1594;
                _1635 = _175 + _1595;
                _1636 = _174 + _1596;
            }
            float2 _1646 = float2(mad(_179, cb0_m[36u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[36u].y / resolutionScale, TEXCOORD.y));
            float4 _1648 = t1.SampleLevel(s1, _1646, 0.0f);
            float _1654 = clamp((_212 * max(_1648.x, _1648.y)) - (_179 * cb0_m[36u].z / resolutionScale), 0.0f, 1.0f);
            float _1673;
            float _1674;
            float _1675;
            float _1676;
            if (_1654 > 0.0f)
            {
                float4 _1661 = t0.SampleLevel(s0, _1646, 0.0f);
                _1673 = _1654 + _1633;
                _1674 = mad(_1654, _1661.z, _1634);
                _1675 = mad(_1654, _1661.y, _1635);
                _1676 = mad(_1654, _1661.x, _1636);
            }
            else
            {
                _1673 = _1633 + 1.0f;
                _1674 = _176 + _1634;
                _1675 = _175 + _1635;
                _1676 = _174 + _1636;
            }
            float2 _1686 = float2(mad(_179, cb0_m[37u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[37u].y / resolutionScale, TEXCOORD.y));
            float4 _1688 = t1.SampleLevel(s1, _1686, 0.0f);
            float _1694 = clamp((_212 * max(_1688.x, _1688.y)) - (_179 * cb0_m[37u].z / resolutionScale), 0.0f, 1.0f);
            float _1713;
            float _1714;
            float _1715;
            float _1716;
            if (_1694 > 0.0f)
            {
                float4 _1701 = t0.SampleLevel(s0, _1686, 0.0f);
                _1713 = _1694 + _1673;
                _1714 = mad(_1694, _1701.z, _1674);
                _1715 = mad(_1694, _1701.y, _1675);
                _1716 = mad(_1694, _1701.x, _1676);
            }
            else
            {
                _1713 = _1673 + 1.0f;
                _1714 = _176 + _1674;
                _1715 = _175 + _1675;
                _1716 = _174 + _1676;
            }
            float2 _1726 = float2(mad(_179, cb0_m[38u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[38u].y / resolutionScale, TEXCOORD.y));
            float4 _1728 = t1.SampleLevel(s1, _1726, 0.0f);
            float _1734 = clamp((_212 * max(_1728.x, _1728.y)) - (_179 * cb0_m[38u].z / resolutionScale), 0.0f, 1.0f);
            float _1753;
            float _1754;
            float _1755;
            float _1756;
            if (_1734 > 0.0f)
            {
                float4 _1741 = t0.SampleLevel(s0, _1726, 0.0f);
                _1753 = _1734 + _1713;
                _1754 = mad(_1734, _1741.z, _1714);
                _1755 = mad(_1734, _1741.y, _1715);
                _1756 = mad(_1734, _1741.x, _1716);
            }
            else
            {
                _1753 = _1713 + 1.0f;
                _1754 = _176 + _1714;
                _1755 = _175 + _1715;
                _1756 = _174 + _1716;
            }
            float2 _1766 = float2(mad(_179, cb0_m[39u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[39u].y / resolutionScale, TEXCOORD.y));
            float4 _1768 = t1.SampleLevel(s1, _1766, 0.0f);
            float _1774 = clamp((_212 * max(_1768.x, _1768.y)) - (_179 * cb0_m[39u].z / resolutionScale), 0.0f, 1.0f);
            float _1793;
            float _1794;
            float _1795;
            float _1796;
            if (_1774 > 0.0f)
            {
                float4 _1781 = t0.SampleLevel(s0, _1766, 0.0f);
                _1793 = _1774 + _1753;
                _1794 = mad(_1774, _1781.z, _1754);
                _1795 = mad(_1774, _1781.y, _1755);
                _1796 = mad(_1774, _1781.x, _1756);
            }
            else
            {
                _1793 = _1753 + 1.0f;
                _1794 = _176 + _1754;
                _1795 = _175 + _1755;
                _1796 = _174 + _1756;
            }
            float2 _1806 = float2(mad(_179, cb0_m[40u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[40u].y / resolutionScale, TEXCOORD.y));
            float4 _1808 = t1.SampleLevel(s1, _1806, 0.0f);
            float _1814 = clamp((_212 * max(_1808.x, _1808.y)) - (_179 * cb0_m[40u].z / resolutionScale), 0.0f, 1.0f);
            float _1833;
            float _1834;
            float _1835;
            float _1836;
            if (_1814 > 0.0f)
            {
                float4 _1821 = t0.SampleLevel(s0, _1806, 0.0f);
                _1833 = _1814 + _1793;
                _1834 = mad(_1814, _1821.z, _1794);
                _1835 = mad(_1814, _1821.y, _1795);
                _1836 = mad(_1814, _1821.x, _1796);
            }
            else
            {
                _1833 = _1793 + 1.0f;
                _1834 = _176 + _1794;
                _1835 = _175 + _1795;
                _1836 = _174 + _1796;
            }
            float2 _1846 = float2(mad(_179, cb0_m[41u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[41u].y / resolutionScale, TEXCOORD.y));
            float4 _1848 = t1.SampleLevel(s1, _1846, 0.0f);
            float _1854 = clamp((_212 * max(_1848.x, _1848.y)) - (_179 * cb0_m[41u].z / resolutionScale), 0.0f, 1.0f);
            float _1873;
            float _1874;
            float _1875;
            float _1876;
            if (_1854 > 0.0f)
            {
                float4 _1861 = t0.SampleLevel(s0, _1846, 0.0f);
                _1873 = _1854 + _1833;
                _1874 = mad(_1854, _1861.z, _1834);
                _1875 = mad(_1854, _1861.y, _1835);
                _1876 = mad(_1854, _1861.x, _1836);
            }
            else
            {
                _1873 = _1833 + 1.0f;
                _1874 = _176 + _1834;
                _1875 = _175 + _1835;
                _1876 = _174 + _1836;
            }
            float2 _1886 = float2(mad(_179, cb0_m[42u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[42u].y / resolutionScale, TEXCOORD.y));
            float4 _1888 = t1.SampleLevel(s1, _1886, 0.0f);
            float _1894 = clamp((_212 * max(_1888.x, _1888.y)) - (_179 * cb0_m[42u].z / resolutionScale), 0.0f, 1.0f);
            float _1913;
            float _1914;
            float _1915;
            float _1916;
            if (_1894 > 0.0f)
            {
                float4 _1901 = t0.SampleLevel(s0, _1886, 0.0f);
                _1913 = _1894 + _1873;
                _1914 = mad(_1894, _1901.z, _1874);
                _1915 = mad(_1894, _1901.y, _1875);
                _1916 = mad(_1894, _1901.x, _1876);
            }
            else
            {
                _1913 = _1873 + 1.0f;
                _1914 = _176 + _1874;
                _1915 = _175 + _1875;
                _1916 = _174 + _1876;
            }
            float2 _1926 = float2(mad(_179, cb0_m[43u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[43u].y / resolutionScale, TEXCOORD.y));
            float4 _1928 = t1.SampleLevel(s1, _1926, 0.0f);
            float _1934 = clamp((_212 * max(_1928.x, _1928.y)) - (_179 * cb0_m[43u].z / resolutionScale), 0.0f, 1.0f);
            float _1953;
            float _1954;
            float _1955;
            float _1956;
            if (_1934 > 0.0f)
            {
                float4 _1941 = t0.SampleLevel(s0, _1926, 0.0f);
                _1953 = _1934 + _1913;
                _1954 = mad(_1934, _1941.z, _1914);
                _1955 = mad(_1934, _1941.y, _1915);
                _1956 = mad(_1934, _1941.x, _1916);
            }
            else
            {
                _1953 = _1913 + 1.0f;
                _1954 = _176 + _1914;
                _1955 = _175 + _1915;
                _1956 = _174 + _1916;
            }
            float2 _1966 = float2(mad(_179, cb0_m[44u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[44u].y / resolutionScale, TEXCOORD.y));
            float4 _1968 = t1.SampleLevel(s1, _1966, 0.0f);
            float _1974 = clamp((_212 * max(_1968.x, _1968.y)) - (_179 * cb0_m[44u].z / resolutionScale), 0.0f, 1.0f);
            float _1993;
            float _1994;
            float _1995;
            float _1996;
            if (_1974 > 0.0f)
            {
                float4 _1981 = t0.SampleLevel(s0, _1966, 0.0f);
                _1993 = _1974 + _1953;
                _1994 = mad(_1974, _1981.z, _1954);
                _1995 = mad(_1974, _1981.y, _1955);
                _1996 = mad(_1974, _1981.x, _1956);
            }
            else
            {
                _1993 = _1953 + 1.0f;
                _1994 = _176 + _1954;
                _1995 = _175 + _1955;
                _1996 = _174 + _1956;
            }
            float2 _2006 = float2(mad(_179, cb0_m[45u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[45u].y / resolutionScale, TEXCOORD.y));
            float4 _2008 = t1.SampleLevel(s1, _2006, 0.0f);
            float _2014 = clamp((_212 * max(_2008.x, _2008.y)) - (_179 * cb0_m[45u].z / resolutionScale), 0.0f, 1.0f);
            float _2033;
            float _2034;
            float _2035;
            float _2036;
            if (_2014 > 0.0f)
            {
                float4 _2021 = t0.SampleLevel(s0, _2006, 0.0f);
                _2033 = _2014 + _1993;
                _2034 = mad(_2014, _2021.z, _1994);
                _2035 = mad(_2014, _2021.y, _1995);
                _2036 = mad(_2014, _2021.x, _1996);
            }
            else
            {
                _2033 = _1993 + 1.0f;
                _2034 = _176 + _1994;
                _2035 = _175 + _1995;
                _2036 = _174 + _1996;
            }
            float2 _2046 = float2(mad(_179, cb0_m[46u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[46u].y / resolutionScale, TEXCOORD.y));
            float4 _2048 = t1.SampleLevel(s1, _2046, 0.0f);
            float _2054 = clamp((_212 * max(_2048.x, _2048.y)) - (_179 * cb0_m[46u].z / resolutionScale), 0.0f, 1.0f);
            float _2073;
            float _2074;
            float _2075;
            float _2076;
            if (_2054 > 0.0f)
            {
                float4 _2061 = t0.SampleLevel(s0, _2046, 0.0f);
                _2073 = _2054 + _2033;
                _2074 = mad(_2054, _2061.z, _2034);
                _2075 = mad(_2054, _2061.y, _2035);
                _2076 = mad(_2054, _2061.x, _2036);
            }
            else
            {
                _2073 = _2033 + 1.0f;
                _2074 = _176 + _2034;
                _2075 = _175 + _2035;
                _2076 = _174 + _2036;
            }
            float2 _2086 = float2(mad(_179, cb0_m[47u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[47u].y / resolutionScale, TEXCOORD.y));
            float4 _2088 = t1.SampleLevel(s1, _2086, 0.0f);
            float _2094 = clamp((_212 * max(_2088.x, _2088.y)) - (_179 * cb0_m[47u].z / resolutionScale), 0.0f, 1.0f);
            float _2113;
            float _2114;
            float _2115;
            float _2116;
            if (_2094 > 0.0f)
            {
                float4 _2101 = t0.SampleLevel(s0, _2086, 0.0f);
                _2113 = _2094 + _2073;
                _2114 = mad(_2094, _2101.z, _2074);
                _2115 = mad(_2094, _2101.y, _2075);
                _2116 = mad(_2094, _2101.x, _2076);
            }
            else
            {
                _2113 = _2073 + 1.0f;
                _2114 = _176 + _2074;
                _2115 = _175 + _2075;
                _2116 = _174 + _2076;
            }
            _5878 = _2114 / _2113;
            _5879 = _2115 / _2113;
            _5880 = _2116 / _2113;
        }
        else
        {
            float _5875;
            float _5876;
            float _5877;
            if (_183 >= 2.0f)
            {
                float _5868;
                float _5869;
                float _5870;
                float _5871;
                if (_183 < 14.0f)
                {
                    float2 _2137 = float2(mad(_179, cb0_m[0u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[0u].y / resolutionScale, TEXCOORD.y));
                    float4 _2139 = t1.SampleLevel(s1, _2137, 0.0f);
                    float _2143 = cb1_m1.w * 0.5f;
                    float _2146 = clamp((max(_2139.x, _2139.y) * _2143) - (_179 * cb0_m[0u].z / resolutionScale), 0.0f, 1.0f);
                    float _2164;
                    float _2165;
                    float _2166;
                    float _2167;
                    float _2168;
                    float _2169;
                    float _2170;
                    float _2171;
                    if (_2146 > 0.0f)
                    {
                        float4 _2153 = t0.SampleLevel(s0, _2137, 0.0f);
                        float _2154 = _2153.x;
                        float _2155 = _2153.y;
                        float _2156 = _2153.z;
                        _2164 = _2146;
                        _2165 = _2146 * _2156;
                        _2166 = _2146 * _2155;
                        _2167 = _2146 * _2154;
                        _2168 = _2146 + 1.0f;
                        _2169 = mad(_2146, _2156, _176);
                        _2170 = mad(_2146, _2155, _175);
                        _2171 = mad(_2146, _2154, _174);
                    }
                    else
                    {
                        _2164 = 0.0f;
                        _2165 = 0.0f;
                        _2166 = 0.0f;
                        _2167 = 0.0f;
                        _2168 = 1.0f;
                        _2169 = _176;
                        _2170 = _175;
                        _2171 = _174;
                    }
                    float2 _2181 = float2(mad(_179, cb0_m[1u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[1u].y / resolutionScale, TEXCOORD.y));
                    float4 _2183 = t1.SampleLevel(s1, _2181, 0.0f);
                    float _2189 = clamp((_2143 * max(_2183.x, _2183.y)) - (_179 * cb0_m[1u].z / resolutionScale), 0.0f, 1.0f);
                    float _2211;
                    float _2212;
                    float _2213;
                    float _2214;
                    float _2215;
                    float _2216;
                    float _2217;
                    float _2218;
                    if (_2189 > 0.0f)
                    {
                        float4 _2196 = t0.SampleLevel(s0, _2181, 0.0f);
                        float _2197 = _2196.x;
                        float _2198 = _2196.y;
                        float _2199 = _2196.z;
                        _2211 = _2189;
                        _2212 = _2189 * _2199;
                        _2213 = _2189 * _2198;
                        _2214 = _2189 * _2197;
                        _2215 = _2189 + _2168;
                        _2216 = mad(_2189, _2199, _2169);
                        _2217 = mad(_2189, _2198, _2170);
                        _2218 = mad(_2189, _2197, _2171);
                    }
                    else
                    {
                        _2211 = _2164;
                        _2212 = _2165;
                        _2213 = _2166;
                        _2214 = _2167;
                        _2215 = _2164 + _2168;
                        _2216 = _2165 + _2169;
                        _2217 = _2166 + _2170;
                        _2218 = _2167 + _2171;
                    }
                    float2 _2228 = float2(mad(_179, cb0_m[2u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[2u].y / resolutionScale, TEXCOORD.y));
                    float4 _2230 = t1.SampleLevel(s1, _2228, 0.0f);
                    float _2236 = clamp((_2143 * max(_2230.x, _2230.y)) - (_179 * cb0_m[2u].z / resolutionScale), 0.0f, 1.0f);
                    float _2258;
                    float _2259;
                    float _2260;
                    float _2261;
                    float _2262;
                    float _2263;
                    float _2264;
                    float _2265;
                    if (_2236 > 0.0f)
                    {
                        float4 _2243 = t0.SampleLevel(s0, _2228, 0.0f);
                        float _2244 = _2243.x;
                        float _2245 = _2243.y;
                        float _2246 = _2243.z;
                        _2258 = _2236;
                        _2259 = _2236 * _2246;
                        _2260 = _2236 * _2245;
                        _2261 = _2236 * _2244;
                        _2262 = _2236 + _2215;
                        _2263 = mad(_2236, _2246, _2216);
                        _2264 = mad(_2236, _2245, _2217);
                        _2265 = mad(_2236, _2244, _2218);
                    }
                    else
                    {
                        _2258 = _2211;
                        _2259 = _2212;
                        _2260 = _2213;
                        _2261 = _2214;
                        _2262 = _2211 + _2215;
                        _2263 = _2212 + _2216;
                        _2264 = _2213 + _2217;
                        _2265 = _2214 + _2218;
                    }
                    float2 _2275 = float2(mad(_179, cb0_m[3u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[3u].y / resolutionScale, TEXCOORD.y));
                    float4 _2277 = t1.SampleLevel(s1, _2275, 0.0f);
                    float _2283 = clamp((_2143 * max(_2277.x, _2277.y)) - (_179 * cb0_m[3u].z / resolutionScale), 0.0f, 1.0f);
                    float _2305;
                    float _2306;
                    float _2307;
                    float _2308;
                    float _2309;
                    float _2310;
                    float _2311;
                    float _2312;
                    if (_2283 > 0.0f)
                    {
                        float4 _2290 = t0.SampleLevel(s0, _2275, 0.0f);
                        float _2291 = _2290.x;
                        float _2292 = _2290.y;
                        float _2293 = _2290.z;
                        _2305 = _2283;
                        _2306 = _2283 * _2293;
                        _2307 = _2283 * _2292;
                        _2308 = _2283 * _2291;
                        _2309 = _2283 + _2262;
                        _2310 = mad(_2283, _2293, _2263);
                        _2311 = mad(_2283, _2292, _2264);
                        _2312 = mad(_2283, _2291, _2265);
                    }
                    else
                    {
                        _2305 = _2258;
                        _2306 = _2259;
                        _2307 = _2260;
                        _2308 = _2261;
                        _2309 = _2258 + _2262;
                        _2310 = _2259 + _2263;
                        _2311 = _2264 + _2260;
                        _2312 = _2261 + _2265;
                    }
                    float2 _2322 = float2(mad(_179, cb0_m[4u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[4u].y / resolutionScale, TEXCOORD.y));
                    float4 _2324 = t1.SampleLevel(s1, _2322, 0.0f);
                    float _2330 = clamp((_2143 * max(_2324.x, _2324.y)) - (_179 * cb0_m[4u].z / resolutionScale), 0.0f, 1.0f);
                    float _2352;
                    float _2353;
                    float _2354;
                    float _2355;
                    float _2356;
                    float _2357;
                    float _2358;
                    float _2359;
                    if (_2330 > 0.0f)
                    {
                        float4 _2337 = t0.SampleLevel(s0, _2322, 0.0f);
                        float _2338 = _2337.x;
                        float _2339 = _2337.y;
                        float _2340 = _2337.z;
                        _2352 = _2330;
                        _2353 = _2330 * _2340;
                        _2354 = _2330 * _2339;
                        _2355 = _2330 * _2338;
                        _2356 = _2330 + _2309;
                        _2357 = mad(_2330, _2340, _2310);
                        _2358 = mad(_2330, _2339, _2311);
                        _2359 = mad(_2330, _2338, _2312);
                    }
                    else
                    {
                        _2352 = _2305;
                        _2353 = _2306;
                        _2354 = _2307;
                        _2355 = _2308;
                        _2356 = _2305 + _2309;
                        _2357 = _2306 + _2310;
                        _2358 = _2307 + _2311;
                        _2359 = _2308 + _2312;
                    }
                    float2 _2369 = float2(mad(_179, cb0_m[5u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[5u].y / resolutionScale, TEXCOORD.y));
                    float4 _2371 = t1.SampleLevel(s1, _2369, 0.0f);
                    float _2377 = clamp((_2143 * max(_2371.x, _2371.y)) - (_179 * cb0_m[5u].z / resolutionScale), 0.0f, 1.0f);
                    float _2399;
                    float _2400;
                    float _2401;
                    float _2402;
                    float _2403;
                    float _2404;
                    float _2405;
                    float _2406;
                    if (_2377 > 0.0f)
                    {
                        float4 _2384 = t0.SampleLevel(s0, _2369, 0.0f);
                        float _2385 = _2384.x;
                        float _2386 = _2384.y;
                        float _2387 = _2384.z;
                        _2399 = _2377;
                        _2400 = _2377 * _2387;
                        _2401 = _2377 * _2386;
                        _2402 = _2377 * _2385;
                        _2403 = _2377 + _2356;
                        _2404 = mad(_2377, _2387, _2357);
                        _2405 = mad(_2377, _2386, _2358);
                        _2406 = mad(_2377, _2385, _2359);
                    }
                    else
                    {
                        _2399 = _2352;
                        _2400 = _2353;
                        _2401 = _2354;
                        _2402 = _2355;
                        _2403 = _2352 + _2356;
                        _2404 = _2353 + _2357;
                        _2405 = _2354 + _2358;
                        _2406 = _2355 + _2359;
                    }
                    float2 _2416 = float2(mad(_179, cb0_m[6u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[6u].y / resolutionScale, TEXCOORD.y));
                    float4 _2418 = t1.SampleLevel(s1, _2416, 0.0f);
                    float _2424 = clamp((_2143 * max(_2418.x, _2418.y)) - (_179 * cb0_m[6u].z / resolutionScale), 0.0f, 1.0f);
                    float _2446;
                    float _2447;
                    float _2448;
                    float _2449;
                    float _2450;
                    float _2451;
                    float _2452;
                    float _2453;
                    if (_2424 > 0.0f)
                    {
                        float4 _2431 = t0.SampleLevel(s0, _2416, 0.0f);
                        float _2432 = _2431.x;
                        float _2433 = _2431.y;
                        float _2434 = _2431.z;
                        _2446 = _2424;
                        _2447 = _2424 * _2434;
                        _2448 = _2424 * _2433;
                        _2449 = _2424 * _2432;
                        _2450 = _2424 + _2403;
                        _2451 = mad(_2424, _2434, _2404);
                        _2452 = mad(_2424, _2433, _2405);
                        _2453 = mad(_2424, _2432, _2406);
                    }
                    else
                    {
                        _2446 = _2399;
                        _2447 = _2400;
                        _2448 = _2401;
                        _2449 = _2402;
                        _2450 = _2399 + _2403;
                        _2451 = _2400 + _2404;
                        _2452 = _2401 + _2405;
                        _2453 = _2402 + _2406;
                    }
                    float2 _2463 = float2(mad(_179, cb0_m[7u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[7u].y / resolutionScale, TEXCOORD.y));
                    float4 _2465 = t1.SampleLevel(s1, _2463, 0.0f);
                    float _2471 = clamp((_2143 * max(_2465.x, _2465.y)) - (_179 * cb0_m[7u].z / resolutionScale), 0.0f, 1.0f);
                    float _2490;
                    float _2491;
                    float _2492;
                    float _2493;
                    if (_2471 > 0.0f)
                    {
                        float4 _2478 = t0.SampleLevel(s0, _2463, 0.0f);
                        _2490 = _2471 + _2450;
                        _2491 = mad(_2471, _2478.z, _2451);
                        _2492 = mad(_2471, _2478.y, _2452);
                        _2493 = mad(_2471, _2478.x, _2453);
                    }
                    else
                    {
                        _2490 = _2446 + _2450;
                        _2491 = _2451 + _2447;
                        _2492 = _2448 + _2452;
                        _2493 = _2449 + _2453;
                    }
                    _5868 = _2490;
                    _5869 = _2491;
                    _5870 = _2492;
                    _5871 = _2493;
                }
                else
                {
                    float _5864;
                    float _5865;
                    float _5866;
                    float _5867;
                    if (_183 < 21.0f)
                    {
                        float2 _2508 = float2(mad(_179, cb0_m[0u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[0u].y / resolutionScale, TEXCOORD.y));
                        float4 _2510 = t1.SampleLevel(s1, _2508, 0.0f);
                        float _2514 = cb1_m1.w * 0.5f;
                        float _2517 = clamp((max(_2510.x, _2510.y) * _2514) - (_179 * cb0_m[0u].z / resolutionScale), 0.0f, 1.0f);
                        float _2535;
                        float _2536;
                        float _2537;
                        float _2538;
                        float _2539;
                        float _2540;
                        float _2541;
                        float _2542;
                        if (_2517 > 0.0f)
                        {
                            float4 _2524 = t0.SampleLevel(s0, _2508, 0.0f);
                            float _2525 = _2524.x;
                            float _2526 = _2524.y;
                            float _2527 = _2524.z;
                            _2535 = _2517;
                            _2536 = _2517 * _2527;
                            _2537 = _2517 * _2526;
                            _2538 = _2517 * _2525;
                            _2539 = _2517 + 1.0f;
                            _2540 = mad(_2517, _2527, _176);
                            _2541 = mad(_2517, _2526, _175);
                            _2542 = mad(_2517, _2525, _174);
                        }
                        else
                        {
                            _2535 = 0.0f;
                            _2536 = 0.0f;
                            _2537 = 0.0f;
                            _2538 = 0.0f;
                            _2539 = 1.0f;
                            _2540 = _176;
                            _2541 = _175;
                            _2542 = _174;
                        }
                        float2 _2552 = float2(mad(_179, cb0_m[1u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[1u].y / resolutionScale, TEXCOORD.y));
                        float4 _2554 = t1.SampleLevel(s1, _2552, 0.0f);
                        float _2560 = clamp((_2514 * max(_2554.x, _2554.y)) - (_179 * cb0_m[1u].z / resolutionScale), 0.0f, 1.0f);
                        float _2582;
                        float _2583;
                        float _2584;
                        float _2585;
                        float _2586;
                        float _2587;
                        float _2588;
                        float _2589;
                        if (_2560 > 0.0f)
                        {
                            float4 _2567 = t0.SampleLevel(s0, _2552, 0.0f);
                            float _2568 = _2567.x;
                            float _2569 = _2567.y;
                            float _2570 = _2567.z;
                            _2582 = _2560;
                            _2583 = _2560 * _2570;
                            _2584 = _2560 * _2569;
                            _2585 = _2560 * _2568;
                            _2586 = _2560 + _2539;
                            _2587 = mad(_2560, _2570, _2540);
                            _2588 = mad(_2560, _2569, _2541);
                            _2589 = mad(_2560, _2568, _2542);
                        }
                        else
                        {
                            _2582 = _2535;
                            _2583 = _2536;
                            _2584 = _2537;
                            _2585 = _2538;
                            _2586 = _2535 + _2539;
                            _2587 = _2540 + _2536;
                            _2588 = _2537 + _2541;
                            _2589 = _2538 + _2542;
                        }
                        float2 _2599 = float2(mad(_179, cb0_m[2u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[2u].y / resolutionScale, TEXCOORD.y));
                        float4 _2601 = t1.SampleLevel(s1, _2599, 0.0f);
                        float _2607 = clamp((_2514 * max(_2601.x, _2601.y)) - (_179 * cb0_m[2u].z / resolutionScale), 0.0f, 1.0f);
                        float _2629;
                        float _2630;
                        float _2631;
                        float _2632;
                        float _2633;
                        float _2634;
                        float _2635;
                        float _2636;
                        if (_2607 > 0.0f)
                        {
                            float4 _2614 = t0.SampleLevel(s0, _2599, 0.0f);
                            float _2615 = _2614.x;
                            float _2616 = _2614.y;
                            float _2617 = _2614.z;
                            _2629 = _2607;
                            _2630 = _2607 * _2617;
                            _2631 = _2607 * _2616;
                            _2632 = _2607 * _2615;
                            _2633 = _2607 + _2586;
                            _2634 = mad(_2607, _2617, _2587);
                            _2635 = mad(_2607, _2616, _2588);
                            _2636 = mad(_2607, _2615, _2589);
                        }
                        else
                        {
                            _2629 = _2582;
                            _2630 = _2583;
                            _2631 = _2584;
                            _2632 = _2585;
                            _2633 = _2582 + _2586;
                            _2634 = _2583 + _2587;
                            _2635 = _2584 + _2588;
                            _2636 = _2585 + _2589;
                        }
                        float2 _2646 = float2(mad(_179, cb0_m[3u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[3u].y / resolutionScale, TEXCOORD.y));
                        float4 _2648 = t1.SampleLevel(s1, _2646, 0.0f);
                        float _2654 = clamp((_2514 * max(_2648.x, _2648.y)) - (_179 * cb0_m[3u].z / resolutionScale), 0.0f, 1.0f);
                        float _2676;
                        float _2677;
                        float _2678;
                        float _2679;
                        float _2680;
                        float _2681;
                        float _2682;
                        float _2683;
                        if (_2654 > 0.0f)
                        {
                            float4 _2661 = t0.SampleLevel(s0, _2646, 0.0f);
                            float _2662 = _2661.x;
                            float _2663 = _2661.y;
                            float _2664 = _2661.z;
                            _2676 = _2654;
                            _2677 = _2654 * _2664;
                            _2678 = _2654 * _2663;
                            _2679 = _2654 * _2662;
                            _2680 = _2654 + _2633;
                            _2681 = mad(_2654, _2664, _2634);
                            _2682 = mad(_2654, _2663, _2635);
                            _2683 = mad(_2654, _2662, _2636);
                        }
                        else
                        {
                            _2676 = _2629;
                            _2677 = _2630;
                            _2678 = _2631;
                            _2679 = _2632;
                            _2680 = _2629 + _2633;
                            _2681 = _2630 + _2634;
                            _2682 = _2631 + _2635;
                            _2683 = _2632 + _2636;
                        }
                        float2 _2693 = float2(mad(_179, cb0_m[4u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[4u].y / resolutionScale, TEXCOORD.y));
                        float4 _2695 = t1.SampleLevel(s1, _2693, 0.0f);
                        float _2701 = clamp((_2514 * max(_2695.x, _2695.y)) - (_179 * cb0_m[4u].z / resolutionScale), 0.0f, 1.0f);
                        float _2723;
                        float _2724;
                        float _2725;
                        float _2726;
                        float _2727;
                        float _2728;
                        float _2729;
                        float _2730;
                        if (_2701 > 0.0f)
                        {
                            float4 _2708 = t0.SampleLevel(s0, _2693, 0.0f);
                            float _2709 = _2708.x;
                            float _2710 = _2708.y;
                            float _2711 = _2708.z;
                            _2723 = _2701;
                            _2724 = _2701 * _2711;
                            _2725 = _2701 * _2710;
                            _2726 = _2701 * _2709;
                            _2727 = _2701 + _2680;
                            _2728 = mad(_2701, _2711, _2681);
                            _2729 = mad(_2701, _2710, _2682);
                            _2730 = mad(_2701, _2709, _2683);
                        }
                        else
                        {
                            _2723 = _2676;
                            _2724 = _2677;
                            _2725 = _2678;
                            _2726 = _2679;
                            _2727 = _2676 + _2680;
                            _2728 = _2677 + _2681;
                            _2729 = _2678 + _2682;
                            _2730 = _2679 + _2683;
                        }
                        float2 _2740 = float2(mad(_179, cb0_m[5u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[5u].y / resolutionScale, TEXCOORD.y));
                        float4 _2742 = t1.SampleLevel(s1, _2740, 0.0f);
                        float _2748 = clamp((_2514 * max(_2742.x, _2742.y)) - (_179 * cb0_m[5u].z / resolutionScale), 0.0f, 1.0f);
                        float _2770;
                        float _2771;
                        float _2772;
                        float _2773;
                        float _2774;
                        float _2775;
                        float _2776;
                        float _2777;
                        if (_2748 > 0.0f)
                        {
                            float4 _2755 = t0.SampleLevel(s0, _2740, 0.0f);
                            float _2756 = _2755.x;
                            float _2757 = _2755.y;
                            float _2758 = _2755.z;
                            _2770 = _2748;
                            _2771 = _2748 * _2758;
                            _2772 = _2748 * _2757;
                            _2773 = _2748 * _2756;
                            _2774 = _2748 + _2727;
                            _2775 = mad(_2748, _2758, _2728);
                            _2776 = mad(_2748, _2757, _2729);
                            _2777 = mad(_2748, _2756, _2730);
                        }
                        else
                        {
                            _2770 = _2723;
                            _2771 = _2724;
                            _2772 = _2725;
                            _2773 = _2726;
                            _2774 = _2727 + _2723;
                            _2775 = _2724 + _2728;
                            _2776 = _2725 + _2729;
                            _2777 = _2726 + _2730;
                        }
                        float2 _2787 = float2(mad(_179, cb0_m[6u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[6u].y / resolutionScale, TEXCOORD.y));
                        float4 _2789 = t1.SampleLevel(s1, _2787, 0.0f);
                        float _2795 = clamp((_2514 * max(_2789.x, _2789.y)) - (_179 * cb0_m[6u].z / resolutionScale), 0.0f, 1.0f);
                        float _2817;
                        float _2818;
                        float _2819;
                        float _2820;
                        float _2821;
                        float _2822;
                        float _2823;
                        float _2824;
                        if (_2795 > 0.0f)
                        {
                            float4 _2802 = t0.SampleLevel(s0, _2787, 0.0f);
                            float _2803 = _2802.x;
                            float _2804 = _2802.y;
                            float _2805 = _2802.z;
                            _2817 = _2795;
                            _2818 = _2795 * _2805;
                            _2819 = _2795 * _2804;
                            _2820 = _2795 * _2803;
                            _2821 = _2795 + _2774;
                            _2822 = mad(_2795, _2805, _2775);
                            _2823 = mad(_2795, _2804, _2776);
                            _2824 = mad(_2795, _2803, _2777);
                        }
                        else
                        {
                            _2817 = _2770;
                            _2818 = _2771;
                            _2819 = _2772;
                            _2820 = _2773;
                            _2821 = _2770 + _2774;
                            _2822 = _2771 + _2775;
                            _2823 = _2772 + _2776;
                            _2824 = _2777 + _2773;
                        }
                        float2 _2834 = float2(mad(_179, cb0_m[7u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[7u].y / resolutionScale, TEXCOORD.y));
                        float4 _2836 = t1.SampleLevel(s1, _2834, 0.0f);
                        float _2842 = clamp((_2514 * max(_2836.x, _2836.y)) - (_179 * cb0_m[7u].z / resolutionScale), 0.0f, 1.0f);
                        float _2864;
                        float _2865;
                        float _2866;
                        float _2867;
                        float _2868;
                        float _2869;
                        float _2870;
                        float _2871;
                        if (_2842 > 0.0f)
                        {
                            float4 _2849 = t0.SampleLevel(s0, _2834, 0.0f);
                            float _2850 = _2849.x;
                            float _2851 = _2849.y;
                            float _2852 = _2849.z;
                            _2864 = _2842;
                            _2865 = _2842 * _2852;
                            _2866 = _2842 * _2851;
                            _2867 = _2842 * _2850;
                            _2868 = _2842 + _2821;
                            _2869 = mad(_2842, _2852, _2822);
                            _2870 = mad(_2842, _2851, _2823);
                            _2871 = mad(_2842, _2850, _2824);
                        }
                        else
                        {
                            _2864 = _2817;
                            _2865 = _2818;
                            _2866 = _2819;
                            _2867 = _2820;
                            _2868 = _2817 + _2821;
                            _2869 = _2818 + _2822;
                            _2870 = _2819 + _2823;
                            _2871 = _2820 + _2824;
                        }
                        float2 _2881 = float2(mad(_179, cb0_m[8u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[8u].y / resolutionScale, TEXCOORD.y));
                        float4 _2883 = t1.SampleLevel(s1, _2881, 0.0f);
                        float _2889 = clamp((_2514 * max(_2883.x, _2883.y)) - (_179 * cb0_m[8u].z / resolutionScale), 0.0f, 1.0f);
                        float _2911;
                        float _2912;
                        float _2913;
                        float _2914;
                        float _2915;
                        float _2916;
                        float _2917;
                        float _2918;
                        if (_2889 > 0.0f)
                        {
                            float4 _2896 = t0.SampleLevel(s0, _2881, 0.0f);
                            float _2897 = _2896.x;
                            float _2898 = _2896.y;
                            float _2899 = _2896.z;
                            _2911 = _2889;
                            _2912 = _2889 * _2899;
                            _2913 = _2889 * _2898;
                            _2914 = _2889 * _2897;
                            _2915 = _2889 + _2868;
                            _2916 = mad(_2889, _2899, _2869);
                            _2917 = mad(_2889, _2898, _2870);
                            _2918 = mad(_2889, _2897, _2871);
                        }
                        else
                        {
                            _2911 = _2864;
                            _2912 = _2865;
                            _2913 = _2866;
                            _2914 = _2867;
                            _2915 = _2864 + _2868;
                            _2916 = _2865 + _2869;
                            _2917 = _2866 + _2870;
                            _2918 = _2867 + _2871;
                        }
                        float2 _2928 = float2(mad(_179, cb0_m[9u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[9u].y / resolutionScale, TEXCOORD.y));
                        float4 _2930 = t1.SampleLevel(s1, _2928, 0.0f);
                        float _2936 = clamp((_2514 * max(_2930.x, _2930.y)) - (_179 * cb0_m[9u].z / resolutionScale), 0.0f, 1.0f);
                        float _2958;
                        float _2959;
                        float _2960;
                        float _2961;
                        float _2962;
                        float _2963;
                        float _2964;
                        float _2965;
                        if (_2936 > 0.0f)
                        {
                            float4 _2943 = t0.SampleLevel(s0, _2928, 0.0f);
                            float _2944 = _2943.x;
                            float _2945 = _2943.y;
                            float _2946 = _2943.z;
                            _2958 = _2936;
                            _2959 = _2936 * _2946;
                            _2960 = _2936 * _2945;
                            _2961 = _2936 * _2944;
                            _2962 = _2936 + _2915;
                            _2963 = mad(_2936, _2946, _2916);
                            _2964 = mad(_2936, _2945, _2917);
                            _2965 = mad(_2936, _2944, _2918);
                        }
                        else
                        {
                            _2958 = _2911;
                            _2959 = _2912;
                            _2960 = _2913;
                            _2961 = _2914;
                            _2962 = _2911 + _2915;
                            _2963 = _2912 + _2916;
                            _2964 = _2913 + _2917;
                            _2965 = _2914 + _2918;
                        }
                        float2 _2975 = float2(mad(_179, cb0_m[10u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[10u].y / resolutionScale, TEXCOORD.y));
                        float4 _2977 = t1.SampleLevel(s1, _2975, 0.0f);
                        float _2983 = clamp((_2514 * max(_2977.x, _2977.y)) - (_179 * cb0_m[10u].z / resolutionScale), 0.0f, 1.0f);
                        float _3005;
                        float _3006;
                        float _3007;
                        float _3008;
                        float _3009;
                        float _3010;
                        float _3011;
                        float _3012;
                        if (_2983 > 0.0f)
                        {
                            float4 _2990 = t0.SampleLevel(s0, _2975, 0.0f);
                            float _2991 = _2990.x;
                            float _2992 = _2990.y;
                            float _2993 = _2990.z;
                            _3005 = _2983;
                            _3006 = _2983 * _2993;
                            _3007 = _2983 * _2992;
                            _3008 = _2983 * _2991;
                            _3009 = _2983 + _2962;
                            _3010 = mad(_2983, _2993, _2963);
                            _3011 = mad(_2983, _2992, _2964);
                            _3012 = mad(_2983, _2991, _2965);
                        }
                        else
                        {
                            _3005 = _2958;
                            _3006 = _2959;
                            _3007 = _2960;
                            _3008 = _2961;
                            _3009 = _2958 + _2962;
                            _3010 = _2959 + _2963;
                            _3011 = _2960 + _2964;
                            _3012 = _2961 + _2965;
                        }
                        float2 _3022 = float2(mad(_179, cb0_m[11u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[11u].y / resolutionScale, TEXCOORD.y));
                        float4 _3024 = t1.SampleLevel(s1, _3022, 0.0f);
                        float _3030 = clamp((_2514 * max(_3024.x, _3024.y)) - (_179 * cb0_m[11u].z / resolutionScale), 0.0f, 1.0f);
                        float _3052;
                        float _3053;
                        float _3054;
                        float _3055;
                        float _3056;
                        float _3057;
                        float _3058;
                        float _3059;
                        if (_3030 > 0.0f)
                        {
                            float4 _3037 = t0.SampleLevel(s0, _3022, 0.0f);
                            float _3038 = _3037.x;
                            float _3039 = _3037.y;
                            float _3040 = _3037.z;
                            _3052 = _3030;
                            _3053 = _3030 * _3040;
                            _3054 = _3030 * _3039;
                            _3055 = _3030 * _3038;
                            _3056 = _3030 + _3009;
                            _3057 = mad(_3030, _3040, _3010);
                            _3058 = mad(_3030, _3039, _3011);
                            _3059 = mad(_3030, _3038, _3012);
                        }
                        else
                        {
                            _3052 = _3005;
                            _3053 = _3006;
                            _3054 = _3007;
                            _3055 = _3008;
                            _3056 = _3005 + _3009;
                            _3057 = _3006 + _3010;
                            _3058 = _3007 + _3011;
                            _3059 = _3008 + _3012;
                        }
                        float2 _3069 = float2(mad(_179, cb0_m[12u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[12u].y / resolutionScale, TEXCOORD.y));
                        float4 _3071 = t1.SampleLevel(s1, _3069, 0.0f);
                        float _3077 = clamp((_2514 * max(_3071.x, _3071.y)) - (_179 * cb0_m[12u].z / resolutionScale), 0.0f, 1.0f);
                        float _3099;
                        float _3100;
                        float _3101;
                        float _3102;
                        float _3103;
                        float _3104;
                        float _3105;
                        float _3106;
                        if (_3077 > 0.0f)
                        {
                            float4 _3084 = t0.SampleLevel(s0, _3069, 0.0f);
                            float _3085 = _3084.x;
                            float _3086 = _3084.y;
                            float _3087 = _3084.z;
                            _3099 = _3077;
                            _3100 = _3077 * _3087;
                            _3101 = _3077 * _3086;
                            _3102 = _3077 * _3085;
                            _3103 = _3077 + _3056;
                            _3104 = mad(_3077, _3087, _3057);
                            _3105 = mad(_3077, _3086, _3058);
                            _3106 = mad(_3077, _3085, _3059);
                        }
                        else
                        {
                            _3099 = _3052;
                            _3100 = _3053;
                            _3101 = _3054;
                            _3102 = _3055;
                            _3103 = _3052 + _3056;
                            _3104 = _3053 + _3057;
                            _3105 = _3054 + _3058;
                            _3106 = _3055 + _3059;
                        }
                        float2 _3116 = float2(mad(_179, cb0_m[13u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[13u].y / resolutionScale, TEXCOORD.y));
                        float4 _3118 = t1.SampleLevel(s1, _3116, 0.0f);
                        float _3124 = clamp((_2514 * max(_3118.x, _3118.y)) - (_179 * cb0_m[13u].z / resolutionScale), 0.0f, 1.0f);
                        float _3146;
                        float _3147;
                        float _3148;
                        float _3149;
                        float _3150;
                        float _3151;
                        float _3152;
                        float _3153;
                        if (_3124 > 0.0f)
                        {
                            float4 _3131 = t0.SampleLevel(s0, _3116, 0.0f);
                            float _3132 = _3131.x;
                            float _3133 = _3131.y;
                            float _3134 = _3131.z;
                            _3146 = _3124;
                            _3147 = _3124 * _3134;
                            _3148 = _3124 * _3133;
                            _3149 = _3124 * _3132;
                            _3150 = _3124 + _3103;
                            _3151 = mad(_3124, _3134, _3104);
                            _3152 = mad(_3124, _3133, _3105);
                            _3153 = mad(_3124, _3132, _3106);
                        }
                        else
                        {
                            _3146 = _3099;
                            _3147 = _3100;
                            _3148 = _3101;
                            _3149 = _3102;
                            _3150 = _3099 + _3103;
                            _3151 = _3104 + _3100;
                            _3152 = _3101 + _3105;
                            _3153 = _3102 + _3106;
                        }
                        float2 _3163 = float2(mad(_179, cb0_m[14u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[14u].y / resolutionScale, TEXCOORD.y));
                        float4 _3165 = t1.SampleLevel(s1, _3163, 0.0f);
                        float _3171 = clamp((_2514 * max(_3165.x, _3165.y)) - (_179 * cb0_m[14u].z / resolutionScale), 0.0f, 1.0f);
                        float _3193;
                        float _3194;
                        float _3195;
                        float _3196;
                        float _3197;
                        float _3198;
                        float _3199;
                        float _3200;
                        if (_3171 > 0.0f)
                        {
                            float4 _3178 = t0.SampleLevel(s0, _3163, 0.0f);
                            float _3179 = _3178.x;
                            float _3180 = _3178.y;
                            float _3181 = _3178.z;
                            _3193 = _3171;
                            _3194 = _3171 * _3181;
                            _3195 = _3171 * _3180;
                            _3196 = _3171 * _3179;
                            _3197 = _3171 + _3150;
                            _3198 = mad(_3171, _3181, _3151);
                            _3199 = mad(_3171, _3180, _3152);
                            _3200 = mad(_3171, _3179, _3153);
                        }
                        else
                        {
                            _3193 = _3146;
                            _3194 = _3147;
                            _3195 = _3148;
                            _3196 = _3149;
                            _3197 = _3146 + _3150;
                            _3198 = _3147 + _3151;
                            _3199 = _3148 + _3152;
                            _3200 = _3149 + _3153;
                        }
                        float2 _3210 = float2(mad(_179, cb0_m[15u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[15u].y / resolutionScale, TEXCOORD.y));
                        float4 _3212 = t1.SampleLevel(s1, _3210, 0.0f);
                        float _3218 = clamp((_2514 * max(_3212.x, _3212.y)) - (_179 * cb0_m[15u].z / resolutionScale), 0.0f, 1.0f);
                        float _3240;
                        float _3241;
                        float _3242;
                        float _3243;
                        float _3244;
                        float _3245;
                        float _3246;
                        float _3247;
                        if (_3218 > 0.0f)
                        {
                            float4 _3225 = t0.SampleLevel(s0, _3210, 0.0f);
                            float _3226 = _3225.x;
                            float _3227 = _3225.y;
                            float _3228 = _3225.z;
                            _3240 = _3218;
                            _3241 = _3218 * _3228;
                            _3242 = _3218 * _3227;
                            _3243 = _3218 * _3226;
                            _3244 = _3218 + _3197;
                            _3245 = mad(_3218, _3228, _3198);
                            _3246 = mad(_3218, _3227, _3199);
                            _3247 = mad(_3218, _3226, _3200);
                        }
                        else
                        {
                            _3240 = _3193;
                            _3241 = _3194;
                            _3242 = _3195;
                            _3243 = _3196;
                            _3244 = _3193 + _3197;
                            _3245 = _3194 + _3198;
                            _3246 = _3195 + _3199;
                            _3247 = _3196 + _3200;
                        }
                        float2 _3257 = float2(mad(_179, cb0_m[16u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[16u].y / resolutionScale, TEXCOORD.y));
                        float4 _3259 = t1.SampleLevel(s1, _3257, 0.0f);
                        float _3265 = clamp((_2514 * max(_3259.x, _3259.y)) - (_179 * cb0_m[16u].z / resolutionScale), 0.0f, 1.0f);
                        float _3287;
                        float _3288;
                        float _3289;
                        float _3290;
                        float _3291;
                        float _3292;
                        float _3293;
                        float _3294;
                        if (_3265 > 0.0f)
                        {
                            float4 _3272 = t0.SampleLevel(s0, _3257, 0.0f);
                            float _3273 = _3272.x;
                            float _3274 = _3272.y;
                            float _3275 = _3272.z;
                            _3287 = _3265;
                            _3288 = _3265 * _3275;
                            _3289 = _3265 * _3274;
                            _3290 = _3265 * _3273;
                            _3291 = _3265 + _3244;
                            _3292 = mad(_3265, _3275, _3245);
                            _3293 = mad(_3265, _3274, _3246);
                            _3294 = mad(_3265, _3273, _3247);
                        }
                        else
                        {
                            _3287 = _3240;
                            _3288 = _3241;
                            _3289 = _3242;
                            _3290 = _3243;
                            _3291 = _3240 + _3244;
                            _3292 = _3241 + _3245;
                            _3293 = _3242 + _3246;
                            _3294 = _3243 + _3247;
                        }
                        float2 _3304 = float2(mad(_179, cb0_m[17u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[17u].y / resolutionScale, TEXCOORD.y));
                        float4 _3306 = t1.SampleLevel(s1, _3304, 0.0f);
                        float _3312 = clamp((_2514 * max(_3306.x, _3306.y)) - (_179 * cb0_m[17u].z / resolutionScale), 0.0f, 1.0f);
                        float _3334;
                        float _3335;
                        float _3336;
                        float _3337;
                        float _3338;
                        float _3339;
                        float _3340;
                        float _3341;
                        if (_3312 > 0.0f)
                        {
                            float4 _3319 = t0.SampleLevel(s0, _3304, 0.0f);
                            float _3320 = _3319.x;
                            float _3321 = _3319.y;
                            float _3322 = _3319.z;
                            _3334 = _3312;
                            _3335 = _3312 * _3322;
                            _3336 = _3312 * _3321;
                            _3337 = _3312 * _3320;
                            _3338 = _3312 + _3291;
                            _3339 = mad(_3312, _3322, _3292);
                            _3340 = mad(_3312, _3321, _3293);
                            _3341 = mad(_3312, _3320, _3294);
                        }
                        else
                        {
                            _3334 = _3287;
                            _3335 = _3288;
                            _3336 = _3289;
                            _3337 = _3290;
                            _3338 = _3291 + _3287;
                            _3339 = _3288 + _3292;
                            _3340 = _3289 + _3293;
                            _3341 = _3290 + _3294;
                        }
                        float2 _3351 = float2(mad(_179, cb0_m[18u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[18u].y / resolutionScale, TEXCOORD.y));
                        float4 _3353 = t1.SampleLevel(s1, _3351, 0.0f);
                        float _3359 = clamp((_2514 * max(_3353.x, _3353.y)) - (_179 * cb0_m[18u].z / resolutionScale), 0.0f, 1.0f);
                        float _3381;
                        float _3382;
                        float _3383;
                        float _3384;
                        float _3385;
                        float _3386;
                        float _3387;
                        float _3388;
                        if (_3359 > 0.0f)
                        {
                            float4 _3366 = t0.SampleLevel(s0, _3351, 0.0f);
                            float _3367 = _3366.x;
                            float _3368 = _3366.y;
                            float _3369 = _3366.z;
                            _3381 = _3359;
                            _3382 = _3359 * _3369;
                            _3383 = _3359 * _3368;
                            _3384 = _3359 * _3367;
                            _3385 = _3359 + _3338;
                            _3386 = mad(_3359, _3369, _3339);
                            _3387 = mad(_3359, _3368, _3340);
                            _3388 = mad(_3359, _3367, _3341);
                        }
                        else
                        {
                            _3381 = _3334;
                            _3382 = _3335;
                            _3383 = _3336;
                            _3384 = _3337;
                            _3385 = _3334 + _3338;
                            _3386 = _3335 + _3339;
                            _3387 = _3336 + _3340;
                            _3388 = _3341 + _3337;
                        }
                        float2 _3398 = float2(mad(_179, cb0_m[19u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[19u].y / resolutionScale, TEXCOORD.y));
                        float4 _3400 = t1.SampleLevel(s1, _3398, 0.0f);
                        float _3406 = clamp((_2514 * max(_3400.x, _3400.y)) - (_179 * cb0_m[19u].z / resolutionScale), 0.0f, 1.0f);
                        float _3428;
                        float _3429;
                        float _3430;
                        float _3431;
                        float _3432;
                        float _3433;
                        float _3434;
                        float _3435;
                        if (_3406 > 0.0f)
                        {
                            float4 _3413 = t0.SampleLevel(s0, _3398, 0.0f);
                            float _3414 = _3413.x;
                            float _3415 = _3413.y;
                            float _3416 = _3413.z;
                            _3428 = _3406;
                            _3429 = _3406 * _3416;
                            _3430 = _3406 * _3415;
                            _3431 = _3406 * _3414;
                            _3432 = _3406 + _3385;
                            _3433 = mad(_3406, _3416, _3386);
                            _3434 = mad(_3406, _3415, _3387);
                            _3435 = mad(_3406, _3414, _3388);
                        }
                        else
                        {
                            _3428 = _3381;
                            _3429 = _3382;
                            _3430 = _3383;
                            _3431 = _3384;
                            _3432 = _3381 + _3385;
                            _3433 = _3382 + _3386;
                            _3434 = _3383 + _3387;
                            _3435 = _3384 + _3388;
                        }
                        float2 _3445 = float2(mad(_179, cb0_m[20u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[20u].y / resolutionScale, TEXCOORD.y));
                        float4 _3447 = t1.SampleLevel(s1, _3445, 0.0f);
                        float _3453 = clamp((_2514 * max(_3447.x, _3447.y)) - (_179 * cb0_m[20u].z / resolutionScale), 0.0f, 1.0f);
                        float _3475;
                        float _3476;
                        float _3477;
                        float _3478;
                        float _3479;
                        float _3480;
                        float _3481;
                        float _3482;
                        if (_3453 > 0.0f)
                        {
                            float4 _3460 = t0.SampleLevel(s0, _3445, 0.0f);
                            float _3461 = _3460.x;
                            float _3462 = _3460.y;
                            float _3463 = _3460.z;
                            _3475 = _3453;
                            _3476 = _3453 * _3463;
                            _3477 = _3453 * _3462;
                            _3478 = _3453 * _3461;
                            _3479 = _3453 + _3432;
                            _3480 = mad(_3453, _3463, _3433);
                            _3481 = mad(_3453, _3462, _3434);
                            _3482 = mad(_3453, _3461, _3435);
                        }
                        else
                        {
                            _3475 = _3428;
                            _3476 = _3429;
                            _3477 = _3430;
                            _3478 = _3431;
                            _3479 = _3428 + _3432;
                            _3480 = _3429 + _3433;
                            _3481 = _3430 + _3434;
                            _3482 = _3431 + _3435;
                        }
                        float2 _3492 = float2(mad(_179, cb0_m[21u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[21u].y / resolutionScale, TEXCOORD.y));
                        float4 _3494 = t1.SampleLevel(s1, _3492, 0.0f);
                        float _3500 = clamp((_2514 * max(_3494.x, _3494.y)) - (_179 * cb0_m[21u].z / resolutionScale), 0.0f, 1.0f);
                        float _3522;
                        float _3523;
                        float _3524;
                        float _3525;
                        float _3526;
                        float _3527;
                        float _3528;
                        float _3529;
                        if (_3500 > 0.0f)
                        {
                            float4 _3507 = t0.SampleLevel(s0, _3492, 0.0f);
                            float _3508 = _3507.x;
                            float _3509 = _3507.y;
                            float _3510 = _3507.z;
                            _3522 = _3500;
                            _3523 = _3500 * _3510;
                            _3524 = _3500 * _3509;
                            _3525 = _3500 * _3508;
                            _3526 = _3500 + _3479;
                            _3527 = mad(_3500, _3510, _3480);
                            _3528 = mad(_3500, _3509, _3481);
                            _3529 = mad(_3500, _3508, _3482);
                        }
                        else
                        {
                            _3522 = _3475;
                            _3523 = _3476;
                            _3524 = _3477;
                            _3525 = _3478;
                            _3526 = _3475 + _3479;
                            _3527 = _3476 + _3480;
                            _3528 = _3477 + _3481;
                            _3529 = _3478 + _3482;
                        }
                        float2 _3539 = float2(mad(_179, cb0_m[22u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[22u].y / resolutionScale, TEXCOORD.y));
                        float4 _3541 = t1.SampleLevel(s1, _3539, 0.0f);
                        float _3547 = clamp((_2514 * max(_3541.x, _3541.y)) - (_179 * cb0_m[22u].z / resolutionScale), 0.0f, 1.0f);
                        float _3569;
                        float _3570;
                        float _3571;
                        float _3572;
                        float _3573;
                        float _3574;
                        float _3575;
                        float _3576;
                        if (_3547 > 0.0f)
                        {
                            float4 _3554 = t0.SampleLevel(s0, _3539, 0.0f);
                            float _3555 = _3554.x;
                            float _3556 = _3554.y;
                            float _3557 = _3554.z;
                            _3569 = _3547;
                            _3570 = _3547 * _3557;
                            _3571 = _3547 * _3556;
                            _3572 = _3547 * _3555;
                            _3573 = _3547 + _3526;
                            _3574 = mad(_3547, _3557, _3527);
                            _3575 = mad(_3547, _3556, _3528);
                            _3576 = mad(_3547, _3555, _3529);
                        }
                        else
                        {
                            _3569 = _3522;
                            _3570 = _3523;
                            _3571 = _3524;
                            _3572 = _3525;
                            _3573 = _3522 + _3526;
                            _3574 = _3523 + _3527;
                            _3575 = _3528 + _3524;
                            _3576 = _3525 + _3529;
                        }
                        float2 _3586 = float2(mad(_179, cb0_m[23u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[23u].y / resolutionScale, TEXCOORD.y));
                        float4 _3588 = t1.SampleLevel(s1, _3586, 0.0f);
                        float _3594 = clamp((_2514 * max(_3588.x, _3588.y)) - (_179 * cb0_m[23u].z / resolutionScale), 0.0f, 1.0f);
                        float _3613;
                        float _3614;
                        float _3615;
                        float _3616;
                        if (_3594 > 0.0f)
                        {
                            float4 _3601 = t0.SampleLevel(s0, _3586, 0.0f);
                            _3613 = _3594 + _3573;
                            _3614 = mad(_3594, _3601.z, _3574);
                            _3615 = mad(_3594, _3601.y, _3575);
                            _3616 = mad(_3594, _3601.x, _3576);
                        }
                        else
                        {
                            _3613 = _3569 + _3573;
                            _3614 = _3570 + _3574;
                            _3615 = _3571 + _3575;
                            _3616 = _3572 + _3576;
                        }
                        _5864 = _3613;
                        _5865 = _3614;
                        _5866 = _3615;
                        _5867 = _3616;
                    }
                    else
                    {
                        float2 _3627 = float2(mad(_179, cb0_m[0u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[0u].y / resolutionScale, TEXCOORD.y));
                        float4 _3629 = t1.SampleLevel(s1, _3627, 0.0f);
                        float _3633 = cb1_m1.w * 0.5f;
                        float _3636 = clamp((max(_3629.x, _3629.y) * _3633) - (_179 * cb0_m[0u].z / resolutionScale), 0.0f, 1.0f);
                        float _3654;
                        float _3655;
                        float _3656;
                        float _3657;
                        float _3658;
                        float _3659;
                        float _3660;
                        float _3661;
                        if (_3636 > 0.0f)
                        {
                            float4 _3643 = t0.SampleLevel(s0, _3627, 0.0f);
                            float _3644 = _3643.x;
                            float _3645 = _3643.y;
                            float _3646 = _3643.z;
                            _3654 = _3636;
                            _3655 = _3636 * _3646;
                            _3656 = _3636 * _3645;
                            _3657 = _3636 * _3644;
                            _3658 = _3636 + 1.0f;
                            _3659 = mad(_3636, _3646, _176);
                            _3660 = mad(_3636, _3645, _175);
                            _3661 = mad(_3636, _3644, _174);
                        }
                        else
                        {
                            _3654 = 0.0f;
                            _3655 = 0.0f;
                            _3656 = 0.0f;
                            _3657 = 0.0f;
                            _3658 = 1.0f;
                            _3659 = _176;
                            _3660 = _175;
                            _3661 = _174;
                        }
                        float2 _3671 = float2(mad(_179, cb0_m[1u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[1u].y / resolutionScale, TEXCOORD.y));
                        float4 _3673 = t1.SampleLevel(s1, _3671, 0.0f);
                        float _3679 = clamp((_3633 * max(_3673.x, _3673.y)) - (_179 * cb0_m[1u].z / resolutionScale), 0.0f, 1.0f);
                        float _3701;
                        float _3702;
                        float _3703;
                        float _3704;
                        float _3705;
                        float _3706;
                        float _3707;
                        float _3708;
                        if (_3679 > 0.0f)
                        {
                            float4 _3686 = t0.SampleLevel(s0, _3671, 0.0f);
                            float _3687 = _3686.x;
                            float _3688 = _3686.y;
                            float _3689 = _3686.z;
                            _3701 = _3679;
                            _3702 = _3679 * _3689;
                            _3703 = _3679 * _3688;
                            _3704 = _3679 * _3687;
                            _3705 = _3679 + _3658;
                            _3706 = mad(_3679, _3689, _3659);
                            _3707 = mad(_3679, _3688, _3660);
                            _3708 = mad(_3679, _3687, _3661);
                        }
                        else
                        {
                            _3701 = _3654;
                            _3702 = _3655;
                            _3703 = _3656;
                            _3704 = _3657;
                            _3705 = _3654 + _3658;
                            _3706 = _3655 + _3659;
                            _3707 = _3660 + _3656;
                            _3708 = _3657 + _3661;
                        }
                        float2 _3718 = float2(mad(_179, cb0_m[2u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[2u].y / resolutionScale, TEXCOORD.y));
                        float4 _3720 = t1.SampleLevel(s1, _3718, 0.0f);
                        float _3726 = clamp((_3633 * max(_3720.x, _3720.y)) - (_179 * cb0_m[2u].z / resolutionScale), 0.0f, 1.0f);
                        float _3748;
                        float _3749;
                        float _3750;
                        float _3751;
                        float _3752;
                        float _3753;
                        float _3754;
                        float _3755;
                        if (_3726 > 0.0f)
                        {
                            float4 _3733 = t0.SampleLevel(s0, _3718, 0.0f);
                            float _3734 = _3733.x;
                            float _3735 = _3733.y;
                            float _3736 = _3733.z;
                            _3748 = _3726;
                            _3749 = _3726 * _3736;
                            _3750 = _3726 * _3735;
                            _3751 = _3726 * _3734;
                            _3752 = _3726 + _3705;
                            _3753 = mad(_3726, _3736, _3706);
                            _3754 = mad(_3726, _3735, _3707);
                            _3755 = mad(_3726, _3734, _3708);
                        }
                        else
                        {
                            _3748 = _3701;
                            _3749 = _3702;
                            _3750 = _3703;
                            _3751 = _3704;
                            _3752 = _3701 + _3705;
                            _3753 = _3702 + _3706;
                            _3754 = _3703 + _3707;
                            _3755 = _3704 + _3708;
                        }
                        float2 _3765 = float2(mad(_179, cb0_m[3u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[3u].y / resolutionScale, TEXCOORD.y));
                        float4 _3767 = t1.SampleLevel(s1, _3765, 0.0f);
                        float _3773 = clamp((_3633 * max(_3767.x, _3767.y)) - (_179 * cb0_m[3u].z / resolutionScale), 0.0f, 1.0f);
                        float _3795;
                        float _3796;
                        float _3797;
                        float _3798;
                        float _3799;
                        float _3800;
                        float _3801;
                        float _3802;
                        if (_3773 > 0.0f)
                        {
                            float4 _3780 = t0.SampleLevel(s0, _3765, 0.0f);
                            float _3781 = _3780.x;
                            float _3782 = _3780.y;
                            float _3783 = _3780.z;
                            _3795 = _3773;
                            _3796 = _3773 * _3783;
                            _3797 = _3773 * _3782;
                            _3798 = _3773 * _3781;
                            _3799 = _3773 + _3752;
                            _3800 = mad(_3773, _3783, _3753);
                            _3801 = mad(_3773, _3782, _3754);
                            _3802 = mad(_3773, _3781, _3755);
                        }
                        else
                        {
                            _3795 = _3748;
                            _3796 = _3749;
                            _3797 = _3750;
                            _3798 = _3751;
                            _3799 = _3748 + _3752;
                            _3800 = _3749 + _3753;
                            _3801 = _3750 + _3754;
                            _3802 = _3751 + _3755;
                        }
                        float2 _3812 = float2(mad(_179, cb0_m[4u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[4u].y / resolutionScale, TEXCOORD.y));
                        float4 _3814 = t1.SampleLevel(s1, _3812, 0.0f);
                        float _3820 = clamp((_3633 * max(_3814.x, _3814.y)) - (_179 * cb0_m[4u].z / resolutionScale), 0.0f, 1.0f);
                        float _3842;
                        float _3843;
                        float _3844;
                        float _3845;
                        float _3846;
                        float _3847;
                        float _3848;
                        float _3849;
                        if (_3820 > 0.0f)
                        {
                            float4 _3827 = t0.SampleLevel(s0, _3812, 0.0f);
                            float _3828 = _3827.x;
                            float _3829 = _3827.y;
                            float _3830 = _3827.z;
                            _3842 = _3820;
                            _3843 = _3820 * _3830;
                            _3844 = _3820 * _3829;
                            _3845 = _3820 * _3828;
                            _3846 = _3820 + _3799;
                            _3847 = mad(_3820, _3830, _3800);
                            _3848 = mad(_3820, _3829, _3801);
                            _3849 = mad(_3820, _3828, _3802);
                        }
                        else
                        {
                            _3842 = _3795;
                            _3843 = _3796;
                            _3844 = _3797;
                            _3845 = _3798;
                            _3846 = _3795 + _3799;
                            _3847 = _3800 + _3796;
                            _3848 = _3797 + _3801;
                            _3849 = _3798 + _3802;
                        }
                        float2 _3859 = float2(mad(_179, cb0_m[5u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[5u].y / resolutionScale, TEXCOORD.y));
                        float4 _3861 = t1.SampleLevel(s1, _3859, 0.0f);
                        float _3867 = clamp((_3633 * max(_3861.x, _3861.y)) - (_179 * cb0_m[5u].z / resolutionScale), 0.0f, 1.0f);
                        float _3889;
                        float _3890;
                        float _3891;
                        float _3892;
                        float _3893;
                        float _3894;
                        float _3895;
                        float _3896;
                        if (_3867 > 0.0f)
                        {
                            float4 _3874 = t0.SampleLevel(s0, _3859, 0.0f);
                            float _3875 = _3874.x;
                            float _3876 = _3874.y;
                            float _3877 = _3874.z;
                            _3889 = _3867;
                            _3890 = _3867 * _3877;
                            _3891 = _3867 * _3876;
                            _3892 = _3867 * _3875;
                            _3893 = _3867 + _3846;
                            _3894 = mad(_3867, _3877, _3847);
                            _3895 = mad(_3867, _3876, _3848);
                            _3896 = mad(_3867, _3875, _3849);
                        }
                        else
                        {
                            _3889 = _3842;
                            _3890 = _3843;
                            _3891 = _3844;
                            _3892 = _3845;
                            _3893 = _3842 + _3846;
                            _3894 = _3843 + _3847;
                            _3895 = _3844 + _3848;
                            _3896 = _3845 + _3849;
                        }
                        float2 _3906 = float2(mad(_179, cb0_m[6u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[6u].y / resolutionScale, TEXCOORD.y));
                        float4 _3908 = t1.SampleLevel(s1, _3906, 0.0f);
                        float _3914 = clamp((_3633 * max(_3908.x, _3908.y)) - (_179 * cb0_m[6u].z / resolutionScale), 0.0f, 1.0f);
                        float _3936;
                        float _3937;
                        float _3938;
                        float _3939;
                        float _3940;
                        float _3941;
                        float _3942;
                        float _3943;
                        if (_3914 > 0.0f)
                        {
                            float4 _3921 = t0.SampleLevel(s0, _3906, 0.0f);
                            float _3922 = _3921.x;
                            float _3923 = _3921.y;
                            float _3924 = _3921.z;
                            _3936 = _3914;
                            _3937 = _3914 * _3924;
                            _3938 = _3914 * _3923;
                            _3939 = _3914 * _3922;
                            _3940 = _3914 + _3893;
                            _3941 = mad(_3914, _3924, _3894);
                            _3942 = mad(_3914, _3923, _3895);
                            _3943 = mad(_3914, _3922, _3896);
                        }
                        else
                        {
                            _3936 = _3889;
                            _3937 = _3890;
                            _3938 = _3891;
                            _3939 = _3892;
                            _3940 = _3889 + _3893;
                            _3941 = _3890 + _3894;
                            _3942 = _3891 + _3895;
                            _3943 = _3892 + _3896;
                        }
                        float2 _3953 = float2(mad(_179, cb0_m[7u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[7u].y / resolutionScale, TEXCOORD.y));
                        float4 _3955 = t1.SampleLevel(s1, _3953, 0.0f);
                        float _3961 = clamp((_3633 * max(_3955.x, _3955.y)) - (_179 * cb0_m[7u].z / resolutionScale), 0.0f, 1.0f);
                        float _3983;
                        float _3984;
                        float _3985;
                        float _3986;
                        float _3987;
                        float _3988;
                        float _3989;
                        float _3990;
                        if (_3961 > 0.0f)
                        {
                            float4 _3968 = t0.SampleLevel(s0, _3953, 0.0f);
                            float _3969 = _3968.x;
                            float _3970 = _3968.y;
                            float _3971 = _3968.z;
                            _3983 = _3961;
                            _3984 = _3961 * _3971;
                            _3985 = _3961 * _3970;
                            _3986 = _3961 * _3969;
                            _3987 = _3961 + _3940;
                            _3988 = mad(_3961, _3971, _3941);
                            _3989 = mad(_3961, _3970, _3942);
                            _3990 = mad(_3961, _3969, _3943);
                        }
                        else
                        {
                            _3983 = _3936;
                            _3984 = _3937;
                            _3985 = _3938;
                            _3986 = _3939;
                            _3987 = _3936 + _3940;
                            _3988 = _3937 + _3941;
                            _3989 = _3938 + _3942;
                            _3990 = _3939 + _3943;
                        }
                        float2 _4000 = float2(mad(_179, cb0_m[8u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[8u].y / resolutionScale, TEXCOORD.y));
                        float4 _4002 = t1.SampleLevel(s1, _4000, 0.0f);
                        float _4008 = clamp((_3633 * max(_4002.x, _4002.y)) - (_179 * cb0_m[8u].z / resolutionScale), 0.0f, 1.0f);
                        float _4030;
                        float _4031;
                        float _4032;
                        float _4033;
                        float _4034;
                        float _4035;
                        float _4036;
                        float _4037;
                        if (_4008 > 0.0f)
                        {
                            float4 _4015 = t0.SampleLevel(s0, _4000, 0.0f);
                            float _4016 = _4015.x;
                            float _4017 = _4015.y;
                            float _4018 = _4015.z;
                            _4030 = _4008;
                            _4031 = _4008 * _4018;
                            _4032 = _4008 * _4017;
                            _4033 = _4008 * _4016;
                            _4034 = _4008 + _3987;
                            _4035 = mad(_4008, _4018, _3988);
                            _4036 = mad(_4008, _4017, _3989);
                            _4037 = mad(_4008, _4016, _3990);
                        }
                        else
                        {
                            _4030 = _3983;
                            _4031 = _3984;
                            _4032 = _3985;
                            _4033 = _3986;
                            _4034 = _3987 + _3983;
                            _4035 = _3984 + _3988;
                            _4036 = _3985 + _3989;
                            _4037 = _3986 + _3990;
                        }
                        float2 _4047 = float2(mad(_179, cb0_m[9u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[9u].y / resolutionScale, TEXCOORD.y));
                        float4 _4049 = t1.SampleLevel(s1, _4047, 0.0f);
                        float _4055 = clamp((_3633 * max(_4049.x, _4049.y)) - (_179 * cb0_m[9u].z / resolutionScale), 0.0f, 1.0f);
                        float _4077;
                        float _4078;
                        float _4079;
                        float _4080;
                        float _4081;
                        float _4082;
                        float _4083;
                        float _4084;
                        if (_4055 > 0.0f)
                        {
                            float4 _4062 = t0.SampleLevel(s0, _4047, 0.0f);
                            float _4063 = _4062.x;
                            float _4064 = _4062.y;
                            float _4065 = _4062.z;
                            _4077 = _4055;
                            _4078 = _4055 * _4065;
                            _4079 = _4055 * _4064;
                            _4080 = _4055 * _4063;
                            _4081 = _4055 + _4034;
                            _4082 = mad(_4055, _4065, _4035);
                            _4083 = mad(_4055, _4064, _4036);
                            _4084 = mad(_4055, _4063, _4037);
                        }
                        else
                        {
                            _4077 = _4030;
                            _4078 = _4031;
                            _4079 = _4032;
                            _4080 = _4033;
                            _4081 = _4030 + _4034;
                            _4082 = _4031 + _4035;
                            _4083 = _4032 + _4036;
                            _4084 = _4033 + _4037;
                        }
                        float2 _4094 = float2(mad(_179, cb0_m[10u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[10u].y / resolutionScale, TEXCOORD.y));
                        float4 _4096 = t1.SampleLevel(s1, _4094, 0.0f);
                        float _4102 = clamp((_3633 * max(_4096.x, _4096.y)) - (_179 * cb0_m[10u].z / resolutionScale), 0.0f, 1.0f);
                        float _4124;
                        float _4125;
                        float _4126;
                        float _4127;
                        float _4128;
                        float _4129;
                        float _4130;
                        float _4131;
                        if (_4102 > 0.0f)
                        {
                            float4 _4109 = t0.SampleLevel(s0, _4094, 0.0f);
                            float _4110 = _4109.x;
                            float _4111 = _4109.y;
                            float _4112 = _4109.z;
                            _4124 = _4102;
                            _4125 = _4102 * _4112;
                            _4126 = _4102 * _4111;
                            _4127 = _4102 * _4110;
                            _4128 = _4102 + _4081;
                            _4129 = mad(_4102, _4112, _4082);
                            _4130 = mad(_4102, _4111, _4083);
                            _4131 = mad(_4102, _4110, _4084);
                        }
                        else
                        {
                            _4124 = _4077;
                            _4125 = _4078;
                            _4126 = _4079;
                            _4127 = _4080;
                            _4128 = _4077 + _4081;
                            _4129 = _4078 + _4082;
                            _4130 = _4079 + _4083;
                            _4131 = _4080 + _4084;
                        }
                        float2 _4141 = float2(mad(_179, cb0_m[11u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[11u].y / resolutionScale, TEXCOORD.y));
                        float4 _4143 = t1.SampleLevel(s1, _4141, 0.0f);
                        float _4149 = clamp((_3633 * max(_4143.x, _4143.y)) - (_179 * cb0_m[11u].z / resolutionScale), 0.0f, 1.0f);
                        float _4171;
                        float _4172;
                        float _4173;
                        float _4174;
                        float _4175;
                        float _4176;
                        float _4177;
                        float _4178;
                        if (_4149 > 0.0f)
                        {
                            float4 _4156 = t0.SampleLevel(s0, _4141, 0.0f);
                            float _4157 = _4156.x;
                            float _4158 = _4156.y;
                            float _4159 = _4156.z;
                            _4171 = _4149;
                            _4172 = _4149 * _4159;
                            _4173 = _4149 * _4158;
                            _4174 = _4149 * _4157;
                            _4175 = _4149 + _4128;
                            _4176 = mad(_4149, _4159, _4129);
                            _4177 = mad(_4149, _4158, _4130);
                            _4178 = mad(_4149, _4157, _4131);
                        }
                        else
                        {
                            _4171 = _4124;
                            _4172 = _4125;
                            _4173 = _4126;
                            _4174 = _4127;
                            _4175 = _4124 + _4128;
                            _4176 = _4125 + _4129;
                            _4177 = _4126 + _4130;
                            _4178 = _4127 + _4131;
                        }
                        float2 _4188 = float2(mad(_179, cb0_m[12u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[12u].y / resolutionScale, TEXCOORD.y));
                        float4 _4190 = t1.SampleLevel(s1, _4188, 0.0f);
                        float _4196 = clamp((_3633 * max(_4190.x, _4190.y)) - (_179 * cb0_m[12u].z / resolutionScale), 0.0f, 1.0f);
                        float _4218;
                        float _4219;
                        float _4220;
                        float _4221;
                        float _4222;
                        float _4223;
                        float _4224;
                        float _4225;
                        if (_4196 > 0.0f)
                        {
                            float4 _4203 = t0.SampleLevel(s0, _4188, 0.0f);
                            float _4204 = _4203.x;
                            float _4205 = _4203.y;
                            float _4206 = _4203.z;
                            _4218 = _4196;
                            _4219 = _4196 * _4206;
                            _4220 = _4196 * _4205;
                            _4221 = _4196 * _4204;
                            _4222 = _4196 + _4175;
                            _4223 = mad(_4196, _4206, _4176);
                            _4224 = mad(_4196, _4205, _4177);
                            _4225 = mad(_4196, _4204, _4178);
                        }
                        else
                        {
                            _4218 = _4171;
                            _4219 = _4172;
                            _4220 = _4173;
                            _4221 = _4174;
                            _4222 = _4171 + _4175;
                            _4223 = _4172 + _4176;
                            _4224 = _4173 + _4177;
                            _4225 = _4174 + _4178;
                        }
                        float2 _4235 = float2(mad(_179, cb0_m[13u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[13u].y / resolutionScale, TEXCOORD.y));
                        float4 _4237 = t1.SampleLevel(s1, _4235, 0.0f);
                        float _4243 = clamp((_3633 * max(_4237.x, _4237.y)) - (_179 * cb0_m[13u].z / resolutionScale), 0.0f, 1.0f);
                        float _4265;
                        float _4266;
                        float _4267;
                        float _4268;
                        float _4269;
                        float _4270;
                        float _4271;
                        float _4272;
                        if (_4243 > 0.0f)
                        {
                            float4 _4250 = t0.SampleLevel(s0, _4235, 0.0f);
                            float _4251 = _4250.x;
                            float _4252 = _4250.y;
                            float _4253 = _4250.z;
                            _4265 = _4243;
                            _4266 = _4243 * _4253;
                            _4267 = _4243 * _4252;
                            _4268 = _4243 * _4251;
                            _4269 = _4243 + _4222;
                            _4270 = mad(_4243, _4253, _4223);
                            _4271 = mad(_4243, _4252, _4224);
                            _4272 = mad(_4243, _4251, _4225);
                        }
                        else
                        {
                            _4265 = _4218;
                            _4266 = _4219;
                            _4267 = _4220;
                            _4268 = _4221;
                            _4269 = _4218 + _4222;
                            _4270 = _4219 + _4223;
                            _4271 = _4220 + _4224;
                            _4272 = _4221 + _4225;
                        }
                        float2 _4282 = float2(mad(_179, cb0_m[14u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[14u].y / resolutionScale, TEXCOORD.y));
                        float4 _4284 = t1.SampleLevel(s1, _4282, 0.0f);
                        float _4290 = clamp((_3633 * max(_4284.x, _4284.y)) - (_179 * cb0_m[14u].z / resolutionScale), 0.0f, 1.0f);
                        float _4312;
                        float _4313;
                        float _4314;
                        float _4315;
                        float _4316;
                        float _4317;
                        float _4318;
                        float _4319;
                        if (_4290 > 0.0f)
                        {
                            float4 _4297 = t0.SampleLevel(s0, _4282, 0.0f);
                            float _4298 = _4297.x;
                            float _4299 = _4297.y;
                            float _4300 = _4297.z;
                            _4312 = _4290;
                            _4313 = _4290 * _4300;
                            _4314 = _4290 * _4299;
                            _4315 = _4290 * _4298;
                            _4316 = _4290 + _4269;
                            _4317 = mad(_4290, _4300, _4270);
                            _4318 = mad(_4290, _4299, _4271);
                            _4319 = mad(_4290, _4298, _4272);
                        }
                        else
                        {
                            _4312 = _4265;
                            _4313 = _4266;
                            _4314 = _4267;
                            _4315 = _4268;
                            _4316 = _4265 + _4269;
                            _4317 = _4266 + _4270;
                            _4318 = _4267 + _4271;
                            _4319 = _4272 + _4268;
                        }
                        float2 _4329 = float2(mad(_179, cb0_m[15u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[15u].y / resolutionScale, TEXCOORD.y));
                        float4 _4331 = t1.SampleLevel(s1, _4329, 0.0f);
                        float _4337 = clamp((_3633 * max(_4331.x, _4331.y)) - (_179 * cb0_m[15u].z / resolutionScale), 0.0f, 1.0f);
                        float _4359;
                        float _4360;
                        float _4361;
                        float _4362;
                        float _4363;
                        float _4364;
                        float _4365;
                        float _4366;
                        if (_4337 > 0.0f)
                        {
                            float4 _4344 = t0.SampleLevel(s0, _4329, 0.0f);
                            float _4345 = _4344.x;
                            float _4346 = _4344.y;
                            float _4347 = _4344.z;
                            _4359 = _4337;
                            _4360 = _4337 * _4347;
                            _4361 = _4337 * _4346;
                            _4362 = _4337 * _4345;
                            _4363 = _4337 + _4316;
                            _4364 = mad(_4337, _4347, _4317);
                            _4365 = mad(_4337, _4346, _4318);
                            _4366 = mad(_4337, _4345, _4319);
                        }
                        else
                        {
                            _4359 = _4312;
                            _4360 = _4313;
                            _4361 = _4314;
                            _4362 = _4315;
                            _4363 = _4312 + _4316;
                            _4364 = _4313 + _4317;
                            _4365 = _4314 + _4318;
                            _4366 = _4315 + _4319;
                        }
                        float2 _4376 = float2(mad(_179, cb0_m[16u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[16u].y / resolutionScale, TEXCOORD.y));
                        float4 _4378 = t1.SampleLevel(s1, _4376, 0.0f);
                        float _4384 = clamp((_3633 * max(_4378.x, _4378.y)) - (_179 * cb0_m[16u].z / resolutionScale), 0.0f, 1.0f);
                        float _4406;
                        float _4407;
                        float _4408;
                        float _4409;
                        float _4410;
                        float _4411;
                        float _4412;
                        float _4413;
                        if (_4384 > 0.0f)
                        {
                            float4 _4391 = t0.SampleLevel(s0, _4376, 0.0f);
                            float _4392 = _4391.x;
                            float _4393 = _4391.y;
                            float _4394 = _4391.z;
                            _4406 = _4384;
                            _4407 = _4384 * _4394;
                            _4408 = _4384 * _4393;
                            _4409 = _4384 * _4392;
                            _4410 = _4384 + _4363;
                            _4411 = mad(_4384, _4394, _4364);
                            _4412 = mad(_4384, _4393, _4365);
                            _4413 = mad(_4384, _4392, _4366);
                        }
                        else
                        {
                            _4406 = _4359;
                            _4407 = _4360;
                            _4408 = _4361;
                            _4409 = _4362;
                            _4410 = _4359 + _4363;
                            _4411 = _4360 + _4364;
                            _4412 = _4361 + _4365;
                            _4413 = _4362 + _4366;
                        }
                        float2 _4423 = float2(mad(_179, cb0_m[17u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[17u].y / resolutionScale, TEXCOORD.y));
                        float4 _4425 = t1.SampleLevel(s1, _4423, 0.0f);
                        float _4431 = clamp((_3633 * max(_4425.x, _4425.y)) - (_179 * cb0_m[17u].z / resolutionScale), 0.0f, 1.0f);
                        float _4453;
                        float _4454;
                        float _4455;
                        float _4456;
                        float _4457;
                        float _4458;
                        float _4459;
                        float _4460;
                        if (_4431 > 0.0f)
                        {
                            float4 _4438 = t0.SampleLevel(s0, _4423, 0.0f);
                            float _4439 = _4438.x;
                            float _4440 = _4438.y;
                            float _4441 = _4438.z;
                            _4453 = _4431;
                            _4454 = _4431 * _4441;
                            _4455 = _4431 * _4440;
                            _4456 = _4431 * _4439;
                            _4457 = _4431 + _4410;
                            _4458 = mad(_4431, _4441, _4411);
                            _4459 = mad(_4431, _4440, _4412);
                            _4460 = mad(_4431, _4439, _4413);
                        }
                        else
                        {
                            _4453 = _4406;
                            _4454 = _4407;
                            _4455 = _4408;
                            _4456 = _4409;
                            _4457 = _4406 + _4410;
                            _4458 = _4407 + _4411;
                            _4459 = _4408 + _4412;
                            _4460 = _4409 + _4413;
                        }
                        float2 _4470 = float2(mad(_179, cb0_m[18u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[18u].y / resolutionScale, TEXCOORD.y));
                        float4 _4472 = t1.SampleLevel(s1, _4470, 0.0f);
                        float _4478 = clamp((_3633 * max(_4472.x, _4472.y)) - (_179 * cb0_m[18u].z / resolutionScale), 0.0f, 1.0f);
                        float _4500;
                        float _4501;
                        float _4502;
                        float _4503;
                        float _4504;
                        float _4505;
                        float _4506;
                        float _4507;
                        if (_4478 > 0.0f)
                        {
                            float4 _4485 = t0.SampleLevel(s0, _4470, 0.0f);
                            float _4486 = _4485.x;
                            float _4487 = _4485.y;
                            float _4488 = _4485.z;
                            _4500 = _4478;
                            _4501 = _4478 * _4488;
                            _4502 = _4478 * _4487;
                            _4503 = _4478 * _4486;
                            _4504 = _4478 + _4457;
                            _4505 = mad(_4478, _4488, _4458);
                            _4506 = mad(_4478, _4487, _4459);
                            _4507 = mad(_4478, _4486, _4460);
                        }
                        else
                        {
                            _4500 = _4453;
                            _4501 = _4454;
                            _4502 = _4455;
                            _4503 = _4456;
                            _4504 = _4453 + _4457;
                            _4505 = _4454 + _4458;
                            _4506 = _4459 + _4455;
                            _4507 = _4456 + _4460;
                        }
                        float2 _4517 = float2(mad(_179, cb0_m[19u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[19u].y / resolutionScale, TEXCOORD.y));
                        float4 _4519 = t1.SampleLevel(s1, _4517, 0.0f);
                        float _4525 = clamp((_3633 * max(_4519.x, _4519.y)) - (_179 * cb0_m[19u].z / resolutionScale), 0.0f, 1.0f);
                        float _4547;
                        float _4548;
                        float _4549;
                        float _4550;
                        float _4551;
                        float _4552;
                        float _4553;
                        float _4554;
                        if (_4525 > 0.0f)
                        {
                            float4 _4532 = t0.SampleLevel(s0, _4517, 0.0f);
                            float _4533 = _4532.x;
                            float _4534 = _4532.y;
                            float _4535 = _4532.z;
                            _4547 = _4525;
                            _4548 = _4525 * _4535;
                            _4549 = _4525 * _4534;
                            _4550 = _4525 * _4533;
                            _4551 = _4525 + _4504;
                            _4552 = mad(_4525, _4535, _4505);
                            _4553 = mad(_4525, _4534, _4506);
                            _4554 = mad(_4525, _4533, _4507);
                        }
                        else
                        {
                            _4547 = _4500;
                            _4548 = _4501;
                            _4549 = _4502;
                            _4550 = _4503;
                            _4551 = _4500 + _4504;
                            _4552 = _4501 + _4505;
                            _4553 = _4502 + _4506;
                            _4554 = _4503 + _4507;
                        }
                        float2 _4564 = float2(mad(_179, cb0_m[20u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[20u].y / resolutionScale, TEXCOORD.y));
                        float4 _4566 = t1.SampleLevel(s1, _4564, 0.0f);
                        float _4572 = clamp((_3633 * max(_4566.x, _4566.y)) - (_179 * cb0_m[20u].z / resolutionScale), 0.0f, 1.0f);
                        float _4594;
                        float _4595;
                        float _4596;
                        float _4597;
                        float _4598;
                        float _4599;
                        float _4600;
                        float _4601;
                        if (_4572 > 0.0f)
                        {
                            float4 _4579 = t0.SampleLevel(s0, _4564, 0.0f);
                            float _4580 = _4579.x;
                            float _4581 = _4579.y;
                            float _4582 = _4579.z;
                            _4594 = _4572;
                            _4595 = _4572 * _4582;
                            _4596 = _4572 * _4581;
                            _4597 = _4572 * _4580;
                            _4598 = _4572 + _4551;
                            _4599 = mad(_4572, _4582, _4552);
                            _4600 = mad(_4572, _4581, _4553);
                            _4601 = mad(_4572, _4580, _4554);
                        }
                        else
                        {
                            _4594 = _4547;
                            _4595 = _4548;
                            _4596 = _4549;
                            _4597 = _4550;
                            _4598 = _4547 + _4551;
                            _4599 = _4548 + _4552;
                            _4600 = _4549 + _4553;
                            _4601 = _4550 + _4554;
                        }
                        float2 _4611 = float2(mad(_179, cb0_m[21u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[21u].y / resolutionScale, TEXCOORD.y));
                        float4 _4613 = t1.SampleLevel(s1, _4611, 0.0f);
                        float _4619 = clamp((_3633 * max(_4613.x, _4613.y)) - (_179 * cb0_m[21u].z / resolutionScale), 0.0f, 1.0f);
                        float _4641;
                        float _4642;
                        float _4643;
                        float _4644;
                        float _4645;
                        float _4646;
                        float _4647;
                        float _4648;
                        if (_4619 > 0.0f)
                        {
                            float4 _4626 = t0.SampleLevel(s0, _4611, 0.0f);
                            float _4627 = _4626.x;
                            float _4628 = _4626.y;
                            float _4629 = _4626.z;
                            _4641 = _4619;
                            _4642 = _4619 * _4629;
                            _4643 = _4619 * _4628;
                            _4644 = _4619 * _4627;
                            _4645 = _4619 + _4598;
                            _4646 = mad(_4619, _4629, _4599);
                            _4647 = mad(_4619, _4628, _4600);
                            _4648 = mad(_4619, _4627, _4601);
                        }
                        else
                        {
                            _4641 = _4594;
                            _4642 = _4595;
                            _4643 = _4596;
                            _4644 = _4597;
                            _4645 = _4594 + _4598;
                            _4646 = _4595 + _4599;
                            _4647 = _4596 + _4600;
                            _4648 = _4597 + _4601;
                        }
                        float2 _4658 = float2(mad(_179, cb0_m[22u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[22u].y / resolutionScale, TEXCOORD.y));
                        float4 _4660 = t1.SampleLevel(s1, _4658, 0.0f);
                        float _4666 = clamp((_3633 * max(_4660.x, _4660.y)) - (_179 * cb0_m[22u].z / resolutionScale), 0.0f, 1.0f);
                        float _4688;
                        float _4689;
                        float _4690;
                        float _4691;
                        float _4692;
                        float _4693;
                        float _4694;
                        float _4695;
                        if (_4666 > 0.0f)
                        {
                            float4 _4673 = t0.SampleLevel(s0, _4658, 0.0f);
                            float _4674 = _4673.x;
                            float _4675 = _4673.y;
                            float _4676 = _4673.z;
                            _4688 = _4666;
                            _4689 = _4666 * _4676;
                            _4690 = _4666 * _4675;
                            _4691 = _4666 * _4674;
                            _4692 = _4666 + _4645;
                            _4693 = mad(_4666, _4676, _4646);
                            _4694 = mad(_4666, _4675, _4647);
                            _4695 = mad(_4666, _4674, _4648);
                        }
                        else
                        {
                            _4688 = _4641;
                            _4689 = _4642;
                            _4690 = _4643;
                            _4691 = _4644;
                            _4692 = _4641 + _4645;
                            _4693 = _4646 + _4642;
                            _4694 = _4643 + _4647;
                            _4695 = _4644 + _4648;
                        }
                        float2 _4705 = float2(mad(_179, cb0_m[23u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[23u].y / resolutionScale, TEXCOORD.y));
                        float4 _4707 = t1.SampleLevel(s1, _4705, 0.0f);
                        float _4713 = clamp((_3633 * max(_4707.x, _4707.y)) - (_179 * cb0_m[23u].z / resolutionScale), 0.0f, 1.0f);
                        float _4735;
                        float _4736;
                        float _4737;
                        float _4738;
                        float _4739;
                        float _4740;
                        float _4741;
                        float _4742;
                        if (_4713 > 0.0f)
                        {
                            float4 _4720 = t0.SampleLevel(s0, _4705, 0.0f);
                            float _4721 = _4720.x;
                            float _4722 = _4720.y;
                            float _4723 = _4720.z;
                            _4735 = _4713;
                            _4736 = _4713 * _4723;
                            _4737 = _4713 * _4722;
                            _4738 = _4713 * _4721;
                            _4739 = _4713 + _4692;
                            _4740 = mad(_4713, _4723, _4693);
                            _4741 = mad(_4713, _4722, _4694);
                            _4742 = mad(_4713, _4721, _4695);
                        }
                        else
                        {
                            _4735 = _4688;
                            _4736 = _4689;
                            _4737 = _4690;
                            _4738 = _4691;
                            _4739 = _4688 + _4692;
                            _4740 = _4689 + _4693;
                            _4741 = _4690 + _4694;
                            _4742 = _4691 + _4695;
                        }
                        float2 _4752 = float2(mad(_179, cb0_m[24u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[24u].y / resolutionScale, TEXCOORD.y));
                        float4 _4754 = t1.SampleLevel(s1, _4752, 0.0f);
                        float _4760 = clamp((_3633 * max(_4754.x, _4754.y)) - (_179 * cb0_m[24u].z / resolutionScale), 0.0f, 1.0f);
                        float _4782;
                        float _4783;
                        float _4784;
                        float _4785;
                        float _4786;
                        float _4787;
                        float _4788;
                        float _4789;
                        if (_4760 > 0.0f)
                        {
                            float4 _4767 = t0.SampleLevel(s0, _4752, 0.0f);
                            float _4768 = _4767.x;
                            float _4769 = _4767.y;
                            float _4770 = _4767.z;
                            _4782 = _4760;
                            _4783 = _4760 * _4770;
                            _4784 = _4760 * _4769;
                            _4785 = _4760 * _4768;
                            _4786 = _4760 + _4739;
                            _4787 = mad(_4760, _4770, _4740);
                            _4788 = mad(_4760, _4769, _4741);
                            _4789 = mad(_4760, _4768, _4742);
                        }
                        else
                        {
                            _4782 = _4735;
                            _4783 = _4736;
                            _4784 = _4737;
                            _4785 = _4738;
                            _4786 = _4735 + _4739;
                            _4787 = _4736 + _4740;
                            _4788 = _4737 + _4741;
                            _4789 = _4738 + _4742;
                        }
                        float2 _4799 = float2(mad(_179, cb0_m[25u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[25u].y / resolutionScale, TEXCOORD.y));
                        float4 _4801 = t1.SampleLevel(s1, _4799, 0.0f);
                        float _4807 = clamp((_3633 * max(_4801.x, _4801.y)) - (_179 * cb0_m[25u].z / resolutionScale), 0.0f, 1.0f);
                        float _4829;
                        float _4830;
                        float _4831;
                        float _4832;
                        float _4833;
                        float _4834;
                        float _4835;
                        float _4836;
                        if (_4807 > 0.0f)
                        {
                            float4 _4814 = t0.SampleLevel(s0, _4799, 0.0f);
                            float _4815 = _4814.x;
                            float _4816 = _4814.y;
                            float _4817 = _4814.z;
                            _4829 = _4807;
                            _4830 = _4807 * _4817;
                            _4831 = _4807 * _4816;
                            _4832 = _4807 * _4815;
                            _4833 = _4807 + _4786;
                            _4834 = mad(_4807, _4817, _4787);
                            _4835 = mad(_4807, _4816, _4788);
                            _4836 = mad(_4807, _4815, _4789);
                        }
                        else
                        {
                            _4829 = _4782;
                            _4830 = _4783;
                            _4831 = _4784;
                            _4832 = _4785;
                            _4833 = _4782 + _4786;
                            _4834 = _4783 + _4787;
                            _4835 = _4784 + _4788;
                            _4836 = _4785 + _4789;
                        }
                        float2 _4846 = float2(mad(_179, cb0_m[26u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[26u].y / resolutionScale, TEXCOORD.y));
                        float4 _4848 = t1.SampleLevel(s1, _4846, 0.0f);
                        float _4854 = clamp((_3633 * max(_4848.x, _4848.y)) - (_179 * cb0_m[26u].z / resolutionScale), 0.0f, 1.0f);
                        float _4876;
                        float _4877;
                        float _4878;
                        float _4879;
                        float _4880;
                        float _4881;
                        float _4882;
                        float _4883;
                        if (_4854 > 0.0f)
                        {
                            float4 _4861 = t0.SampleLevel(s0, _4846, 0.0f);
                            float _4862 = _4861.x;
                            float _4863 = _4861.y;
                            float _4864 = _4861.z;
                            _4876 = _4854;
                            _4877 = _4854 * _4864;
                            _4878 = _4854 * _4863;
                            _4879 = _4854 * _4862;
                            _4880 = _4854 + _4833;
                            _4881 = mad(_4854, _4864, _4834);
                            _4882 = mad(_4854, _4863, _4835);
                            _4883 = mad(_4854, _4862, _4836);
                        }
                        else
                        {
                            _4876 = _4829;
                            _4877 = _4830;
                            _4878 = _4831;
                            _4879 = _4832;
                            _4880 = _4833 + _4829;
                            _4881 = _4830 + _4834;
                            _4882 = _4831 + _4835;
                            _4883 = _4832 + _4836;
                        }
                        float2 _4893 = float2(mad(_179, cb0_m[27u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[27u].y / resolutionScale, TEXCOORD.y));
                        float4 _4895 = t1.SampleLevel(s1, _4893, 0.0f);
                        float _4901 = clamp((_3633 * max(_4895.x, _4895.y)) - (_179 * cb0_m[27u].z / resolutionScale), 0.0f, 1.0f);
                        float _4923;
                        float _4924;
                        float _4925;
                        float _4926;
                        float _4927;
                        float _4928;
                        float _4929;
                        float _4930;
                        if (_4901 > 0.0f)
                        {
                            float4 _4908 = t0.SampleLevel(s0, _4893, 0.0f);
                            float _4909 = _4908.x;
                            float _4910 = _4908.y;
                            float _4911 = _4908.z;
                            _4923 = _4901;
                            _4924 = _4901 * _4911;
                            _4925 = _4901 * _4910;
                            _4926 = _4901 * _4909;
                            _4927 = _4901 + _4880;
                            _4928 = mad(_4901, _4911, _4881);
                            _4929 = mad(_4901, _4910, _4882);
                            _4930 = mad(_4901, _4909, _4883);
                        }
                        else
                        {
                            _4923 = _4876;
                            _4924 = _4877;
                            _4925 = _4878;
                            _4926 = _4879;
                            _4927 = _4876 + _4880;
                            _4928 = _4877 + _4881;
                            _4929 = _4878 + _4882;
                            _4930 = _4883 + _4879;
                        }
                        float2 _4940 = float2(mad(_179, cb0_m[28u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[28u].y / resolutionScale, TEXCOORD.y));
                        float4 _4942 = t1.SampleLevel(s1, _4940, 0.0f);
                        float _4948 = clamp((_3633 * max(_4942.x, _4942.y)) - (_179 * cb0_m[28u].z / resolutionScale), 0.0f, 1.0f);
                        float _4970;
                        float _4971;
                        float _4972;
                        float _4973;
                        float _4974;
                        float _4975;
                        float _4976;
                        float _4977;
                        if (_4948 > 0.0f)
                        {
                            float4 _4955 = t0.SampleLevel(s0, _4940, 0.0f);
                            float _4956 = _4955.x;
                            float _4957 = _4955.y;
                            float _4958 = _4955.z;
                            _4970 = _4948;
                            _4971 = _4948 * _4958;
                            _4972 = _4948 * _4957;
                            _4973 = _4948 * _4956;
                            _4974 = _4948 + _4927;
                            _4975 = mad(_4948, _4958, _4928);
                            _4976 = mad(_4948, _4957, _4929);
                            _4977 = mad(_4948, _4956, _4930);
                        }
                        else
                        {
                            _4970 = _4923;
                            _4971 = _4924;
                            _4972 = _4925;
                            _4973 = _4926;
                            _4974 = _4923 + _4927;
                            _4975 = _4924 + _4928;
                            _4976 = _4925 + _4929;
                            _4977 = _4926 + _4930;
                        }
                        float2 _4987 = float2(mad(_179, cb0_m[29u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[29u].y / resolutionScale, TEXCOORD.y));
                        float4 _4989 = t1.SampleLevel(s1, _4987, 0.0f);
                        float _4995 = clamp((_3633 * max(_4989.x, _4989.y)) - (_179 * cb0_m[29u].z / resolutionScale), 0.0f, 1.0f);
                        float _5017;
                        float _5018;
                        float _5019;
                        float _5020;
                        float _5021;
                        float _5022;
                        float _5023;
                        float _5024;
                        if (_4995 > 0.0f)
                        {
                            float4 _5002 = t0.SampleLevel(s0, _4987, 0.0f);
                            float _5003 = _5002.x;
                            float _5004 = _5002.y;
                            float _5005 = _5002.z;
                            _5017 = _4995;
                            _5018 = _4995 * _5005;
                            _5019 = _4995 * _5004;
                            _5020 = _4995 * _5003;
                            _5021 = _4995 + _4974;
                            _5022 = mad(_4995, _5005, _4975);
                            _5023 = mad(_4995, _5004, _4976);
                            _5024 = mad(_4995, _5003, _4977);
                        }
                        else
                        {
                            _5017 = _4970;
                            _5018 = _4971;
                            _5019 = _4972;
                            _5020 = _4973;
                            _5021 = _4970 + _4974;
                            _5022 = _4971 + _4975;
                            _5023 = _4972 + _4976;
                            _5024 = _4973 + _4977;
                        }
                        float2 _5034 = float2(mad(_179, cb0_m[30u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[30u].y / resolutionScale, TEXCOORD.y));
                        float4 _5036 = t1.SampleLevel(s1, _5034, 0.0f);
                        float _5042 = clamp((_3633 * max(_5036.x, _5036.y)) - (_179 * cb0_m[30u].z / resolutionScale), 0.0f, 1.0f);
                        float _5064;
                        float _5065;
                        float _5066;
                        float _5067;
                        float _5068;
                        float _5069;
                        float _5070;
                        float _5071;
                        if (_5042 > 0.0f)
                        {
                            float4 _5049 = t0.SampleLevel(s0, _5034, 0.0f);
                            float _5050 = _5049.x;
                            float _5051 = _5049.y;
                            float _5052 = _5049.z;
                            _5064 = _5042;
                            _5065 = _5042 * _5052;
                            _5066 = _5042 * _5051;
                            _5067 = _5042 * _5050;
                            _5068 = _5042 + _5021;
                            _5069 = mad(_5042, _5052, _5022);
                            _5070 = mad(_5042, _5051, _5023);
                            _5071 = mad(_5042, _5050, _5024);
                        }
                        else
                        {
                            _5064 = _5017;
                            _5065 = _5018;
                            _5066 = _5019;
                            _5067 = _5020;
                            _5068 = _5017 + _5021;
                            _5069 = _5018 + _5022;
                            _5070 = _5019 + _5023;
                            _5071 = _5020 + _5024;
                        }
                        float2 _5081 = float2(mad(_179, cb0_m[31u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[31u].y / resolutionScale, TEXCOORD.y));
                        float4 _5083 = t1.SampleLevel(s1, _5081, 0.0f);
                        float _5089 = clamp((_3633 * max(_5083.x, _5083.y)) - (_179 * cb0_m[31u].z / resolutionScale), 0.0f, 1.0f);
                        float _5111;
                        float _5112;
                        float _5113;
                        float _5114;
                        float _5115;
                        float _5116;
                        float _5117;
                        float _5118;
                        if (_5089 > 0.0f)
                        {
                            float4 _5096 = t0.SampleLevel(s0, _5081, 0.0f);
                            float _5097 = _5096.x;
                            float _5098 = _5096.y;
                            float _5099 = _5096.z;
                            _5111 = _5089;
                            _5112 = _5089 * _5099;
                            _5113 = _5089 * _5098;
                            _5114 = _5089 * _5097;
                            _5115 = _5089 + _5068;
                            _5116 = mad(_5089, _5099, _5069);
                            _5117 = mad(_5089, _5098, _5070);
                            _5118 = mad(_5089, _5097, _5071);
                        }
                        else
                        {
                            _5111 = _5064;
                            _5112 = _5065;
                            _5113 = _5066;
                            _5114 = _5067;
                            _5115 = _5064 + _5068;
                            _5116 = _5065 + _5069;
                            _5117 = _5070 + _5066;
                            _5118 = _5067 + _5071;
                        }
                        float2 _5128 = float2(mad(_179, cb0_m[32u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[32u].y / resolutionScale, TEXCOORD.y));
                        float4 _5130 = t1.SampleLevel(s1, _5128, 0.0f);
                        float _5136 = clamp((_3633 * max(_5130.x, _5130.y)) - (_179 * cb0_m[32u].z / resolutionScale), 0.0f, 1.0f);
                        float _5158;
                        float _5159;
                        float _5160;
                        float _5161;
                        float _5162;
                        float _5163;
                        float _5164;
                        float _5165;
                        if (_5136 > 0.0f)
                        {
                            float4 _5143 = t0.SampleLevel(s0, _5128, 0.0f);
                            float _5144 = _5143.x;
                            float _5145 = _5143.y;
                            float _5146 = _5143.z;
                            _5158 = _5136;
                            _5159 = _5136 * _5146;
                            _5160 = _5136 * _5145;
                            _5161 = _5136 * _5144;
                            _5162 = _5136 + _5115;
                            _5163 = mad(_5136, _5146, _5116);
                            _5164 = mad(_5136, _5145, _5117);
                            _5165 = mad(_5136, _5144, _5118);
                        }
                        else
                        {
                            _5158 = _5111;
                            _5159 = _5112;
                            _5160 = _5113;
                            _5161 = _5114;
                            _5162 = _5111 + _5115;
                            _5163 = _5112 + _5116;
                            _5164 = _5113 + _5117;
                            _5165 = _5114 + _5118;
                        }
                        float2 _5175 = float2(mad(_179, cb0_m[33u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[33u].y / resolutionScale, TEXCOORD.y));
                        float4 _5177 = t1.SampleLevel(s1, _5175, 0.0f);
                        float _5183 = clamp((_3633 * max(_5177.x, _5177.y)) - (_179 * cb0_m[33u].z / resolutionScale), 0.0f, 1.0f);
                        float _5205;
                        float _5206;
                        float _5207;
                        float _5208;
                        float _5209;
                        float _5210;
                        float _5211;
                        float _5212;
                        if (_5183 > 0.0f)
                        {
                            float4 _5190 = t0.SampleLevel(s0, _5175, 0.0f);
                            float _5191 = _5190.x;
                            float _5192 = _5190.y;
                            float _5193 = _5190.z;
                            _5205 = _5183;
                            _5206 = _5183 * _5193;
                            _5207 = _5183 * _5192;
                            _5208 = _5183 * _5191;
                            _5209 = _5183 + _5162;
                            _5210 = mad(_5183, _5193, _5163);
                            _5211 = mad(_5183, _5192, _5164);
                            _5212 = mad(_5183, _5191, _5165);
                        }
                        else
                        {
                            _5205 = _5158;
                            _5206 = _5159;
                            _5207 = _5160;
                            _5208 = _5161;
                            _5209 = _5158 + _5162;
                            _5210 = _5159 + _5163;
                            _5211 = _5160 + _5164;
                            _5212 = _5161 + _5165;
                        }
                        float2 _5222 = float2(mad(_179, cb0_m[34u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[34u].y / resolutionScale, TEXCOORD.y));
                        float4 _5224 = t1.SampleLevel(s1, _5222, 0.0f);
                        float _5230 = clamp((_3633 * max(_5224.x, _5224.y)) - (_179 * cb0_m[34u].z / resolutionScale), 0.0f, 1.0f);
                        float _5252;
                        float _5253;
                        float _5254;
                        float _5255;
                        float _5256;
                        float _5257;
                        float _5258;
                        float _5259;
                        if (_5230 > 0.0f)
                        {
                            float4 _5237 = t0.SampleLevel(s0, _5222, 0.0f);
                            float _5238 = _5237.x;
                            float _5239 = _5237.y;
                            float _5240 = _5237.z;
                            _5252 = _5230;
                            _5253 = _5230 * _5240;
                            _5254 = _5230 * _5239;
                            _5255 = _5230 * _5238;
                            _5256 = _5230 + _5209;
                            _5257 = mad(_5230, _5240, _5210);
                            _5258 = mad(_5230, _5239, _5211);
                            _5259 = mad(_5230, _5238, _5212);
                        }
                        else
                        {
                            _5252 = _5205;
                            _5253 = _5206;
                            _5254 = _5207;
                            _5255 = _5208;
                            _5256 = _5205 + _5209;
                            _5257 = _5206 + _5210;
                            _5258 = _5207 + _5211;
                            _5259 = _5208 + _5212;
                        }
                        float2 _5269 = float2(mad(_179, cb0_m[35u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[35u].y / resolutionScale, TEXCOORD.y));
                        float4 _5271 = t1.SampleLevel(s1, _5269, 0.0f);
                        float _5277 = clamp((_3633 * max(_5271.x, _5271.y)) - (_179 * cb0_m[35u].z / resolutionScale), 0.0f, 1.0f);
                        float _5299;
                        float _5300;
                        float _5301;
                        float _5302;
                        float _5303;
                        float _5304;
                        float _5305;
                        float _5306;
                        if (_5277 > 0.0f)
                        {
                            float4 _5284 = t0.SampleLevel(s0, _5269, 0.0f);
                            float _5285 = _5284.x;
                            float _5286 = _5284.y;
                            float _5287 = _5284.z;
                            _5299 = _5277;
                            _5300 = _5277 * _5287;
                            _5301 = _5277 * _5286;
                            _5302 = _5277 * _5285;
                            _5303 = _5277 + _5256;
                            _5304 = mad(_5277, _5287, _5257);
                            _5305 = mad(_5277, _5286, _5258);
                            _5306 = mad(_5277, _5285, _5259);
                        }
                        else
                        {
                            _5299 = _5252;
                            _5300 = _5253;
                            _5301 = _5254;
                            _5302 = _5255;
                            _5303 = _5252 + _5256;
                            _5304 = _5257 + _5253;
                            _5305 = _5254 + _5258;
                            _5306 = _5255 + _5259;
                        }
                        float2 _5316 = float2(mad(_179, cb0_m[36u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[36u].y / resolutionScale, TEXCOORD.y));
                        float4 _5318 = t1.SampleLevel(s1, _5316, 0.0f);
                        float _5324 = clamp((_3633 * max(_5318.x, _5318.y)) - (_179 * cb0_m[36u].z / resolutionScale), 0.0f, 1.0f);
                        float _5346;
                        float _5347;
                        float _5348;
                        float _5349;
                        float _5350;
                        float _5351;
                        float _5352;
                        float _5353;
                        if (_5324 > 0.0f)
                        {
                            float4 _5331 = t0.SampleLevel(s0, _5316, 0.0f);
                            float _5332 = _5331.x;
                            float _5333 = _5331.y;
                            float _5334 = _5331.z;
                            _5346 = _5324;
                            _5347 = _5324 * _5334;
                            _5348 = _5324 * _5333;
                            _5349 = _5324 * _5332;
                            _5350 = _5324 + _5303;
                            _5351 = mad(_5324, _5334, _5304);
                            _5352 = mad(_5324, _5333, _5305);
                            _5353 = mad(_5324, _5332, _5306);
                        }
                        else
                        {
                            _5346 = _5299;
                            _5347 = _5300;
                            _5348 = _5301;
                            _5349 = _5302;
                            _5350 = _5299 + _5303;
                            _5351 = _5300 + _5304;
                            _5352 = _5301 + _5305;
                            _5353 = _5302 + _5306;
                        }
                        float2 _5363 = float2(mad(_179, cb0_m[37u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[37u].y / resolutionScale, TEXCOORD.y));
                        float4 _5365 = t1.SampleLevel(s1, _5363, 0.0f);
                        float _5371 = clamp((_3633 * max(_5365.x, _5365.y)) - (_179 * cb0_m[37u].z / resolutionScale), 0.0f, 1.0f);
                        float _5393;
                        float _5394;
                        float _5395;
                        float _5396;
                        float _5397;
                        float _5398;
                        float _5399;
                        float _5400;
                        if (_5371 > 0.0f)
                        {
                            float4 _5378 = t0.SampleLevel(s0, _5363, 0.0f);
                            float _5379 = _5378.x;
                            float _5380 = _5378.y;
                            float _5381 = _5378.z;
                            _5393 = _5371;
                            _5394 = _5371 * _5381;
                            _5395 = _5371 * _5380;
                            _5396 = _5371 * _5379;
                            _5397 = _5371 + _5350;
                            _5398 = mad(_5371, _5381, _5351);
                            _5399 = mad(_5371, _5380, _5352);
                            _5400 = mad(_5371, _5379, _5353);
                        }
                        else
                        {
                            _5393 = _5346;
                            _5394 = _5347;
                            _5395 = _5348;
                            _5396 = _5349;
                            _5397 = _5346 + _5350;
                            _5398 = _5347 + _5351;
                            _5399 = _5348 + _5352;
                            _5400 = _5349 + _5353;
                        }
                        float2 _5410 = float2(mad(_179, cb0_m[38u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[38u].y / resolutionScale, TEXCOORD.y));
                        float4 _5412 = t1.SampleLevel(s1, _5410, 0.0f);
                        float _5418 = clamp((_3633 * max(_5412.x, _5412.y)) - (_179 * cb0_m[38u].z / resolutionScale), 0.0f, 1.0f);
                        float _5440;
                        float _5441;
                        float _5442;
                        float _5443;
                        float _5444;
                        float _5445;
                        float _5446;
                        float _5447;
                        if (_5418 > 0.0f)
                        {
                            float4 _5425 = t0.SampleLevel(s0, _5410, 0.0f);
                            float _5426 = _5425.x;
                            float _5427 = _5425.y;
                            float _5428 = _5425.z;
                            _5440 = _5418;
                            _5441 = _5418 * _5428;
                            _5442 = _5418 * _5427;
                            _5443 = _5418 * _5426;
                            _5444 = _5418 + _5397;
                            _5445 = mad(_5418, _5428, _5398);
                            _5446 = mad(_5418, _5427, _5399);
                            _5447 = mad(_5418, _5426, _5400);
                        }
                        else
                        {
                            _5440 = _5393;
                            _5441 = _5394;
                            _5442 = _5395;
                            _5443 = _5396;
                            _5444 = _5393 + _5397;
                            _5445 = _5394 + _5398;
                            _5446 = _5395 + _5399;
                            _5447 = _5396 + _5400;
                        }
                        float2 _5457 = float2(mad(_179, cb0_m[39u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[39u].y / resolutionScale, TEXCOORD.y));
                        float4 _5459 = t1.SampleLevel(s1, _5457, 0.0f);
                        float _5465 = clamp((_3633 * max(_5459.x, _5459.y)) - (_179 * cb0_m[39u].z / resolutionScale), 0.0f, 1.0f);
                        float _5487;
                        float _5488;
                        float _5489;
                        float _5490;
                        float _5491;
                        float _5492;
                        float _5493;
                        float _5494;
                        if (_5465 > 0.0f)
                        {
                            float4 _5472 = t0.SampleLevel(s0, _5457, 0.0f);
                            float _5473 = _5472.x;
                            float _5474 = _5472.y;
                            float _5475 = _5472.z;
                            _5487 = _5465;
                            _5488 = _5465 * _5475;
                            _5489 = _5465 * _5474;
                            _5490 = _5465 * _5473;
                            _5491 = _5444 + _5465;
                            _5492 = mad(_5465, _5475, _5445);
                            _5493 = mad(_5465, _5474, _5446);
                            _5494 = mad(_5465, _5473, _5447);
                        }
                        else
                        {
                            _5487 = _5440;
                            _5488 = _5441;
                            _5489 = _5442;
                            _5490 = _5443;
                            _5491 = _5444 + _5440;
                            _5492 = _5441 + _5445;
                            _5493 = _5442 + _5446;
                            _5494 = _5443 + _5447;
                        }
                        float2 _5504 = float2(mad(_179, cb0_m[40u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[40u].y / resolutionScale, TEXCOORD.y));
                        float4 _5506 = t1.SampleLevel(s1, _5504, 0.0f);
                        float _5512 = clamp((_3633 * max(_5506.x, _5506.y)) - (_179 * cb0_m[40u].z / resolutionScale), 0.0f, 1.0f);
                        float _5534;
                        float _5535;
                        float _5536;
                        float _5537;
                        float _5538;
                        float _5539;
                        float _5540;
                        float _5541;
                        if (_5512 > 0.0f)
                        {
                            float4 _5519 = t0.SampleLevel(s0, _5504, 0.0f);
                            float _5520 = _5519.x;
                            float _5521 = _5519.y;
                            float _5522 = _5519.z;
                            _5534 = _5512;
                            _5535 = _5512 * _5522;
                            _5536 = _5512 * _5521;
                            _5537 = _5512 * _5520;
                            _5538 = _5491 + _5512;
                            _5539 = mad(_5512, _5522, _5492);
                            _5540 = mad(_5512, _5521, _5493);
                            _5541 = mad(_5512, _5520, _5494);
                        }
                        else
                        {
                            _5534 = _5487;
                            _5535 = _5488;
                            _5536 = _5489;
                            _5537 = _5490;
                            _5538 = _5487 + _5491;
                            _5539 = _5488 + _5492;
                            _5540 = _5489 + _5493;
                            _5541 = _5490 + _5494;
                        }
                        float2 _5551 = float2(mad(_179, cb0_m[41u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[41u].y / resolutionScale, TEXCOORD.y));
                        float4 _5553 = t1.SampleLevel(s1, _5551, 0.0f);
                        float _5559 = clamp((_3633 * max(_5553.x, _5553.y)) - (_179 * cb0_m[41u].z / resolutionScale), 0.0f, 1.0f);
                        float _5581;
                        float _5582;
                        float _5583;
                        float _5584;
                        float _5585;
                        float _5586;
                        float _5587;
                        float _5588;
                        if (_5559 > 0.0f)
                        {
                            float4 _5566 = t0.SampleLevel(s0, _5551, 0.0f);
                            float _5567 = _5566.x;
                            float _5568 = _5566.y;
                            float _5569 = _5566.z;
                            _5581 = _5559;
                            _5582 = _5559 * _5569;
                            _5583 = _5559 * _5568;
                            _5584 = _5559 * _5567;
                            _5585 = _5538 + _5559;
                            _5586 = mad(_5559, _5569, _5539);
                            _5587 = mad(_5559, _5568, _5540);
                            _5588 = mad(_5559, _5567, _5541);
                        }
                        else
                        {
                            _5581 = _5534;
                            _5582 = _5535;
                            _5583 = _5536;
                            _5584 = _5537;
                            _5585 = _5534 + _5538;
                            _5586 = _5535 + _5539;
                            _5587 = _5536 + _5540;
                            _5588 = _5537 + _5541;
                        }
                        float2 _5598 = float2(mad(_179, cb0_m[42u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[42u].y / resolutionScale, TEXCOORD.y));
                        float4 _5600 = t1.SampleLevel(s1, _5598, 0.0f);
                        float _5606 = clamp((_3633 * max(_5600.x, _5600.y)) - (_179 * cb0_m[42u].z / resolutionScale), 0.0f, 1.0f);
                        float _5628;
                        float _5629;
                        float _5630;
                        float _5631;
                        float _5632;
                        float _5633;
                        float _5634;
                        float _5635;
                        if (_5606 > 0.0f)
                        {
                            float4 _5613 = t0.SampleLevel(s0, _5598, 0.0f);
                            float _5614 = _5613.x;
                            float _5615 = _5613.y;
                            float _5616 = _5613.z;
                            _5628 = _5606;
                            _5629 = _5606 * _5616;
                            _5630 = _5606 * _5615;
                            _5631 = _5606 * _5614;
                            _5632 = _5585 + _5606;
                            _5633 = mad(_5606, _5616, _5586);
                            _5634 = mad(_5606, _5615, _5587);
                            _5635 = mad(_5606, _5614, _5588);
                        }
                        else
                        {
                            _5628 = _5581;
                            _5629 = _5582;
                            _5630 = _5583;
                            _5631 = _5584;
                            _5632 = _5581 + _5585;
                            _5633 = _5582 + _5586;
                            _5634 = _5583 + _5587;
                            _5635 = _5584 + _5588;
                        }
                        float2 _5645 = float2(mad(_179, cb0_m[43u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[43u].y / resolutionScale, TEXCOORD.y));
                        float4 _5647 = t1.SampleLevel(s1, _5645, 0.0f);
                        float _5653 = clamp((_3633 * max(_5647.x, _5647.y)) - (_179 * cb0_m[43u].z / resolutionScale), 0.0f, 1.0f);
                        float _5675;
                        float _5676;
                        float _5677;
                        float _5678;
                        float _5679;
                        float _5680;
                        float _5681;
                        float _5682;
                        if (_5653 > 0.0f)
                        {
                            float4 _5660 = t0.SampleLevel(s0, _5645, 0.0f);
                            float _5661 = _5660.x;
                            float _5662 = _5660.y;
                            float _5663 = _5660.z;
                            _5675 = _5653;
                            _5676 = _5653 * _5663;
                            _5677 = _5653 * _5662;
                            _5678 = _5653 * _5661;
                            _5679 = _5632 + _5653;
                            _5680 = mad(_5653, _5663, _5633);
                            _5681 = mad(_5653, _5662, _5634);
                            _5682 = mad(_5653, _5661, _5635);
                        }
                        else
                        {
                            _5675 = _5628;
                            _5676 = _5629;
                            _5677 = _5630;
                            _5678 = _5631;
                            _5679 = _5628 + _5632;
                            _5680 = _5629 + _5633;
                            _5681 = _5630 + _5634;
                            _5682 = _5631 + _5635;
                        }
                        float2 _5692 = float2(mad(_179, cb0_m[44u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[44u].y / resolutionScale, TEXCOORD.y));
                        float4 _5694 = t1.SampleLevel(s1, _5692, 0.0f);
                        float _5700 = clamp((_3633 * max(_5694.x, _5694.y)) - (_179 * cb0_m[44u].z / resolutionScale), 0.0f, 1.0f);
                        float _5722;
                        float _5723;
                        float _5724;
                        float _5725;
                        float _5726;
                        float _5727;
                        float _5728;
                        float _5729;
                        if (_5700 > 0.0f)
                        {
                            float4 _5707 = t0.SampleLevel(s0, _5692, 0.0f);
                            float _5708 = _5707.x;
                            float _5709 = _5707.y;
                            float _5710 = _5707.z;
                            _5722 = _5700;
                            _5723 = _5700 * _5710;
                            _5724 = _5700 * _5709;
                            _5725 = _5700 * _5708;
                            _5726 = _5679 + _5700;
                            _5727 = mad(_5700, _5710, _5680);
                            _5728 = mad(_5700, _5709, _5681);
                            _5729 = mad(_5700, _5708, _5682);
                        }
                        else
                        {
                            _5722 = _5675;
                            _5723 = _5676;
                            _5724 = _5677;
                            _5725 = _5678;
                            _5726 = _5675 + _5679;
                            _5727 = _5676 + _5680;
                            _5728 = _5677 + _5681;
                            _5729 = _5678 + _5682;
                        }
                        float2 _5739 = float2(mad(_179, cb0_m[45u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[45u].y / resolutionScale, TEXCOORD.y));
                        float4 _5741 = t1.SampleLevel(s1, _5739, 0.0f);
                        float _5747 = clamp((_3633 * max(_5741.x, _5741.y)) - (_179 * cb0_m[45u].z / resolutionScale), 0.0f, 1.0f);
                        float _5769;
                        float _5770;
                        float _5771;
                        float _5772;
                        float _5773;
                        float _5774;
                        float _5775;
                        float _5776;
                        if (_5747 > 0.0f)
                        {
                            float4 _5754 = t0.SampleLevel(s0, _5739, 0.0f);
                            float _5755 = _5754.x;
                            float _5756 = _5754.y;
                            float _5757 = _5754.z;
                            _5769 = _5747;
                            _5770 = _5747 * _5757;
                            _5771 = _5747 * _5756;
                            _5772 = _5747 * _5755;
                            _5773 = _5726 + _5747;
                            _5774 = mad(_5747, _5757, _5727);
                            _5775 = mad(_5747, _5756, _5728);
                            _5776 = mad(_5747, _5755, _5729);
                        }
                        else
                        {
                            _5769 = _5722;
                            _5770 = _5723;
                            _5771 = _5724;
                            _5772 = _5725;
                            _5773 = _5722 + _5726;
                            _5774 = _5723 + _5727;
                            _5775 = _5724 + _5728;
                            _5776 = _5729 + _5725;
                        }
                        float2 _5786 = float2(mad(_179, cb0_m[46u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[46u].y / resolutionScale, TEXCOORD.y));
                        float4 _5788 = t1.SampleLevel(s1, _5786, 0.0f);
                        float _5794 = clamp((_3633 * max(_5788.x, _5788.y)) - (_179 * cb0_m[46u].z / resolutionScale), 0.0f, 1.0f);
                        float _5816;
                        float _5817;
                        float _5818;
                        float _5819;
                        float _5820;
                        float _5821;
                        float _5822;
                        float _5823;
                        if (_5794 > 0.0f)
                        {
                            float4 _5801 = t0.SampleLevel(s0, _5786, 0.0f);
                            float _5802 = _5801.x;
                            float _5803 = _5801.y;
                            float _5804 = _5801.z;
                            _5816 = _5794;
                            _5817 = _5794 * _5804;
                            _5818 = _5794 * _5803;
                            _5819 = _5794 * _5802;
                            _5820 = _5773 + _5794;
                            _5821 = mad(_5794, _5804, _5774);
                            _5822 = mad(_5794, _5803, _5775);
                            _5823 = mad(_5794, _5802, _5776);
                        }
                        else
                        {
                            _5816 = _5769;
                            _5817 = _5770;
                            _5818 = _5771;
                            _5819 = _5772;
                            _5820 = _5769 + _5773;
                            _5821 = _5770 + _5774;
                            _5822 = _5771 + _5775;
                            _5823 = _5772 + _5776;
                        }
                        float2 _5833 = float2(mad(_179, cb0_m[47u].x / resolutionScale, TEXCOORD.x), mad(_179, cb0_m[47u].y / resolutionScale, TEXCOORD.y));
                        float4 _5835 = t1.SampleLevel(s1, _5833, 0.0f);
                        float _5841 = clamp((_3633 * max(_5835.x, _5835.y)) - (_179 * cb0_m[47u].z / resolutionScale), 0.0f, 1.0f);
                        float _5860;
                        float _5861;
                        float _5862;
                        float _5863;
                        if (_5841 > 0.0f)
                        {
                            float4 _5848 = t0.SampleLevel(s0, _5833, 0.0f);
                            _5860 = _5820 + _5841;
                            _5861 = mad(_5841, _5848.z, _5821);
                            _5862 = mad(_5841, _5848.y, _5822);
                            _5863 = mad(_5841, _5848.x, _5823);
                        }
                        else
                        {
                            _5860 = _5816 + _5820;
                            _5861 = _5817 + _5821;
                            _5862 = _5818 + _5822;
                            _5863 = _5819 + _5823;
                        }
                        _5864 = _5860;
                        _5865 = _5861;
                        _5866 = _5862;
                        _5867 = _5863;
                    }
                    _5868 = _5864;
                    _5869 = _5865;
                    _5870 = _5866;
                    _5871 = _5867;
                }
                _5875 = _5869 / _5868;
                _5876 = _5870 / _5868;
                _5877 = _5871 / _5868;
            }
            else
            {
                _5875 = _176;
                _5876 = _175;
                _5877 = _174;
            }
            _5878 = _5875;
            _5879 = _5876;
            _5880 = _5877;
        }
        _5881 = _5878;
        _5882 = _5879;
        _5883 = _5880;
    }
    else
    {
        _5881 = _176;
        _5882 = _175;
        _5883 = _174;
    }
    SV_TARGET.x = _5883;
    SV_TARGET.y = _5882;
    SV_TARGET.z = _5881;
    SV_TARGET.w = _173.w;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}
