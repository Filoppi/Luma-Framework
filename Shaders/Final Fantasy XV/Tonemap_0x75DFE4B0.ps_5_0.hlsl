#include "Includes/Tonemap.hlsli"
cbuffer cb0_buf : register(b0)
{
    float3 cb0_m0 : packoffset(c0);
    uint cb0_m1 : packoffset(c0.w);
    float4 cb0_m2 : packoffset(c1);
    float2 cb0_m3 : packoffset(c2);
    float2 cb0_m4 : packoffset(c2.z);
    float2 cb0_m5 : packoffset(c3);
    float2 cb0_m6 : packoffset(c3.z);
    float2 cb0_m7 : packoffset(c4);
    float2 cb0_m8 : packoffset(c4.z);
    float2 cb0_m9 : packoffset(c5);
    float2 cb0_m10 : packoffset(c5.z);
    float4 cb0_m11 : packoffset(c6);
    float4 cb0_m12 : packoffset(c7);
    float4 cb0_m13 : packoffset(c8);
    float4 cb0_m14 : packoffset(c9);
    float3 cb0_m15 : packoffset(c10);
    uint cb0_m16 : packoffset(c10.w);
    uint4 cb0_m17 : packoffset(c11);
    uint4 cb0_m18 : packoffset(c12);
    float4 cb0_m19 : packoffset(c13);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);

static float2 TEXCOORD;
static float4 SV_TARGET;

struct SPIRV_Cross_Input
{
    float4 Position : SV_Position;
    float2 TEXCOORD : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 SV_TARGET : SV_Target0;
};

float dp3_f32(float3 a, float3 b)
{
    precise float _173 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _173));
}

void frag_main()
{
    float4 _194 = t0.SampleLevel(s0, float2(TEXCOORD.x, TEXCOORD.y), 0.0f);
    float3 _199 = float3(_194.xyz);
    float3 _213 = float3(dp3_f32(_199, float3(0.54247200489044189453125f, 0.4392839968204498291015625f, 0.01824299991130828857421875f)) * cb0_m0.x, dp3_f32(_199, float3(0.0426700003445148468017578125f, 0.94111502170562744140625f, 0.01621400006115436553955078125f)) * cb0_m0.y, dp3_f32(_199, float3(0.01731600053608417510986328125f, 0.094967998564243316650390625f, 0.887715995311737060546875f)) * cb0_m0.z);
    float _214 = dp3_f32(_213, float3(0.720840990543365478515625f, 0.267010986804962158203125f, 0.012148000299930572509765625f));
    float _215 = dp3_f32(_213, float3(0.0496839992702007293701171875f, 0.943306982517242431640625f, 0.0070090000517666339874267578125f));
    float _216 = dp3_f32(_213, float3(0.0064210002310574054718017578125f, 0.0243079997599124908447265625f, 0.969271004199981689453125f));
    bool EnabledToneCurve = cb0_m17.z != 0u;
    float3 _277;
    if (LumaSettings.DisplayMode == 1)
    {
        float TenPowLogHighRangePlusContrastMinusOne = cb0_m2.x;
        float TenPowDispositionTimesTwoPowHighRange_PlusOne_Log_Inverse = cb0_m2.y;
        float ZeroSlopeByTenPowDispositionPlusOne = cb0_m2.z;
        float Param_n37 = cb0_m2.w;
        float Param_n46 = cb0_m3.x;
        float Param_n49 = cb0_m3.y;
        // Derive the game's SDR curve tuning from the HDR-mode uploads (closed form).
        // Only applies when the game itself is in HDR mode (cb0_m16) and the toggle is on.
        if (cb0_m16 != 0u && LumaSettings.GameSettings.UseSDROverHDR != 0u)
        {
            RecoverSDRParams(cb0_m2.x, cb0_m2.y, cb0_m2.z, cb0_m2.w, cb0_m3.x, cb0_m3.y,
                             TenPowLogHighRangePlusContrastMinusOne, TenPowDispositionTimesTwoPowHighRange_PlusOne_Log_Inverse, ZeroSlopeByTenPowDispositionPlusOne, Param_n37, Param_n46, Param_n49);
        }
        float3 untonemapped = float3(_214, _215, _216);
        float3 vanilla = FFXV(untonemapped, ZeroSlopeByTenPowDispositionPlusOne, TenPowDispositionTimesTwoPowHighRange_PlusOne_Log_Inverse, Param_n37, TenPowLogHighRangePlusContrastMinusOne, Param_n46, Param_n49);
        float3 tonemapped = FFXV_TonemapExtended(untonemapped, ZeroSlopeByTenPowDispositionPlusOne, TenPowDispositionTimesTwoPowHighRange_PlusOne_Log_Inverse, Param_n37, TenPowLogHighRangePlusContrastMinusOne, Param_n46, Param_n49);
        tonemapped = lerp(tonemapped, vanilla, 0.25f);
        _277 = EnabledToneCurve ? tonemapped : untonemapped;
    }
    else 
    {
        _277 = float3(EnabledToneCurve ? max(mad(log2(mad(exp2(log2((log2(mad(_214, 39.810718536376953125f, cb0_m2.z)) * cb0_m2.y) * 0.693147182464599609375f) * cb0_m2.w), cb0_m2.x, 1.0f)) * cb0_m3.x, 0.693147182464599609375f, -cb0_m3.y), 0.0f) : _214, EnabledToneCurve ? max(mad(cb0_m3.x * log2(mad(cb0_m2.x, exp2(log2((log2(mad(_215, 39.810718536376953125f, cb0_m2.z)) * cb0_m2.y) * 0.693147182464599609375f) * cb0_m2.w), 1.0f)), 0.693147182464599609375f, -cb0_m3.y), 0.0f) : _215, EnabledToneCurve ? max(mad(cb0_m3.x * log2(mad(cb0_m2.x, exp2(log2((log2(mad(_216, 39.810718536376953125f, cb0_m2.z)) * cb0_m2.y) * 0.693147182464599609375f) * cb0_m2.w), 1.0f)), 0.693147182464599609375f, -cb0_m3.y), 0.0f) : _216);
    }
    float3 _281 = float3(dp3_f32(_277, float3(1.41498100757598876953125f, -0.400139987468719482421875f, -0.01484099961817264556884765625f)), dp3_f32(_277, float3(-0.074470996856689453125f, 1.08135700225830078125f, -0.00688599981367588043212890625f)), dp3_f32(_277, float3(-0.0075070001184940338134765625f, -0.02446799911558628082275390625f, 1.03197395801544189453125f)));
    float _282 = dp3_f32(_281, float3(0.3433000147342681884765625f, 0.59329998493194580078125f, 0.0634000003337860107421875f));
    float _283 = dp3_f32(_281, float3(0.4095999896526336669921875f, -0.4532000124454498291015625f, 0.043600000441074371337890625f));
    float _284 = dp3_f32(_281, float3(0.2867999970912933349609375f, 0.21130000054836273193359375f, -0.4981000125408172607421875f));
    float _306 = _283 + ((cb0_m5.y * max(-_284, 0.0f)) - (cb0_m4.y * max(_284, 0.0f)));
    float _307 = _284 + ((max(_283, 0.0f) * cb0_m4.x) - (cb0_m5.x * max(-_283, 0.0f)));
    bool _330 = cb0_m18.y != 0u;
    float _331 = _330 ? ((max(_306, 0.0f) * cb0_m6.x) - (max(-_306, 0.0f) * cb0_m7.x)) : _306;
    float _332 = _330 ? ((max(_307, 0.0f) * cb0_m6.y) - (max(-_307, 0.0f) * cb0_m7.y)) : _307;
    float _337 = max(_331 * 1.0f, 0.0f);
    float _338 = max(_332 * 1.0f, 0.0f);
    float _339 = max(_331 * (-1.0f), 0.0f);
    float _340 = max(_332 * (-1.0f), 0.0f);
    bool _365 = cb0_m18.z != 0u;
    float _366 = _365 ? (((_337 / (_337 + cb0_m8.x)) * cb0_m8.x) - ((_339 / (_339 + cb0_m9.x)) * cb0_m9.x)) : _331;
    float _367 = _365 ? ((cb0_m8.y * (_338 / (cb0_m8.y + _338))) - ((_340 / (_340 + cb0_m9.y)) * cb0_m9.y)) : _332;
    float _372 = clamp(_282, 0.0f, 0.0199999995529651641845703125f) - 0.0f;
    float _373 = clamp(_282, 0.0199999995529651641845703125f, 0.180000007152557373046875f) - 0.0199999995529651641845703125f;
    float _374 = clamp(_282, 0.180000007152557373046875f, 0.5f) - 0.180000007152557373046875f;
    float _375 = clamp(_282, 0.5f, 1.0f) - 0.5f;
    bool _452 = cb0_m18.w != 0u;
    float3 _455 = float3(_282, _452 ? ((max(mad(cb0_m11.z, _375, mad(_374, cb0_m11.y, mad(_373, cb0_m11.x, mad(_372, cb0_m10.y, cb0_m10.x)))), 0.0f) * max(_366 * 1.0f, 0.0f)) - (max(_366 * (-1.0f), 0.0f) * max(mad(cb0_m14.x, _375, mad(_374, cb0_m13.w, mad(_373, cb0_m13.z, mad(cb0_m13.y, _372, cb0_m13.x)))), 0.0f))) : _366, _452 ? ((max(_367 * 1.0f, 0.0f) * max(mad(_375, cb0_m12.w, mad(cb0_m12.z, _374, mad(cb0_m12.y, _373, mad(cb0_m12.x, _372, cb0_m11.w)))), 0.0f)) - (max(_367 * (-1.0f), 0.0f) * max(mad(_375, cb0_m15.y, mad(_374, cb0_m15.x, mad(_373, cb0_m14.w, mad(cb0_m14.z, _372, cb0_m14.y)))), 0.0f))) : _367);
    float3 _459 = float3(dp3_f32(_455, float3(1.0f, 1.4268000125885009765625f, 0.25220000743865966796875f)), dp3_f32(_455, float3(1.0f, -0.873799979686737060546875f, 0.0509000010788440704345703125f)), dp3_f32(_455, float3(1.0f, 0.450800001621246337890625f, -1.840899944305419921875f)));
    float _460 = dp3_f32(_459, float3(1.914248943328857421875f, -0.8911859989166259765625f, -0.02306200005114078521728515625f));
    float _461 = dp3_f32(_459, float3(-0.086308002471923828125f, 1.104712009429931640625f, -0.018403999507427215576171875f));
    float _462 = dp3_f32(_459, float3(-0.02810700051486492156982421875f, -0.100798003375530242919921875f, 1.1289050579071044921875f));
    float _488;
    float _489;
    float _490;
    if (cb0_m16 != 0u)
    {
        float3 color = float3(_460, _461, _462);
        float3 color_bt2020 = BT709_To_BT2020(color);
        color_bt2020.xyz = LumaSettings.GameSettings.UseVanillaGamutRatio == 1 ? mad(cb0_m19.y, color - color_bt2020, color_bt2020) : color_bt2020;
        color = (color_bt2020);
        _488 = color.z;
        _489 = color.y;
        _490 = color.x;
    }
    else
    {
        _488 = _462;
        _489 = _461;
        _490 = _460;
    }
    float _491 = max(_490, 0.0f);
    float _492 = max(_489, 0.0f);
    float _493 = max(_488, 0.0f);
    if (LumaSettings.DisplayMode == 1)
    {
        float3 color = ApplyTonemapAndGrading(float3(_491, _492, _493), false);
        _491 = color.x;
        _492 = color.y;
        _493 = color.z;
        float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
        float UIPaperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;
        _491 *= gamePaperWhite / UIPaperWhite;
        _492 *= gamePaperWhite / UIPaperWhite;
        _493 *= gamePaperWhite / UIPaperWhite;
    }
    bool _517 = cb0_m17.x != 0u;
    SV_TARGET.x = _517 ? ((_491 <= 0.003130800090730190277099609375f) ? (_491 * 12.9200000762939453125f) : mad(exp2(log2(_491) * 0.4166666567325592041015625f), 1.05499994754791259765625f, -0.054999999701976776123046875f)) : _491;
    SV_TARGET.y = _517 ? ((_492 <= 0.003130800090730190277099609375f) ? (_492 * 12.9200000762939453125f) : mad(exp2(log2(_492) * 0.4166666567325592041015625f), 1.05499994754791259765625f, -0.054999999701976776123046875f)) : _492;
    SV_TARGET.z = _517 ? ((_493 <= 0.003130800090730190277099609375f) ? (_493 * 12.9200000762939453125f) : mad(exp2(log2(_493) * 0.4166666567325592041015625f), 1.05499994754791259765625f, -0.054999999701976776123046875f)) : _493;
    SV_TARGET.w = _194.w;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}
