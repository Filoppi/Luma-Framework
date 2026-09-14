cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m[45] : packoffset(c0);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t5 : register(t5);

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

// compared to others, this use the least history info
void frag_main()
{
    float _47 = asfloat(cb1_m[44u].z);
    float _48 = asfloat(cb1_m[44u].w);
    float _54 = mad(_47, 0.5f, TEXCOORD.x);
    float _56 = mad(_48, 0.5f, TEXCOORD.y);
    float2 _59 = float2(_54, _56);
    float4 _62 = t0.GatherRed(s0, _59);
    float _67 = _62.w;
    if (_67 < 65504.0f)
    {
        float _74 = 1.0f / (_67 * 0.00999999977648258209228515625f);
        float _75 = mad(_47, -1.5f, TEXCOORD.x);
        float _76 = mad(_48, -1.5f, TEXCOORD.y);
        float2 _77 = float2(_75, _56);
        float4 _79 = t0.GatherRed(s0, _77);
        float _84 = _67 * _74;
        float _89 = (_74 * _79.x) - _84;
        float _90 = (_74 * _79.y) - _84;
        float _91 = (_74 * _79.z) - _84;
        float _92 = (_74 * _79.w) - _84;
        float _96 = (_62.x * _74) - _84;
        float _97 = (_62.y * _74) - _84;
        float _98 = (_62.z * _74) - _84;
        float _99 = _84 - _84;
        float2 _100 = float2(_54, _76);
        float4 _102 = t0.GatherRed(s0, _100);
        float _111 = (_74 * _102.x) - _84;
        float _112 = (_74 * _102.y) - _84;
        float _113 = (_74 * _102.z) - _84;
        float _114 = (_74 * _102.w) - _84;
        float2 _115 = float2(_75, _76);
        float4 _117 = t0.GatherRed(s0, _115);
        float _126 = (_74 * _117.x) - _84;
        float _127 = (_74 * _117.y) - _84;
        float _128 = (_74 * _117.z) - _84;
        float _129 = (_74 * _117.w) - _84;
        float4 _132 = t5.GatherRed(s0, _77);
        float _137 = _97 * 0.707106769084930419921875f;
        float _138 = _89 * 0.447213590145111083984375f;
        float _140 = _129 * 0.3535533845424652099609375f;
        float _141 = _128 * 0.447213590145111083984375f;
        float _142 = _126 * 0.447213590145111083984375f;
        float _148 = abs(_137 + _138);
        float _149 = abs(_138 + _140);
        float _154 = max(1.0f - min(min(_148, _149), abs(_89)), 0.0f);
        float _157 = abs((_90 * 0.707106769084930419921875f) + _141);
        float _158 = abs((_92 * 0.5f) + _142);
        float _159 = abs(mad(_90, 0.707106769084930419921875f, _96));
        float _160 = abs(mad(_92, 0.5f, _98));
        float _169 = max(1.0f - min(min(_157, _159), abs(_90)), 0.0f);
        float _170 = max(1.0f - min(abs(_92), min(_160, _158)), 0.0f);
        float _179 = abs(mad(_99, asfloat(0x7f800000u /* inf */), _91));
        float _180 = abs(mad(_127, 0.707106769084930419921875f, _91));
        float _185 = max(1.0f - min(min(_179, _180), abs(_91)), 0.0f);
        float4 _191 = t5.GatherRed(s0, _59);
        float _196 = _113 * 0.447213590145111083984375f;
        float _201 = abs(mad(_114, 0.5f, _96));
        float _202 = abs(mad(_112, 0.707106769084930419921875f, _98));
        float _211 = max(1.0f - min(min(_159, _201), abs(_96)), 0.0f);
        float _212 = max(1.0f - min(abs(_98), min(_202, _160)), 0.0f);
        float _216 = abs(_137 + _196);
        float _221 = max(1.0f - min(min(_148, _216), abs(_97)), 0.0f);
        float _227 = abs(mad(_99, asfloat(0x7f800000u /* inf */), _111));
        float _232 = max(1.0f - min(min(_179, _227), abs(_99)), 0.0f);
        float4 _236 = t5.GatherRed(s0, _100);
        float _242 = abs(mad(_127, 0.707106769084930419921875f, _111));
        float _247 = max(1.0f - min(min(_227, _242), abs(_111)), 0.0f);
        float _253 = abs((_112 * 0.707106769084930419921875f) + _142);
        float _254 = abs((_114 * 0.5f) + _141);
        float _263 = max(1.0f - min(min(_253, _202), abs(_112)), 0.0f);
        float _264 = max(1.0f - min(abs(_114), min(_201, _254)), 0.0f);
        float _267 = abs(_196 + _140);
        float _272 = max(1.0f - min(min(_216, _267), abs(_113)), 0.0f);
        float4 _278 = t5.GatherRed(s0, _115);
        float _295 = max(1.0f - min(min(_253, _158), abs(_126)), 0.0f);
        float _296 = max(1.0f - min(abs(_128), min(_157, _254)), 0.0f);
        float _297 = max(1.0f - min(abs(_129), min(_149, _267)), 0.0f);
        float _304 = max(1.0f - min(min(_180, _242), abs(_127)), 0.0f);
        SV_TARGET = mad(_297, _278.w, mad(_296, _278.z, mad(_278.y, _304, mad(_278.x, _295, mad(_236.w, _264, mad(_236.z, _272, mad(_236.y, _263, mad(_236.x, _247, mad(_191.w, _232, mad(_191.z, _212, mad(_191.y, _221, mad(_191.x, _211, mad(_132.w, _170, mad(_132.z, _185, (_132.y * _169) + (_132.x * _154))))))))))))))) / mad(_297, 1.0f, mad(_296, 1.0f, mad(_304, 1.0f, mad(_295, 1.0f, mad(_264, 1.0f, mad(_272, 1.0f, mad(_263, 1.0f, mad(_247, 1.0f, mad(_232, 1.0f, mad(_212, 1.0f, mad(_221, 1.0f, mad(_211, 1.0f, mad(_170, 1.0f, mad(_185, 1.0f, (_154 * 1.0f) + (_169 * 1.0f)))))))))))))));
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
