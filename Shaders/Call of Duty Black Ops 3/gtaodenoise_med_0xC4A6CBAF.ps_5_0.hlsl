cbuffer cb8_buf : register(b8)
{
    uint4 cb8_m0 : packoffset(c0);
    float4 cb8_m1 : packoffset(c1);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m[45] : packoffset(c0);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t7 : register(t7);

#include "./Includes/Common.hlsl"

static float2 TEXCOORD;
static float SV_TARGET;

struct SPIRV_Cross_Input
{
    float4 SV_POSITION : SV_Position;
    float2 TEXCOORD : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float SV_TARGET : SV_Target0;
};

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

float dp2_f32(float2 a, float2 b)
{
    precise float _73 = a.x * b.x;
    return mad(a.y, b.y, _73);
}

void frag_main()
{
    float _99 = asfloat(cb1_m[44u].z);
    float _100 = asfloat(cb1_m[44u].w);
    float _101 = _100 * 0.5f;
    float _102 = _99 * 0.5f;
    float _108 = mad(_99, 0.5f, TEXCOORD.x);
    float _109 = mad(_100, 0.5f, TEXCOORD.y);
    float2 _112 = float2(_108, _109);
    float4 _115 = t0.GatherRed(s0, _112);
    float _119 = _115.w;
    if (_119 < 65504.0f)
    {
        float _126 = 1.0f / (_119 * 0.00999999977648258209228515625f);
        float _127 = 1.0f / (_119 * 0.100000001490116119384765625f);
        float _128 = mad(_99, -1.5f, TEXCOORD.x);
        float _129 = mad(_100, -1.5f, TEXCOORD.y);
        float2 _130 = float2(_128, _109);
        float4 _132 = t0.GatherRed(s0, _130);
        float _137 = _119 * _126;
        float _142 = (_126 * _132.x) - _137;
        float _143 = (_126 * _132.y) - _137;
        float _144 = (_126 * _132.z) - _137;
        float _145 = (_126 * _132.w) - _137;
        float _149 = (_115.x * _126) - _137;
        float _150 = (_115.y * _126) - _137;
        float _151 = (_115.z * _126) - _137;
        float _152 = _137 - _137;
        float2 _153 = float2(_108, _129);
        float4 _155 = t0.GatherRed(s0, _153);
        float _164 = (_126 * _155.x) - _137;
        float _165 = (_126 * _155.y) - _137;
        float _166 = (_126 * _155.z) - _137;
        float _167 = (_126 * _155.w) - _137;
        float _168 = mad(_99, -0.5f, TEXCOORD.x);
        float _169 = mad(_100, -0.5f, TEXCOORD.y);
        float2 _170 = float2(_128, _129);
        float4 _172 = t0.GatherRed(s0, _170);
        float _181 = (_126 * _172.x) - _137;
        float _182 = (_126 * _172.y) - _137;
        float _183 = (_126 * _172.z) - _137;
        float _184 = (_126 * _172.w) - _137;
        float4 _187 = t5.GatherRed(s0, _130);
        float _192 = _150 * 0.707106769084930419921875f;
        float _193 = _142 * 0.447213590145111083984375f;
        float _195 = _184 * 0.3535533845424652099609375f;
        float _196 = _183 * 0.447213590145111083984375f;
        float _197 = _181 * 0.447213590145111083984375f;
        float _203 = abs(_192 + _193);
        float _204 = abs(_193 + _195);
        float _209 = max(1.0f - min(min(_203, _204), abs(_142)), 0.0f);
        float _212 = abs((_143 * 0.707106769084930419921875f) + _196);
        float _213 = abs((_145 * 0.5f) + _197);
        float _214 = abs(mad(_143, 0.707106769084930419921875f, _149));
        float _215 = abs(mad(_145, 0.5f, _151));
        float _224 = max(1.0f - min(min(_212, _214), abs(_143)), 0.0f);
        float _225 = max(1.0f - min(abs(_145), min(_215, _213)), 0.0f);
        float _234 = abs(mad(_182, 0.707106769084930419921875f, _144));
        float _235 = abs(mad(_152, asfloat(0x7f800000u /* inf */), _144));
        float _240 = max(1.0f - min(min(_234, _235), abs(_144)), 0.0f);
        float4 _246 = t5.GatherRed(s0, _112);
        float _251 = _166 * 0.447213590145111083984375f;
        float _256 = abs(mad(_167, 0.5f, _149));
        float _257 = abs(mad(_165, 0.707106769084930419921875f, _151));
        float _266 = max(1.0f - min(min(_214, _256), abs(_149)), 0.0f);
        float _267 = max(1.0f - min(abs(_151), min(_257, _215)), 0.0f);
        float _271 = abs(_192 + _251);
        float _276 = max(1.0f - min(min(_203, _271), abs(_150)), 0.0f);
        float _282 = abs(mad(_152, asfloat(0x7f800000u /* inf */), _164));
        float _287 = max(1.0f - min(min(_235, _282), abs(_152)), 0.0f);
        float4 _291 = t5.GatherRed(s0, _153);
        float _297 = abs(mad(_182, 0.707106769084930419921875f, _164));
        float _302 = max(1.0f - min(min(_282, _297), abs(_164)), 0.0f);
        float _308 = abs((_165 * 0.707106769084930419921875f) + _197);
        float _309 = abs((_167 * 0.5f) + _196);
        float _318 = max(1.0f - min(min(_308, _257), abs(_165)), 0.0f);
        float _319 = max(1.0f - min(abs(_167), min(_256, _309)), 0.0f);
        float _322 = abs(_251 + _195);
        float _327 = max(1.0f - min(min(_271, _322), abs(_166)), 0.0f);
        float4 _333 = t5.GatherRed(s0, _170);
        float _350 = max(1.0f - min(min(_308, _213), abs(_181)), 0.0f);
        float _351 = max(1.0f - min(abs(_183), min(_212, _309)), 0.0f);
        float _352 = max(1.0f - min(abs(_184), min(_204, _322)), 0.0f);
        float _359 = max(1.0f - min(min(_234, _297), abs(_182)), 0.0f);
        float _366 = mad(_333.w, _352, mad(_333.z, _351, mad(_333.y, _359, mad(_333.x, _350, mad(_291.w, _319, mad(_291.z, _327, mad(_291.y, _318, mad(_291.x, _302, mad(_246.w, _287, mad(_246.z, _267, mad(_246.y, _276, mad(_246.x, _266, mad(_187.w, _225, mad(_187.z, _240, (_187.y * _224) + (_187.x * _209))))))))))))))) / mad(_352, 1.0f, mad(_351, 1.0f, mad(_359, 1.0f, mad(_350, 1.0f, mad(_319, 1.0f, mad(_327, 1.0f, mad(_318, 1.0f, mad(_302, 1.0f, mad(_287, 1.0f, mad(_267, 1.0f, mad(_276, 1.0f, mad(_266, 1.0f, mad(_225, 1.0f, mad(_240, 1.0f, (_209 * 1.0f) + (_224 * 1.0f)))))))))))))));
        float4 _370 = t3.SampleLevel(s0, float2(TEXCOORD.x, TEXCOORD.y), 0.0f);
        float _371 = _370.x;
        float _372 = _370.y;
        float _373 = abs(_371);
        float _374 = abs(_372);
        float _375 = _373 - 0.5f;
        float _376 = _374 - 0.5f;
        float _389 = (clamp(_375 + _375, 0.0f, 1.0f) * 30.0f) + (min(_373 + _373, 1.0f) * 10.0f);
        float _390 = (clamp(_376 + _376, 0.0f, 1.0f) * 30.0f) + (min(_374 + _374, 1.0f) * 10.0f);
        float _395 = (_371 >= 0.0f) ? _389 : (-_389);
        float _396 = (_372 >= 0.0f) ? _390 : (-_390);
        float _398 = mad(-_101, _396, TEXCOORD.y);
        float _400 = mad(-_102, _395, TEXCOORD.x);
        float4 _404 = t4.SampleLevel(s0, float2(_400, _398), 0.0f);
        float _405 = _404.x;
        float _406 = _404.y;
        float _407 = abs(_405);
        float _408 = abs(_406);
        float _409 = _407 - 0.5f;
        float _410 = _408 - 0.5f;
        float _423 = (min(_407 + _407, 1.0f) * 10.0f) + (clamp(_409 + _409, 0.0f, 1.0f) * 30.0f);
        float _424 = (clamp(_410 + _410, 0.0f, 1.0f) * 30.0f) + (min(_408 + _408, 1.0f) * 10.0f);
        float _429 = (_405 >= 0.0f) ? _423 : (-_423);
        float _430 = (_406 >= 0.0f) ? _424 : (-_424);
        float _432 = mad(-_101, _430, _398);
        float _434 = mad(-_102, _429, _400);
        float4 _438 = t7.GatherRed(s0, float2(_434, _432));
        float _447 = mad(_432, asfloat(cb1_m[44u].y), -0.498046875f);
        float _448 = mad(_434, asfloat(cb1_m[44u].x), -0.498046875f);
        int _449 = cvt_f32_i32(_448);
        int _450 = cvt_f32_i32(_447);
        uint _452 = uint(_450 + 1);
        uint _454 = uint(_449 + 1);
        uint _456 = uint(_449);
        uint _463 = uint(_450);
        float2 _470 = float2(_395, _396);
        float2 _472 = float2(_429, _430);
        float _478 = asfloat((asint(max(dp2_f32(_470, _470), dp2_f32(_472, _472))) >> int(1u)) + 532487669);
        float _491 = frac(_447);
        float _492 = frac(_448);
        float _493 = 1.0f - _492;
        float _494 = 1.0f - _491;
        float _527 = mad(_494 * _493, mad(_438.w - _366, clamp(mad(-_127, _119 - t2.Load(int3(uint2(_456, _463), 0u)).x, 1.0f), 0.0f, 1.0f), _366), mad(_494 * _492, mad(_438.z - _366, clamp(mad(-_127, _119 - t2.Load(int3(uint2(_454, _463), 0u)).x, 1.0f), 0.0f, 1.0f), _366), ((_492 * _491) * mad(_438.y - _366, clamp(mad(-_127, _119 - t2.Load(int3(uint2(_454, _452), 0u)).x, 1.0f), 0.0f, 1.0f), _366)) + ((_491 * _493) * mad(clamp(mad(-_127, _119 - t2.Load(int3(uint2(_456, _452), 0u)).x, 1.0f), 0.0f, 1.0f), _438.x - _366, _366))));
        float4 _530 = t5.SampleLevel(s0, float2(_168, _169), 0.0f);
        float _531 = _530.x;
        float4 _533 = t5.SampleLevel(s0, _112, 0.0f);
        float _534 = _533.x;
        float4 _537 = t5.SampleLevel(s0, float2(_168, _109), 0.0f);
        float _538 = _537.x;
        float4 _541 = t5.SampleLevel(s0, float2(_108, _169), 0.0f);
        float _542 = _541.x;
        float _546 = min(_366, min(_531, min(_534, min(_538, _542))));
        float _550 = max(_366, max(_531, max(_534, max(_538, _542))));
        float2 _553 = float2(_395 - _429, _396 - _430);
        float _560 = clamp(mad(asfloat((asint(dp2_f32(_553, _553)) >> int(1u)) + 532487669), -0.25f, 1.0f), 0.0f, 1.0f);
        float _565 = (mad(_560, -2.0f, 3.0f) * (_560 * _560)) * (_550 - _546);
        float _566 = mad(_565, -0.5f, _546);
        float _567 = mad(_565, 0.5f, _550);
        SV_TARGET = mad(mad(clamp(_478, 0.0f, 1.0f), cb8_m1.x - cb8_m1.y, cb8_m1.y) * clamp(40.0f - _478, 0.0f, 1.0f), clamp(_527, _566, min(max(_527, _567), max(_566, _567))) - _366, _366);
        SV_TARGET = pow(SV_TARGET, GS_AmbientOcclusion);
    }
    else
    {
        SV_TARGET = 1.0f;
    }
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    return stage_output;
}
