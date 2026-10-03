// aka glas_eye_ps / glass_eyes_ps

cbuffer cb0_buf : register(b0)
{
    uint2 cb0_m0 : packoffset(c0);
    uint2 cb0_m1 : packoffset(c0.z);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m[55] : packoffset(c0);
};

cbuffer cb2_buf : register(b2)
{
    uint4 cb2_m[42] : packoffset(c0);
};

cbuffer cb4_buf : register(b4)
{
    uint4 cb4_m0 : packoffset(c0);
    float4 cb4_m1 : packoffset(c1);
    uint4 cb4_m2 : packoffset(c2);
    uint4 cb4_m3 : packoffset(c3);
    uint4 cb4_m4 : packoffset(c4);
    uint4 cb4_m5 : packoffset(c5);
    uint4 cb4_m6 : packoffset(c6);
    uint4 cb4_m7 : packoffset(c7);
    float4 cb4_m8 : packoffset(c8);
};

SamplerState s0 : register(s0);
SamplerState s3 : register(s3);
SamplerState s5 : register(s5);
SamplerState s7 : register(s7);
SamplerState s9 : register(s9);
SamplerState s10 : register(s10);
SamplerState s12 : register(s12);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t3 : register(t3);
TextureCube<float4> t5 : register(t5);
TextureCube<float4> t9 : register(t9);
TextureCube<float4> t10 : register(t10);
TextureCube<float4> t12 : register(t12);
Texture2D<float4> t16 : register(t16); // SSS
Texture2D<float4> t19 : register(t19);
Texture2D<float4> t32 : register(t32); // SSS setup

#include "./Includes/Common.hlsl"

static float4 gl_FragCoord;
static float4 TEXCOORD;
static float4 TEXCOORD1;
static float4 TEXCOORD2;
static float2 TEXCOORD3;
static float4 NORMAL1;
static float4 NORMAL2;
static float4 POSITION1;
static float4 COLOR;
static float4 COLOR1;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float4 v0        : SV_POSITION0; // unused
    float3 v1        : NORMAL0;      // unused
    float4 TEXCOORD  : TEXCOORD0;
    float4 TEXCOORD1 : TEXCOORD1;
    float4 TEXCOORD2 : TEXCOORD2;
    float2 TEXCOORD3 : TEXCOORD3;
    float4 NORMAL1   : NORMAL1;
    float4 NORMAL2   : NORMAL2;
    float4 POSITION1 : POSITION1;
    float4 COLOR     : COLOR0;
    float4 COLOR1    : COLOR1;
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

uint spvBitfieldUExtract(uint Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

uint2 spvBitfieldUExtract(uint2 Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

uint3 spvBitfieldUExtract(uint3 Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

uint4 spvBitfieldUExtract(uint4 Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

int spvBitfieldSExtract(int Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int2 spvBitfieldSExtract(int2 Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int2 Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int3 spvBitfieldSExtract(int3 Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int3 Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int4 spvBitfieldSExtract(int4 Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int4 Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

float dp2_f32(float2 a, float2 b)
{
    precise float _200 = a.x * b.x;
    return mad(a.y, b.y, _200);
}

float dp3_f32(float3 a, float3 b)
{
    precise float _186 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _186));
}

float dp4_f32(float4 a, float4 b)
{
    precise float _168 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _168)));
}

void frag_main()
{
    uint _211 = cb0_m1.x & 1u;
    float _217 = POSITION1.x * 2500.0f;
    float _218 = POSITION1.y * 1736.111083984375f;
    float _221 = dp2_f32(float2(_217, _218), float2(POSITION1.x, POSITION1.y));
    float _222 = mad(_221, -8.0999998317565768957138061523438e-05f, 8.0999998317565768957138061523438e-05f);
    float _230 = (mad(_221, -8.0999998317565768957138061523438e-05f, 7.999999797903001308441162109375e-05f) > 0.0f) ? (rsqrt(_222) * _222) : POSITION1.z;
    float3 _237 = float3(TEXCOORD1.x, TEXCOORD1.y, TEXCOORD1.z);
    float _239 = rsqrt(dp3_f32(_237, _237));
    float _240 = TEXCOORD1.x * _239;
    float _241 = TEXCOORD1.y * _239;
    float _242 = TEXCOORD1.z * _239;
    float _297;
    if (spvBitfieldUExtract(cb0_m1.x, 9u, 1u) != 0u)
    {
        float _293 = min(dp3_f32(float3(asfloat(cb1_m[26u].x), asfloat(cb1_m[26u].y), asfloat(cb1_m[26u].z)), float3(NORMAL1.x, NORMAL1.y, NORMAL1.z)) + 1.0f, 1.0f);
        float _294 = _293 * _293;
        _297 = min(min(exp2(((t19.Sample(s7, float2(TEXCOORD2.x, TEXCOORD2.y)).x - TEXCOORD2.z) * asfloat(cb1_m[45u].x)) * asfloat(cb2_m[30u].w)), 1.0f), _294 * _294);
    }
    else
    {
        _297 = 1.0f;
    }
    float3 _682 = 0;
    float _684 = 0;
    float3 _693 = 0;
    float _694 = 0;
    float3 _703 = 0;
    float _704 = 0;
    float4 _306 = t3.Sample(s3, float2(TEXCOORD.z, TEXCOORD.w));
    float _308 = _306.w * 0.89999997615814208984375f;
    float2 _313 = float2(TEXCOORD3.x, TEXCOORD3.y);
    if (dp2_f32(_313, _313) > 0.959999978542327880859375f)
    {
        float3 _325 = float3(NORMAL1.x, NORMAL1.y, NORMAL1.z);
        float _327 = rsqrt(dp3_f32(_325, _325));
        float3 _340 = float3(asfloat(cb1_m[42u].x), asfloat(cb1_m[42u].y), asfloat(cb1_m[42u].z));
        float3 _341 = float3(NORMAL1.x * _327, NORMAL1.y * _327, NORMAL1.z * _327);
        float3 _351 = float3(asfloat(cb1_m[43u].x), asfloat(cb1_m[43u].y), asfloat(cb1_m[43u].z));
        float3 _361 = float3(asfloat(cb1_m[44u].x), asfloat(cb1_m[44u].y), asfloat(cb1_m[44u].z));
        float3 _365 = float3(dp3_f32(_340, _341), dp3_f32(_351, _341), dp3_f32(_361, _341));
        float4 _368 = t9.SampleLevel(s9, _365, 0.0f);
        float4 _373 = t9.SampleLevel(s9, _365, 1.0f);
        float _374 = _373.x;
        float _375 = _373.y;
        float _376 = _373.z;
        float _380 = mad(_368.x - _374, _297, _374);
        float _381 = mad(_368.y - _375, _297, _375);
        float _382 = mad(_368.z - _376, _297, _376);
        bool _383 = _211 == 1u;
        float _389 = asfloat(cb1_m[14u].x);
        float _390 = asfloat(cb1_m[14u].y);
        float _391 = asfloat(cb1_m[14u].z);
        float _418 = asfloat(cb1_m[15u].x) + (_383 ? mad(_380 * _389, cb4_m1.w, COLOR.w) : mad(_380, _389, COLOR.w));
        float _419 = asfloat(cb1_m[15u].y) + (_383 ? mad(_390 * _381, cb4_m1.w, COLOR.w) : mad(_390, _381, COLOR.w));
        float _420 = asfloat(cb1_m[15u].z) + (_383 ? mad(_391 * _382, cb4_m1.w, COLOR.w) : mad(_391, _382, COLOR.w));
        bool _428 = spvBitfieldUExtract(cb0_m0.y, 12u, 1u) == 1u;
        bool _429 = spvBitfieldUExtract(cb0_m0.x, 10u, 1u) == 1u;
        float _449 = clamp((mad(dp3_f32(_341, float3(asfloat(cb1_m[33u].x), asfloat(cb1_m[33u].y), asfloat(cb1_m[33u].z))), 0.5f, 0.5f) - asfloat(cb1_m[36u].x)) * asfloat(cb1_m[36u].y), 0.0f, 1.0f);
        float _455 = asfloat(cb1_m[34u].x);
        float _456 = asfloat(cb1_m[34u].y);
        float _457 = asfloat(cb1_m[34u].z);
        float _474 = asfloat(cb1_m[36u].z);
        float _478 = _428 ? mad(mad(_449, asfloat(cb1_m[35u].x) - _455, _455), _474, _418) : _418;
        float _479 = _428 ? mad(_474, mad(asfloat(cb1_m[35u].y) - _456, _449, _456), _419) : _419;
        float _480 = _428 ? mad(_474, mad(asfloat(cb1_m[35u].z) - _457, _449, _457), _420) : _420;
        float2 sss_uv = float2(gl_FragCoord.x * asfloat(cb1_m[54u].x), gl_FragCoord.y * asfloat(cb1_m[54u].y));
        #if CUSTOM_SSS_EYEDODGE == 0
            float4 _497 = t16.Sample(s12, sss_uv);
        #else
            float4 sss = t16.Sample(s12, sss_uv);
            float4 sss_setup = t32.Sample(s12, sss_uv);
            float3 sss_blended = lerp(sss.rgb, sss_setup.rgb, GS.SSSEyeDodge);
            float4 _497 = float4(sss_blended, sss_setup.a); // alpha is discarded anyways
        #endif
        float _507 = asfloat(cb2_m[41u].x);
        float _508 = mad(_497.x - _478, _507, _478);
        float _509 = mad(_507, _497.y - _479, _479);
        float _510 = mad(_507, _497.z - _480, _480);
        float _515 = cb4_m1.y * cb4_m1.x;
        float4 _536 = t0.SampleGrad(s0, float2(TEXCOORD.x, TEXCOORD.y), float2(ddx_coarse(TEXCOORD.x), ddx_coarse(TEXCOORD.y)), float2(ddy_coarse(TEXCOORD.x), ddy_coarse(TEXCOORD.y)));
        float _548 = dp3_f32(float3(_240, _241, _242), float3(NORMAL2.x, NORMAL2.y, NORMAL2.z));
        float _549 = _548 + _548;
        float3 _556 = float3((NORMAL2.x * _549) - _240, (NORMAL2.y * _549) - _241, (NORMAL2.z * _549) - _242);
        float3 _562 = float3(dp3_f32(_340, _556), dp3_f32(_351, _556), dp3_f32(_361, _556));
        float4 _564 = t10.SampleLevel(s10, _562, 0.0f);
        float4 _570 = t12.SampleLevel(s10, _562, 0.0f);
        float _571 = _570.x;
        float _572 = _570.y;
        float _573 = _570.z;
        float _588 = mad(_564.x - _571, _297, _571) * asfloat(cb1_m[16u].x);
        float _589 = asfloat(cb1_m[16u].y) * mad(_297, _564.y - _572, _572);
        float _590 = asfloat(cb1_m[16u].z) * mad(_297, _564.z - _573, _573);
        float _603 = mad(_308, _478, _588 + (((_536.x * (_383 ? (_508 * _515) : _508)) - _588) * 0.939999997615814208984375f));
        float _604 = mad(_308, _479, (((_536.y * (_383 ? (_515 * _509) : _509)) - _589) * 0.939999997615814208984375f) + _589);
        float _605 = mad(_308, _480, (((_536.z * (_383 ? (_515 * _510) : _510)) - _590) * 0.939999997615814208984375f) + _590);
        float _607 = dp3_f32(float3(_603, _604, _605), float3(0.29890000820159912109375f, 0.586600005626678466796875f, 0.11450000107288360595703125f));
        float _613 = asfloat(cb1_m[31u].x);
        float _614 = asfloat(cb1_m[31u].y);
        float _615 = asfloat(cb1_m[31u].z);
        float _635 = asfloat(cb1_m[32u].w);
        SV_Target.x = _429 ? max(mad(mad(_607, asfloat(cb1_m[32u].x) - _613, _613) - _603, _635, _603), 0.0f) : mad(COLOR1.x - _603, TEXCOORD2.w, _603);
        SV_Target.y = _429 ? max(mad(_635, mad(_607, asfloat(cb1_m[32u].y) - _614, _614) - _604, _604), 0.0f) : mad(TEXCOORD2.w, COLOR1.y - _604, _604);
        SV_Target.z = _429 ? max(mad(_635, mad(_607, asfloat(cb1_m[32u].z) - _615, _615) - _605, _605), 0.0f) : mad(TEXCOORD2.w, COLOR1.z - _605, _605);
        SV_Target.w = 1.0f;
        return;
    }
    else
    {
        float _664 = POSITION1.x * 5000.0f;
        float _665 = POSITION1.y * 3472.22216796875f;
        float _666 = _230 * 24691.357421875f;
        float3 _667 = float3(_664, _665, _666);
        float _669 = rsqrt(dp3_f32(_667, _667));
        _682 = float3(asfloat(cb2_m[16u].x), asfloat(cb2_m[16u].y), asfloat(cb2_m[16u].z));
        float3 _683 = float3(_669 * _664, _669 * _665, _669 * _666);
        _684 = dp3_f32(_682, _683);
        _693 = float3(asfloat(cb2_m[17u].x), asfloat(cb2_m[17u].y), asfloat(cb2_m[17u].z));
        _694 = dp3_f32(_693, _683);
        _703 = float3(asfloat(cb2_m[18u].x), asfloat(cb2_m[18u].y), asfloat(cb2_m[18u].z));
        _704 = dp3_f32(_703, _683);
    }
    float _707 = dp3_f32(float3(_240, _241, _242), float3(_684, _694, _704));
    float _708 = clamp(_707, 0.0f, 1.0f);
    float _713 = mad(_708, 0.68965518474578857421875f, -sqrt(mad(_708 * _708, 0.475624263286590576171875f, 0.524375736713409423828125f)));
    float3 _732 = float3((_684 * _713) + (_240 * (-0.68965518474578857421875f)), (_694 * _713) + (_241 * (-0.68965518474578857421875f)), (_704 * _713) + (_242 * (-0.68965518474578857421875f)));
    float _733 = dp3_f32(float3(asfloat(cb2_m[19u].x), asfloat(cb2_m[19u].y), asfloat(cb2_m[19u].z)), _732);
    float _743 = dp3_f32(float3(asfloat(cb2_m[20u].x), asfloat(cb2_m[20u].y), asfloat(cb2_m[20u].z)), _732);
    float _753 = dp3_f32(float3(asfloat(cb2_m[21u].x), asfloat(cb2_m[21u].y), asfloat(cb2_m[21u].z)), _732);
    float3 _757 = float3(_733 * 2500.0f, _743 * 1736.111083984375f, _753 * 40000.0f);
    float3 _758 = float3(_733, _743, _753);
    float _759 = dp3_f32(_757, _758);
    float _761 = dp3_f32(_757, float3(POSITION1.x, POSITION1.y, _230));
    float _762 = _761 + _761;
    float _766 = POSITION1.w * (-1.0f);
    float _773 = (_762 * _762) + ((_759 * dp4_f32(float4(_217, _218, _230 * 40000.0f, _766), float4(POSITION1.x, POSITION1.y, _230, POSITION1.w))) * (-4.0f));
    if ((_773 - 9.9999999747524270787835121154785e-07f) < 0.0f)
    {
        SV_Target.x = 0.0f;
        SV_Target.y = 0.0f;
        SV_Target.z = 1.0f;
        SV_Target.w = 1.0f;
        return;
    }
    float _785 = mad(_761, -2.0f, sqrt(_773)) * (0.5f / _759);
    float _786 = mad(_733, _785, POSITION1.x);
    float _787 = mad(_743, _785, POSITION1.y);
    float _788 = mad(_753, _785, _230);
    float _789 = POSITION1.w + 0.0f;
    if (_788 > 0.0f)
    {
        SV_Target.x = 0.0f;
        SV_Target.y = 1.0f;
        SV_Target.z = 0.0f;
        SV_Target.w = 1.0f;
        return;
    }
    float _797 = 1.0f - _708;
    float _798 = _797 * _797;
    float _804 = asfloat(cb2_m[32u].w);
    float _806 = min(mad(_797 * (_798 * _798), 0.966264069080352783203125f, 0.0337359420955181121826171875f), _804 + _804);
    float _807 = _707 + _707;
    float3 _820 = float3(asfloat(cb1_m[42u].x), asfloat(cb1_m[42u].y), asfloat(cb1_m[42u].z));
    float3 _824 = float3((_684 * _807) - _240, (_694 * _807) - _241, (_704 * _807) - _242);
    float3 _834 = float3(asfloat(cb1_m[43u].x), asfloat(cb1_m[43u].y), asfloat(cb1_m[43u].z));
    float3 _844 = float3(asfloat(cb1_m[44u].x), asfloat(cb1_m[44u].y), asfloat(cb1_m[44u].z));
    float4 _850 = t5.SampleLevel(s5, float3(dp3_f32(_820, _824), dp3_f32(_834, _824), dp3_f32(_844, _824)), 0.0f);
    float _851 = _850.x;
    float _852 = _850.y;
    float _853 = _850.z;
    float4 _871 = t0.SampleLevel(s0, float2(mad(mad(_786, 10.0f, 0.5f), -1.0f, 1.0f), mad(mad(_787, 8.33333301544189453125f, 0.5f), 1.0f, 0.0f)), 0.0f);
    float _872 = _871.x;
    float _873 = _871.y;
    float _874 = _871.z;
    float _875 = _230 + 0.0040000001899898052215576171875f;
    float3 _879 = float3(_733 * 10000.0f, _743 * 6944.4443359375f, _753 * 15625.0f);
    float _880 = dp3_f32(_879, _758);
    float _882 = dp3_f32(_879, float3(POSITION1.x, POSITION1.y, _875));
    float _883 = _882 + _882;
    float _893 = (_883 * _883) + ((_880 * dp4_f32(float4(POSITION1.x * 10000.0f, POSITION1.y * 6944.4443359375f, _875 * 15625.0f, _766), float4(POSITION1.x, POSITION1.y, _875, POSITION1.w))) * (-4.0f));
    float _901 = ((_893 - 9.9999999747524270787835121154785e-07f) < 0.0f) ? 999999.0f : (mad(_882, -2.0f, -sqrt(_893)) * (0.5f / _880));
    float _902 = abs(_785);
    float _966;
    float _967;
    float _968;
    float _969;
    float _970;
    float _971;
    float _972;
    if ((_901 - _902) < 0.0f)
    {
        float _911 = mad(_733, _901, POSITION1.x) * 20000.0f;
        float _912 = mad(_743, _901, POSITION1.y) * 13888.888671875f;
        float _913 = mad(_753, _901, _875) * 31250.0f;
        float3 _914 = float3(_911, _912, _913);
        float _916 = rsqrt(dp3_f32(_914, _914));
        float _917 = _916 * _911;
        float _918 = _916 * _912;
        float _919 = _916 * _913;
        float _925 = dp3_f32(float3(-_733, -_743, -_753), float3(_917, _918, _919));
        float _926 = _925 + _925;
        float3 _930 = float3(mad(_917, _926, _733), mad(_926, _918, _743), mad(_926, _919, _753));
        float3 _934 = float3(dp3_f32(_682, _930), dp3_f32(_693, _930), dp3_f32(_703, _930));
        float4 _942 = t10.SampleLevel(s9, float3(dp3_f32(_820, _934), dp3_f32(_834, _934), dp3_f32(_844, _934)), 0.0f);
        _966 = (asfloat(cb1_m[16u].z) * (_874 * _942.z)) * 0.100000001490116119384765625f;
        _967 = (asfloat(cb1_m[16u].y) * (_873 * _942.y)) * 0.100000001490116119384765625f;
        _968 = ((_872 * _942.x) * asfloat(cb1_m[16u].x)) * 0.100000001490116119384765625f;
        _969 = _901;
        _970 = _874 * 0.75f;
        _971 = _873 * 0.75f;
        _972 = _872 * 0.75f;
    }
    else
    {
        _966 = 0.0f;
        _967 = 0.0f;
        _968 = 0.0f;
        _969 = _902;
        _970 = _874;
        _971 = _873;
        _972 = _872;
    }
    float _973 = _786 * (-5000.0f);
    float _974 = _787 * (-3472.22216796875f);
    float _975 = _788 * (-80000.0f);
    float3 _976 = float3(_973, _974, _975);
    float _978 = rsqrt(dp3_f32(_976, _976));
    float _979 = _978 * _973;
    float _980 = _978 * _974;
    float _981 = _978 * _975;
    float3 _982 = float3(_979, _980, _981);
    float3 _986 = float3(dp3_f32(_682, _982), dp3_f32(_693, _982), dp3_f32(_703, _982));
    float3 _992 = float3(dp3_f32(_820, _986), dp3_f32(_834, _986), dp3_f32(_844, _986));
    float4 _994 = t9.SampleLevel(s9, _992, 0.0f);
    float4 _999 = t9.SampleLevel(s9, _992, 1.0f);
    float _1000 = _999.x;
    float _1001 = _999.y;
    float _1002 = _999.z;
    float _1006 = mad(_994.x - _1000, _297, _1000);
    float _1007 = mad(_994.y - _1001, _297, _1001);
    float _1008 = mad(_994.z - _1002, _297, _1002);
    bool _1009 = _211 == 1u;
    float _1015 = asfloat(cb1_m[14u].x);
    float _1016 = asfloat(cb1_m[14u].y);
    float _1017 = asfloat(cb1_m[14u].z);
    float _1043 = asfloat(cb1_m[15u].x) + (_1009 ? mad(_1006 * _1015, cb4_m1.w, COLOR.w) : mad(_1006, _1015, COLOR.w));
    float _1044 = asfloat(cb1_m[15u].y) + (_1009 ? mad(cb4_m1.w, _1016 * _1007, COLOR.w) : mad(_1016, _1007, COLOR.w));
    float _1045 = asfloat(cb1_m[15u].z) + (_1009 ? mad(cb4_m1.w, _1017 * _1008, COLOR.w) : mad(_1017, _1008, COLOR.w));
    float _1051 = _1009 ? (_1043 * cb4_m1.y) : _1043;
    float _1052 = _1009 ? (cb4_m1.y * _1044) : _1044;
    float _1053 = _1009 ? (cb4_m1.y * _1045) : _1045;
    bool _1060 = spvBitfieldUExtract(cb0_m0.y, 12u, 1u) == 1u;
    bool _1061 = spvBitfieldUExtract(cb0_m0.x, 10u, 1u) == 1u;
    float _1081 = clamp((mad(dp3_f32(_986, float3(asfloat(cb1_m[33u].x), asfloat(cb1_m[33u].y), asfloat(cb1_m[33u].z))), 0.5f, 0.5f) - asfloat(cb1_m[36u].x)) * asfloat(cb1_m[36u].y), 0.0f, 1.0f);
    float _1087 = asfloat(cb1_m[34u].x);
    float _1088 = asfloat(cb1_m[34u].y);
    float _1089 = asfloat(cb1_m[34u].z);
    float _1106 = asfloat(cb1_m[36u].z);
    float _1110 = _1060 ? mad(mad(_1081, asfloat(cb1_m[35u].x) - _1087, _1087), _1106, _1051) : _1051;
    float _1111 = _1060 ? mad(_1106, mad(_1081, asfloat(cb1_m[35u].y) - _1088, _1088), _1052) : _1052;
    float _1112 = _1060 ? mad(_1106, mad(_1081, asfloat(cb1_m[35u].z) - _1089, _1089), _1053) : _1053;
    float _1113 = _1110 * 0.949999988079071044921875f;
    float _1114 = _1111 * 0.949999988079071044921875f;
    float _1115 = _1112 * 0.949999988079071044921875f;
    float _1120 = cb4_m8.y * cb4_m1.x;
    float _1127 = _972 * (_1009 ? (_1120 * _1113) : _1113);
    float _1128 = _971 * (_1009 ? (_1120 * _1114) : _1114);
    float _1129 = _970 * (_1009 ? (_1120 * _1115) : _1115);
    float _1141 = mad(asfloat(cb2_m[28u].x), -2.0f, 2.0f) * 61.31453704833984375f;
    float _1142 = mad(asfloat(cb2_m[28u].y), -2.0f, 2.0f) * 61.31453704833984375f;
    float _1143 = mad(asfloat(cb2_m[28u].z), -2.0f, 2.0f) * 61.31453704833984375f;
    float _1150 = exp2(-(_1141 * _969));
    float _1151 = exp2(-(_1142 * _969));
    float _1152 = exp2(-(_1143 * _969));
    float _1153 = _1127 * _1150;
    float _1154 = _1151 * _1128;
    float _1155 = _1152 * _1129;
    float _1161 = asfloat(cb1_m[16u].x);
    float _1162 = asfloat(cb1_m[16u].y);
    float _1163 = asfloat(cb1_m[16u].z);
    float _1164 = max(mad(_851 * _851, 7.0f, -0.5f), 0.0f) * _1161;
    float _1165 = _1162 * max(mad(_852 * _852, 7.0f, -0.5f), 0.0f);
    float _1166 = _1163 * max(mad(_853 * _853, 7.0f, -0.5f), 0.0f);
    float _1176 = mad(_308, _1110, _1153 + (_806 * (_1164 - _1153)));
    float _1177 = mad(_308, _1111, (_806 * (_1165 - _1154)) + _1154);
    float _1178 = mad(_308, _1112, (_806 * (_1166 - _1155)) + _1155);
    float _1183 = dp3_f32(float3(-_733, -_743, -_753), _982);
    float _1184 = _1183 + _1183;
    float _1185 = mad(_979, _1184, _733);
    float _1187 = mad(_1184, _980, _743);
    float _1189 = mad(_1184, _981, _753);
    float3 _1194 = float3(_1185 * 2500.0f, _1187 * 1736.111083984375f, _1189 * 12345.6787109375f);
    float _1196 = dp3_f32(_1194, float3(_1185, _1187, _1189));
    float _1198 = dp3_f32(_1194, float3(_786, _787, _788));
    float _1199 = _1198 + _1198;
    float _1210 = (_1199 * _1199) + ((_1196 * dp4_f32(float4(_786 * 2500.0f, _787 * 1736.111083984375f, _788 * 12345.6787109375f, _789 * (-1.0f)), float4(_786, _787, _788, _789))) * (-4.0f));
    float _1214 = dp3_f32(float3(_1176, _1177, _1178), float3(0.29890000820159912109375f, 0.586600005626678466796875f, 0.11450000107288360595703125f));
    float _1220 = asfloat(cb1_m[31u].x);
    float _1221 = asfloat(cb1_m[31u].y);
    float _1222 = asfloat(cb1_m[31u].z);
    float _1231 = asfloat(cb1_m[32u].x) - _1220;
    float _1232 = asfloat(cb1_m[32u].y) - _1221;
    float _1233 = asfloat(cb1_m[32u].z) - _1222;
    float _1242 = asfloat(cb1_m[32u].w);
    float _1263 = _1061 ? max(mad(mad(_1214, _1231, _1220) - _1176, _1242, _1176), 0.0f) : mad(COLOR1.x - _1176, TEXCOORD2.w, _1176);
    float _1264 = _1061 ? max(mad(mad(_1232, _1214, _1221) - _1177, _1242, _1177), 0.0f) : mad(COLOR1.y - _1177, TEXCOORD2.w, _1177);
    float _1265 = _1061 ? max(mad(mad(_1233, _1214, _1222) - _1178, _1242, _1178), 0.0f) : mad(COLOR1.z - _1178, TEXCOORD2.w, _1178);
    if ((_1210 - 9.9999999747524270787835121154785e-07f) < 0.0f)
    {
        SV_Target.x = _1263;
        SV_Target.y = _1264;
        SV_Target.z = _1265;
        SV_Target.w = 1.0f;
        return;
    }
    float _1275 = mad(_1198, -2.0f, sqrt(_1210)) * (0.5f / _1196);
    float _1278 = mad(_1275, _1189, _788);
    if (_1278 < 0.0f)
    {
        SV_Target.x = _1263;
        SV_Target.y = _1264;
        SV_Target.z = _1265;
        SV_Target.w = 1.0f;
        return;
    }
    float _1286 = mad(_1185, _1275, _786) * 5000.0f;
    float _1287 = mad(_1275, _1187, _787) * 3472.22216796875f;
    float _1288 = _1278 * 24691.357421875f;
    float3 _1289 = float3(_1286, _1287, _1288);
    float _1291 = rsqrt(dp3_f32(_1289, _1289));
    float _1292 = _1286 * _1291;
    float _1293 = _1287 * _1291;
    float _1294 = _1288 * _1291;
    float _1301 = clamp(dp3_f32(float3(-_1185, -_1187, -_1189), float3(-_1292, -_1293, -_1294)), 0.0f, 1.0f);
    float _1302 = _1301 * _1301;
    if (mad(_1302, 2.10249996185302734375f, -1.10250091552734375f) < 0.0f)
    {
        float _1307 = _1176 * 0.85000002384185791015625f;
        float _1308 = _1177 * 0.85000002384185791015625f;
        float _1309 = _1178 * 0.85000002384185791015625f;
        float _1311 = dp3_f32(float3(_1307, _1308, _1309), float3(0.29890000820159912109375f, 0.586600005626678466796875f, 0.11450000107288360595703125f));
        SV_Target.x = _1061 ? max(_1307 + (_1242 * mad(_1176, -0.85000002384185791015625f, mad(_1231, _1311, _1220))), 0.0f) : (_1307 + (TEXCOORD2.w * mad(_1176, -0.85000002384185791015625f, COLOR1.x)));
        SV_Target.y = _1061 ? max((mad(_1177, -0.85000002384185791015625f, mad(_1232, _1311, _1221)) * _1242) + _1308, 0.0f) : ((mad(_1177, -0.85000002384185791015625f, COLOR1.y) * TEXCOORD2.w) + _1308);
        SV_Target.z = _1061 ? max((mad(_1178, -0.85000002384185791015625f, mad(_1233, _1311, _1222)) * _1242) + _1309, 0.0f) : ((mad(_1178, -0.85000002384185791015625f, COLOR1.z) * TEXCOORD2.w) + _1309);
        SV_Target.w = 1.0f;
        return;
    }
    float _1343 = mad(_1302, 2.10249996185302734375f, -1.10249996185302734375f);
    float3 _1344 = float3(_872, _873, _874);
    float _1347 = clamp(mad(dp3_f32(_1344, _1344), 0.20000000298023223876953125f, -0.0199999995529651641845703125f), 0.0f, 1.0f);
    float _1351 = (_1301 * 1.4500000476837158203125f) - (_1343 * rsqrt(_1343));
    float3 _1361 = float3((_1185 * 1.4500000476837158203125f) - (_1292 * _1351), (_1187 * 1.4500000476837158203125f) - (_1293 * _1351), (_1189 * 1.4500000476837158203125f) - (_1294 * _1351));
    float3 _1365 = float3(dp3_f32(_682, _1361), dp3_f32(_693, _1361), dp3_f32(_703, _1361));
    float3 _1371 = float3(dp3_f32(_820, _1365), dp3_f32(_834, _1365), dp3_f32(_844, _1365));
    float4 _1373 = t10.SampleLevel(s10, _1371, 0.0f);
    float4 _1379 = t12.SampleLevel(s10, _1371, 0.0f);
    float _1380 = _1379.x;
    float _1381 = _1379.y;
    float _1382 = _1379.z;
    float _1413 = _1150 * (_1127 + (_1347 * ((_872 * (_1161 * mad(_1373.x - _1380, _297, _1380))) * exp2(-(_1141 * _1275)))));
    float _1414 = (((exp2(-(_1142 * _1275)) * (_873 * (mad(_1373.y - _1381, _297, _1381) * _1162))) * _1347) + _1128) * _1151;
    float _1415 = (((exp2(-(_1143 * _1275)) * (_874 * (mad(_1373.z - _1382, _297, _1382) * _1163))) * _1347) + _1129) * _1152;
    float _1428 = mad(_308, _1110, _968 + (_1413 + (_806 * (_1164 - _1413))));
    float _1429 = mad(_308, _1111, ((_806 * (_1165 - _1414)) + _1414) + _967);
    float _1430 = mad(_308, _1112, ((_806 * (_1166 - _1415)) + _1415) + _966);
    float _1432 = dp3_f32(float3(_1428, _1429, _1430), float3(0.29890000820159912109375f, 0.586600005626678466796875f, 0.11450000107288360595703125f));
    SV_Target.x = _1061 ? max(mad(_1242, mad(_1231, _1432, _1220) - _1428, _1428), 0.0f) : mad(TEXCOORD2.w, COLOR1.x - _1428, _1428);
    SV_Target.y = _1061 ? max(mad(mad(_1232, _1432, _1221) - _1429, _1242, _1429), 0.0f) : mad(COLOR1.y - _1429, TEXCOORD2.w, _1429);
    SV_Target.z = _1061 ? max(mad(mad(_1233, _1432, _1222) - _1430, _1242, _1430), 0.0f) : mad(COLOR1.z - _1430, TEXCOORD2.w, _1430);
    SV_Target.w = 1.0f;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    TEXCOORD = stage_input.TEXCOORD;
    TEXCOORD1 = stage_input.TEXCOORD1;
    TEXCOORD2 = stage_input.TEXCOORD2;
    TEXCOORD3 = stage_input.TEXCOORD3;
    NORMAL1 = stage_input.NORMAL1;
    NORMAL2 = stage_input.NORMAL2;
    POSITION1 = stage_input.POSITION1;
    COLOR = stage_input.COLOR;
    COLOR1 = stage_input.COLOR1;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}

/*
// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:11 2025

cbuffer Shader : register(b0)
{
  uint4 g_shader_flags : packoffset(c0);
}

cbuffer Scene : register(b1)
{
  float4 g_irradiance_r_transforms[4] : packoffset(c0);
  float4 g_irradiance_g_transforms[4] : packoffset(c4);
  float4 g_irradiance_b_transforms[4] : packoffset(c8);
  float4 g_light_env_stage_diffuse : packoffset(c12);
  float4 g_light_env_stage_specular : packoffset(c13);
  float4 g_light_env_chara_diffuse : packoffset(c14);
  float4 g_light_env_chara_ambient : packoffset(c15);
  float4 g_light_env_chara_specular : packoffset(c16);
  float4 g_light_env_reflect_diffuse : packoffset(c17);
  float4 g_light_env_reflect_ambient : packoffset(c18);
  float4 g_light_env_reflect_specular : packoffset(c19);
  float4 g_light_env_proj_diffuse : packoffset(c20);
  float4 g_light_env_proj_specular : packoffset(c21);
  float4 g_light_env_proj_position : packoffset(c22);
  float4 g_light_stage_dir : packoffset(c23);
  float4 g_light_stage_diff : packoffset(c24);
  float4 g_light_stage_spec : packoffset(c25);
  float4 g_light_chara_dir : packoffset(c26);
  float4 g_light_chara_spec : packoffset(c27);
  float4 g_light_chara_luce : packoffset(c28);
  float4 g_light_chara_back : packoffset(c29);
  float4 g_light_face_diff : packoffset(c30);
  float4 g_chara_color0 : packoffset(c31);
  float4 g_chara_color1 : packoffset(c32);
  float4 g_chara_f_dir : packoffset(c33);
  float4 g_chara_f_ambient : packoffset(c34);
  float4 g_chara_f_diffuse : packoffset(c35);
  float4 g_chara_tc_param : packoffset(c36);
  float4 g_fog_depth_color : packoffset(c37);
  float4 g_fog_height_params : packoffset(c38);
  float4 g_fog_height_color : packoffset(c39);
  float4 g_fog_bump_params : packoffset(c40);
  float4 g_fog_state_params : packoffset(c41);
  float4 g_normal_tangent_transforms[3] : packoffset(c42);
  float4 g_esm_param : packoffset(c45);
  float4 g_self_shadow_receivers[6] : packoffset(c46);
  float4 g_shadow_ambient : packoffset(c52);
  float4 g_shadow_ambient1 : packoffset(c53);
  float4 g_framebuffer_size : packoffset(c54);
  float4 g_light_reflect_dir : packoffset(c55);
  float4 g_clip_plane : packoffset(c56);
  float4 g_npr_cloth_spec_color : packoffset(c57);
  float4 g_view[3] : packoffset(c58);
  float4 g_view_inverse[3] : packoffset(c61);
  float4 g_projection_view[4] : packoffset(c64);
  float4 g_view_position : packoffset(c68);
  float4 g_light_projection[4] : packoffset(c69);
  float4 g_light_projection_depth[4] : packoffset(c73);
  float4 g_foward_z_projection_row2 : packoffset(c77);
}

cbuffer Batch : register(b2)
{
  float4 g_transforms[4] : packoffset(c0);
  float4 g_worlds[3] : packoffset(c4);
  float4 g_worlds_invtrans[3] : packoffset(c7);
  float4 g_worldview[3] : packoffset(c10);
  float4 g_worldview_inverse[3] : packoffset(c13);
  float4 g_joint[3] : packoffset(c16);
  float4 g_joint_inverse[3] : packoffset(c19);
  float4 g_texcoord_transforms[4] : packoffset(c22);
  float4 g_blend_color : packoffset(c26);
  float4 g_offset_color : packoffset(c27);
  float4 g_material_state_diffuse : packoffset(c28);
  float4 g_material_state_ambient : packoffset(c29);
  float4 g_material_state_emission : packoffset(c30);
  float4 g_material_state_shininess : packoffset(c31);
  float4 g_material_state_specular : packoffset(c32);
  float4 g_fresnel_coefficients : packoffset(c33);
  float4 g_texture_color_coefficients : packoffset(c34);
  float4 g_texture_color_offset : packoffset(c35);
  float4 g_texture_specular_coefficients : packoffset(c36);
  float4 g_texture_specular_offset : packoffset(c37);
  float4 g_shininess : packoffset(c38);
  float4 g_max_alpha : packoffset(c39);
  float4 g_morph_weight : packoffset(c40);
  float4 g_sss_param : packoffset(c41);
  float4 g_bump_depth : packoffset(c42);
  float4 g_intensity : packoffset(c43);
  float4 g_reflect_uv_scale : packoffset(c44);
  float4 g_shadow_position[2] : packoffset(c45);
  float4 g_shadow_direction[2] : packoffset(c47);
  float4 g_shadow_param : packoffset(c49);
  uint4 g_skip_flags : packoffset(c50);
}

cbuffer NPRParam : register(b4)
{
  uint4 g_npr_flags : packoffset(c0);
  float4 g_npr_param[8] : packoffset(c1);
  uint4 g_npr_dbg_flags : packoffset(c9);
  float4 g_npr_dbg_param[4] : packoffset(c10);
}

SamplerState g_samplers_0__s : register(s0);
SamplerState g_samplers_3__s : register(s3);
SamplerState g_envmap_sampler_s : register(s5);
SamplerState g_shadow_depth_sampler_s : register(s7);
SamplerState g_ibl_samplers_0__s : register(s9);
SamplerState g_ibl_samplers_1__s : register(s10);
SamplerState g_sss_sampler_s : register(s12);
Texture2D<float4> g_textures_0_ : register(t0);
Texture2D<float4> g_textures_3_ : register(t3);
TextureCube<float4> g_envmap_texture : register(t5);
TextureCube<float4> g_ibl_textures_0_ : register(t9);
TextureCube<float4> g_ibl_textures_1_ : register(t10);
TextureCube<float4> g_ibl_textures_3_ : register(t12);
Texture2D<float4> g_sss_texture : register(t16);
Texture2D<float4> g_shadow_depth_textures_0_ : register(t19);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float3 v1 : NORMAL0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float2 v5 : TEXCOORD3,
  float4 v6 : NORMAL1,
  float4 v7 : NORMAL2,
  float4 v8 : POSITION1,
  float4 v9 : COLOR0,
  float4 v10 : COLOR1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = (int)g_shader_flags.z & 1;
  r0.yz = float2(2500,1736.11108) * v8.xy;
  r0.y = dot(r0.yz, v8.xy);
  r0.yz = r0.yy * float2(-8.09999983e-005,-8.09999983e-005) + float2(8.09999983e-005,7.9999998e-005);
  r0.z = cmp(0 < r0.z);
  r0.w = rsqrt(r0.y);
  r0.y = r0.w * r0.y;
  r1.z = r0.z ? r0.y : v8.z;
  r0.y = dot(v3.xyz, v3.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v3.xyz * r0.yyy;
  if (1 == 0) r1.w = 0; else if (1+9 < 32) {   r1.w = (uint)g_shader_flags.z << (32-(1 + 9)); r1.w = (uint)r1.w >> (32-1);  } else r1.w = (uint)g_shader_flags.z >> 9;
  if (r1.w != 0) {
    r1.w = g_shadow_depth_textures_0_.Sample(g_shadow_depth_sampler_s, v4.xy).x;
    r1.w = -v4.z + r1.w;
    r1.w = g_esm_param.x * r1.w;
    r1.w = g_material_state_emission.w * r1.w;
    r1.w = exp2(r1.w);
    r1.w = min(1, r1.w);
    r2.x = dot(g_light_chara_dir.xyz, v6.xyz);
    r2.x = 1 + r2.x;
    r2.x = min(1, r2.x);
    r2.x = r2.x * r2.x;
    r2.x = r2.x * r2.x;
    r1.w = min(r2.x, r1.w);
    r2.x = r1.w;
  } else {
    r2.x = 1;
    r1.w = 1;
  }
  r2.y = g_textures_3_.Sample(g_samplers_3__s, v2.zw).w;
  r2.y = 0.899999976 * r2.y;
  r2.z = dot(v5.xy, v5.xy);
  r2.z = cmp(0.959999979 < r2.z);
  if (r2.z != 0) {
    r2.z = dot(v6.xyz, v6.xyz);
    r2.z = rsqrt(r2.z);
    r3.xyz = v6.xyz * r2.zzz;
    r4.x = dot(g_normal_tangent_transforms[0].xyz, r3.xyz);
    r4.y = dot(g_normal_tangent_transforms[1].xyz, r3.xyz);
    r4.z = dot(g_normal_tangent_transforms[2].xyz, r3.xyz);
    r5.xyz = g_ibl_textures_0_.SampleLevel(g_ibl_samplers_0__s, r4.xyz, 0).xyz;
    r4.xyz = g_ibl_textures_0_.SampleLevel(g_ibl_samplers_0__s, r4.xyz, 1).xyz;
    r5.xyz = r5.xyz + -r4.xyz;
    r4.xyz = r2.xxx * r5.xyz + r4.xyz;
    r2.z = cmp((int)r0.x == 1);
    r5.xyz = g_light_env_chara_diffuse.xyz * r4.xyz;
    r5.xyz = r5.xyz * g_npr_param[0].www + v9.www;
    r4.xyz = r4.xyz * g_light_env_chara_diffuse.xyz + v9.www;
    r4.xyz = r2.zzz ? r5.xyz : r4.xyz;
    r4.xyz = g_light_env_chara_ambient.xyz + r4.xyz;
    if (1 == 0) r5.x = 0; else if (1+12 < 32) {     r5.x = (uint)g_shader_flags.y << (32-(1 + 12)); r5.x = (uint)r5.x >> (32-1);    } else r5.x = (uint)g_shader_flags.y >> 12;
    if (1 == 0) r5.y = 0; else if (1+10 < 32) {     r5.y = (uint)g_shader_flags.x << (32-(1 + 10)); r5.y = (uint)r5.y >> (32-1);    } else r5.y = (uint)g_shader_flags.x >> 10;
    r5.xy = cmp((int2)r5.xy == int2(1,1));
    r2.w = dot(r3.xyz, g_chara_f_dir.xyz);
    r2.w = r2.w * 0.5 + 0.5;
    r2.w = -g_chara_tc_param.x + r2.w;
    r2.w = saturate(g_chara_tc_param.y * r2.w);
    r3.xyz = g_chara_f_diffuse.xyz + -g_chara_f_ambient.xyz;
    r3.xyz = r2.www * r3.xyz + g_chara_f_ambient.xyz;
    r3.xyz = r3.xyz * g_chara_tc_param.zzz + r4.xyz;
    r3.xyz = r5.xxx ? r3.xyz : r4.xyz;
    r4.xy = g_framebuffer_size.xy * v0.xy;
    r4.xyz = g_sss_texture.Sample(g_sss_sampler_s, r4.xy).xyz;
    r4.xyz = r4.xyz + -r3.xyz;
    r4.xyz = g_sss_param.xxx * r4.xyz + r3.xyz;
    r2.w = g_npr_param[0].y * g_npr_param[0].x;
    r5.xzw = r4.xyz * r2.www;
    r4.xyz = r2.zzz ? r5.xzw : r4.xyz;
    r2.zw = ddx_coarse(v2.xy);
    r5.xz = ddy_coarse(v2.xy);
    r5.xzw = g_textures_0_.SampleGrad(g_samplers_0__s, v2.xy, r2.zzz, r5.xxx).xyz;
    r2.z = dot(r0.yzw, v7.xyz);
    r2.z = r2.z + r2.z;
    r6.xyz = v7.xyz * -r2.zzz + r0.yzw;
    r7.x = dot(g_normal_tangent_transforms[0].xyz, -r6.xyz);
    r7.y = dot(g_normal_tangent_transforms[1].xyz, -r6.xyz);
    r7.z = dot(g_normal_tangent_transforms[2].xyz, -r6.xyz);
    r6.xyz = g_ibl_textures_1_.SampleLevel(g_ibl_samplers_1__s, r7.xyz, 0).xyz;
    r7.xyz = g_ibl_textures_3_.SampleLevel(g_ibl_samplers_1__s, r7.xyz, 0).xyz;
    r6.xyz = -r7.xyz + r6.xyz;
    r6.xyz = r1.www * r6.xyz + r7.xyz;
    r6.xyz = g_light_env_chara_specular.xyz * r6.xyz;
    r4.xyz = r4.xyz * r5.xzw + -r6.xyz;
    r4.xyz = r4.xyz * float3(0.939999998,0.939999998,0.939999998) + r6.xyz;
    r3.xyz = r2.yyy * r3.xyz + r4.xyz;
    r2.z = dot(r3.xyz, float3(0.298900008,0.586600006,0.114500001));
    r4.xyz = g_chara_color1.xyz + -g_chara_color0.xyz;
    r4.xyz = r2.zzz * r4.xyz + g_chara_color0.xyz;
    r4.xyz = r4.xyz + -r3.xyz;
    r4.xyz = g_chara_color1.www * r4.xyz + r3.xyz;
    r4.xyz = max(float3(0,0,0), r4.xyz);
    r5.xzw = v10.xyz + -r3.xyz;
    r3.xyz = v4.www * r5.xzw + r3.xyz;
    o0.xyz = r5.yyy ? r4.xyz : r3.xyz;
    o0.w = 1;
    return;
  } else {
    r1.xy = v8.xy;
    r4.xyz = float3(5000,3472.22217,24691.3574) * r1.xyz;
    r1.x = dot(r4.xyz, r4.xyz);
    r1.x = rsqrt(r1.x);
    r4.xyz = r4.xyz * r1.xxx;
    r3.x = dot(g_joint[0].xyz, r4.xyz);
    r3.y = dot(g_joint[1].xyz, r4.xyz);
    r3.z = dot(g_joint[2].xyz, r4.xyz);
  }
  r1.x = dot(r0.yzw, r3.xyz);
  r1.y = saturate(r1.x);
  r2.z = r1.y * r1.y;
  r2.z = r2.z * 0.475624263 + 0.524375737;
  r2.z = sqrt(r2.z);
  r2.z = r1.y * 0.689655185 + -r2.z;
  r4.xyz = float3(-0.689655185,-0.689655185,-0.689655185) * r0.yzw;
  r4.xyz = r3.xyz * r2.zzz + r4.xyz;
  r5.x = dot(g_joint_inverse[0].xyz, r4.xyz);
  r5.y = dot(g_joint_inverse[1].xyz, r4.xyz);
  r5.z = dot(g_joint_inverse[2].xyz, r4.xyz);
  r4.xyz = float3(2500,1736.11108,40000) * r5.xyz;
  r2.z = dot(r4.xyz, r5.xyz);
  r6.xy = v8.xy;
  r6.z = r1.z;
  r2.w = dot(r4.xyz, r6.xyz);
  r3.w = r2.w + r2.w;
  r6.xyw = v8.xyw;
  r4.xyzw = float4(2500,1736.11108,40000,-1) * r6.xyzw;
  r4.x = dot(r4.xyzw, r6.xyzw);
  r4.x = r4.x * r2.z;
  r3.w = r3.w * r3.w;
  r3.w = r4.x * -4 + r3.w;
  r4.x = -9.99999997e-007 + r3.w;
  r4.x = cmp(r4.x < 0);
  if (r4.x != 0) {
    o0.xyzw = float4(0,0,1,1);
    return;
  }
  r3.w = sqrt(r3.w);
  r2.w = -r2.w * 2 + r3.w;
  r2.z = 0.5 / r2.z;
  r2.z = r2.z * r2.w;
  r4.xyz = r5.xyz * r2.zzz;
  r4.w = 0;
  r4.xyzw = r4.xyzw + r6.xyzw;
  r2.w = cmp(0 < r4.z);
  if (r2.w != 0) {
    o0.xyzw = float4(0,1,0,1);
    return;
  }
  r1.y = 1 + -r1.y;
  r2.w = r1.y * r1.y;
  r2.w = r2.w * r2.w;
  r1.y = r2.w * r1.y;
  r1.y = r1.y * 0.966264069 + 0.0337359421;
  r2.w = g_material_state_specular.w + g_material_state_specular.w;
  r1.y = min(r2.w, r1.y);
  r1.x = r1.x + r1.x;
  r0.yzw = r3.xyz * -r1.xxx + r0.yzw;
  r3.x = dot(g_normal_tangent_transforms[0].xyz, -r0.yzw);
  r3.y = dot(g_normal_tangent_transforms[1].xyz, -r0.yzw);
  r3.z = dot(g_normal_tangent_transforms[2].xyz, -r0.yzw);
  r0.yzw = g_envmap_texture.SampleLevel(g_envmap_sampler_s, r3.xyz, 0).xyz;
  r0.yzw = r0.yzw * r0.yzw;
  r0.yzw = r0.yzw * float3(7,7,7) + float3(-0.5,-0.5,-0.5);
  r0.yzw = max(float3(0,0,0), r0.yzw);
  r3.xy = r4.xy * float2(10,8.33333302) + float2(0.5,0.5);
  r3.xy = r3.xy * float2(-1,1) + float2(1,0);
  r3.xyz = g_textures_0_.SampleLevel(g_samplers_0__s, r3.xy, 0).xyz;
  r6.z = 0.00400000019 + r1.z;
  r7.xyz = float3(10000,6944.44434,15625) * r5.xyz;
  r1.x = dot(r7.xyz, r5.xyz);
  r6.xy = v8.xy;
  r1.z = dot(r7.xyz, r6.xyz);
  r2.w = r1.z + r1.z;
  r6.xyw = v8.xyw;
  r7.xyzw = float4(10000,6944.44434,15625,-1) * r6.xyzw;
  r3.w = dot(r7.xyzw, r6.xyzw);
  r3.w = r3.w * r1.x;
  r2.w = r2.w * r2.w;
  r2.w = r3.w * -4 + r2.w;
  r3.w = -9.99999997e-007 + r2.w;
  r3.w = cmp(r3.w < 0);
  r2.w = sqrt(r2.w);
  r1.z = -r1.z * 2 + -r2.w;
  r1.x = 0.5 / r1.x;
  r1.x = r1.x * r1.z;
  r1.x = r3.w ? 999999 : r1.x;
  r1.z = r1.x + -abs(r2.z);
  r1.z = cmp(r1.z < 0);
  if (r1.z != 0) {
    r6.xyz = r5.xyz * r1.xxx + r6.xyz;
    r6.xyz = float3(20000,13888.8887,31250) * r6.xyz;
    r1.z = dot(r6.xyz, r6.xyz);
    r1.z = rsqrt(r1.z);
    r6.xyz = r6.xyz * r1.zzz;
    r1.z = dot(-r5.xyz, r6.xyz);
    r1.z = r1.z + r1.z;
    r6.xyz = r6.xyz * -r1.zzz + -r5.xyz;
    r7.x = dot(g_joint[0].xyz, -r6.xyz);
    r7.y = dot(g_joint[1].xyz, -r6.xyz);
    r7.z = dot(g_joint[2].xyz, -r6.xyz);
    r6.x = dot(g_normal_tangent_transforms[0].xyz, r7.xyz);
    r6.y = dot(g_normal_tangent_transforms[1].xyz, r7.xyz);
    r6.z = dot(g_normal_tangent_transforms[2].xyz, r7.xyz);
    r6.xyz = g_ibl_textures_1_.SampleLevel(g_ibl_samplers_0__s, r6.xyz, 0).xyz;
    r6.xyz = r6.xyz * r3.xyz;
    r6.xyz = g_light_env_chara_specular.xyz * r6.xyz;
    r6.xyz = float3(0.100000001,0.100000001,0.100000001) * r6.xyz;
    r7.xyz = float3(0.75,0.75,0.75) * r3.xyz;
    r2.z = r1.x;
  } else {
    r7.xyz = r3.xyz;
    r6.xyz = float3(0,0,0);
    r2.z = abs(r2.z);
  }
  r8.xyz = float3(-5000,-3472.22217,-80000) * r4.xyz;
  r1.x = dot(r8.xyz, r8.xyz);
  r1.x = rsqrt(r1.x);
  r8.xyz = r8.xyz * r1.xxx;
  r9.x = dot(g_joint[0].xyz, r8.xyz);
  r9.y = dot(g_joint[1].xyz, r8.xyz);
  r9.z = dot(g_joint[2].xyz, r8.xyz);
  r10.x = dot(g_normal_tangent_transforms[0].xyz, r9.xyz);
  r10.y = dot(g_normal_tangent_transforms[1].xyz, r9.xyz);
  r10.z = dot(g_normal_tangent_transforms[2].xyz, r9.xyz);
  r11.xyz = g_ibl_textures_0_.SampleLevel(g_ibl_samplers_0__s, r10.xyz, 0).xyz;
  r10.xyz = g_ibl_textures_0_.SampleLevel(g_ibl_samplers_0__s, r10.xyz, 1).xyz;
  r11.xyz = r11.xyz + -r10.xyz;
  r10.xyz = r2.xxx * r11.xyz + r10.xyz;
  r0.x = cmp((int)r0.x == 1);
  r11.xyz = g_light_env_chara_diffuse.xyz * r10.xyz;
  r11.xyz = r11.xyz * g_npr_param[0].www + v9.www;
  r10.xyz = r10.xyz * g_light_env_chara_diffuse.xyz + v9.www;
  r10.xyz = r0.xxx ? r11.xyz : r10.xyz;
  r10.xyz = g_light_env_chara_ambient.xyz + r10.xyz;
  r11.xyz = g_npr_param[0].yyy * r10.xyz;
  r10.xyz = r0.xxx ? r11.xyz : r10.xyz;
  if (1 == 0) r1.x = 0; else if (1+12 < 32) {   r1.x = (uint)g_shader_flags.y << (32-(1 + 12)); r1.x = (uint)r1.x >> (32-1);  } else r1.x = (uint)g_shader_flags.y >> 12;
  if (1 == 0) r1.z = 0; else if (1+10 < 32) {   r1.z = (uint)g_shader_flags.x << (32-(1 + 10)); r1.z = (uint)r1.z >> (32-1);  } else r1.z = (uint)g_shader_flags.x >> 10;
  r1.xz = cmp((int2)r1.xz == int2(1,1));
  r2.x = dot(r9.xyz, g_chara_f_dir.xyz);
  r2.x = r2.x * 0.5 + 0.5;
  r2.x = -g_chara_tc_param.x + r2.x;
  r2.x = saturate(g_chara_tc_param.y * r2.x);
  r9.xyz = g_chara_f_diffuse.xyz + -g_chara_f_ambient.xyz;
  r9.xyz = r2.xxx * r9.xyz + g_chara_f_ambient.xyz;
  r9.xyz = r9.xyz * g_chara_tc_param.zzz + r10.xyz;
  r9.xyz = r1.xxx ? r9.xyz : r10.xyz;
  r10.xyz = float3(0.949999988,0.949999988,0.949999988) * r9.xyz;
  r1.x = g_npr_param[7].y * g_npr_param[0].x;
  r11.xyz = r10.xyz * r1.xxx;
  r10.xyz = r0.xxx ? r11.xyz : r10.xyz;
  r7.xyz = r10.xyz * r7.xyz;
  r10.xyz = g_material_state_diffuse.xyz * float3(-2,-2,-2) + float3(2,2,2);
  r10.xyz = float3(61.314537,61.314537,61.314537) * r10.xyz;
  r2.xzw = r10.xyz * -r2.zzz;
  r2.xzw = exp2(r2.xzw);
  r11.xyz = r7.xyz * r2.xzw;
  r12.xyz = r0.yzw * g_light_env_chara_specular.xyz + -r11.xyz;
  r11.xyz = r1.yyy * r12.xyz + r11.xyz;
  r11.xyz = r2.yyy * r9.xyz + r11.xyz;
  r0.x = dot(-r5.xyz, r8.xyz);
  r0.x = r0.x + r0.x;
  r5.xyz = r8.xyz * -r0.xxx + -r5.xyz;
  r8.xyz = float3(-2500,-1736.11108,-12345.6787) * r5.xyz;
  r0.x = dot(r8.xyz, -r5.xyz);
  r1.x = dot(r8.xyz, r4.xyz);
  r3.w = r1.x + r1.x;
  r8.xyzw = float4(2500,1736.11108,12345.6787,-1) * r4.xyzw;
  r4.w = dot(r8.xyzw, r4.xyzw);
  r4.w = r4.w * r0.x;
  r3.w = r3.w * r3.w;
  r3.w = r4.w * -4 + r3.w;
  r4.w = -9.99999997e-007 + r3.w;
  r4.w = cmp(r4.w < 0);
  r5.w = dot(r11.xyz, float3(0.298900008,0.586600006,0.114500001));
  r8.xyz = g_chara_color1.xyz + -g_chara_color0.xyz;
  r12.xyz = r5.www * r8.xyz + g_chara_color0.xyz;
  r12.xyz = r12.xyz + -r11.xyz;
  r12.xyz = g_chara_color1.www * r12.xyz + r11.xyz;
  r12.xyz = max(float3(0,0,0), r12.xyz);
  r13.xyz = v10.xyz + -r11.xyz;
  r13.xyz = v4.www * r13.xyz + r11.xyz;
  r12.xyz = r1.zzz ? r12.xyz : r13.xyz;
  if (r4.w != 0) {
    o0.xyz = r12.xyz;
    o0.w = 1;
    return;
  }
  r3.w = sqrt(r3.w);
  r1.x = -r1.x * 2 + r3.w;
  r0.x = 0.5 / r0.x;
  r0.x = r0.x * r1.x;
  r4.xyz = -r5.xyz * r0.xxx + r4.xyz;
  r1.x = cmp(r4.z < 0);
  if (r1.x != 0) {
    o0.xyz = r12.xyz;
    o0.w = 1;
    return;
  }
  r4.xyz = float3(5000,3472.22217,24691.3574) * r4.xyz;
  r1.x = dot(r4.xyz, r4.xyz);
  r1.x = rsqrt(r1.x);
  r4.xyz = r4.xyz * r1.xxx;
  r1.x = saturate(dot(r5.xyz, -r4.xyz));
  r3.w = r1.x * r1.x;
  r4.w = r3.w * 2.10249996 + -1.10250092;
  r4.w = cmp(r4.w < 0);
  if (r4.w != 0) {
    r12.xyz = float3(0.850000024,0.850000024,0.850000024) * r11.xyz;
    r4.w = dot(r12.xyz, float3(0.298900008,0.586600006,0.114500001));
    r13.xyz = r4.www * r8.xyz + g_chara_color0.xyz;
    r13.xyz = -r11.xyz * float3(0.850000024,0.850000024,0.850000024) + r13.xyz;
    r13.xyz = g_chara_color1.www * r13.xyz + r12.xyz;
    r13.xyz = max(float3(0,0,0), r13.xyz);
    r11.xyz = -r11.xyz * float3(0.850000024,0.850000024,0.850000024) + v10.xyz;
    r11.xyz = v4.www * r11.xyz + r12.xyz;
    o0.xyz = r1.zzz ? r13.xyz : r11.xyz;
    o0.w = 1;
    return;
  }
  r3.w = r3.w * 2.10249996 + -1.10249996;
  r4.w = dot(r3.xyz, r3.xyz);
  r4.w = saturate(r4.w * 0.200000003 + -0.0199999996);
  r5.w = rsqrt(r3.w);
  r3.w = r5.w * r3.w;
  r1.x = r1.x * 1.45000005 + -r3.w;
  r5.xyz = float3(-1.45000005,-1.45000005,-1.45000005) * r5.xyz;
  r4.xyz = -r4.xyz * r1.xxx + r5.xyz;
  r5.x = dot(g_joint[0].xyz, r4.xyz);
  r5.y = dot(g_joint[1].xyz, r4.xyz);
  r5.z = dot(g_joint[2].xyz, r4.xyz);
  r4.x = dot(g_normal_tangent_transforms[0].xyz, r5.xyz);
  r4.y = dot(g_normal_tangent_transforms[1].xyz, r5.xyz);
  r4.z = dot(g_normal_tangent_transforms[2].xyz, r5.xyz);
  r5.xyz = g_ibl_textures_1_.SampleLevel(g_ibl_samplers_1__s, r4.xyz, 0).xyz;
  r4.xyz = g_ibl_textures_3_.SampleLevel(g_ibl_samplers_1__s, r4.xyz, 0).xyz;
  r5.xyz = r5.xyz + -r4.xyz;
  r4.xyz = r1.www * r5.xyz + r4.xyz;
  r4.xyz = g_light_env_chara_specular.xyz * r4.xyz;
  r3.xyz = r4.xyz * r3.xyz;
  r4.xyz = -r0.xxx * r10.xyz;
  r4.xyz = exp2(r4.xyz);
  r3.xyz = r4.xyz * r3.xyz;
  r3.xyz = r3.xyz * r4.www + r7.xyz;
  r2.xzw = r3.xyz * r2.xzw;
  r0.xyz = r0.yzw * g_light_env_chara_specular.xyz + -r2.xzw;
  r0.xyz = r1.yyy * r0.xyz + r2.xzw;
  r0.xyz = r0.xyz + r6.xyz;
  r0.xyz = r2.yyy * r9.xyz + r0.xyz;
  r0.w = dot(r0.xyz, float3(0.298900008,0.586600006,0.114500001));
  r1.xyw = r0.www * r8.xyz + g_chara_color0.xyz;
  r1.xyw = r1.xyw + -r0.xyz;
  r1.xyw = g_chara_color1.www * r1.xyw + r0.xyz;
  r1.xyw = max(float3(0,0,0), r1.xyw);
  r2.xyz = v10.xyz + -r0.xyz;
  r0.xyz = v4.www * r2.xyz + r0.xyz;
  o0.xyz = r1.zzz ? r1.xyw : r0.xyz;
  o0.w = 1;
  return;
}
*/