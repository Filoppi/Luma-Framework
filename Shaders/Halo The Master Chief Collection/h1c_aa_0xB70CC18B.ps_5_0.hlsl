/*
// Buffer Definitions: 
//
// cbuffer CB_PS_COMMON
// {
//
//   float4 PS_REG_COMMON_DYN_LIGHT0_PARAMS[4];// Offset:    0 Size:    64 [unused]
//   float4 PS_REG_COMMON_DYN_LIGHT1_PARAMS[4];// Offset:   64 Size:    64 [unused]
//   float4 PS_REG_COMMON_DYN_LIGHT2_PARAMS[4];// Offset:  128 Size:    64 [unused]
//   float4 PS_REG_COMMON_SM_PARAMS[8]; // Offset:  192 Size:   128 [unused]
//   float4 PS_REG_COMMON_SM_MASK_SELECTOR[4];// Offset:  320 Size:    64 [unused]
//   float4 COMMON_VP_PARAMS[4];        // Offset:  384 Size:    64
//   float4 PS_REG_COMMON_HDR_PARAMS;   // Offset:  448 Size:    16 [unused]
//   float4 PS_REG_COMMON_HDR_PARAMS2;  // Offset:  464 Size:    16 [unused]
//   float4 PS_REG_COMMON_HDR_PARAMS3;  // Offset:  480 Size:    16 [unused]
//   float4 PS_REG_COMMON_LIGHTING_PARAMS;// Offset:  496 Size:    16 [unused]
//   float4 PS_REG_COMMON_FOG_SUN_DIR;  // Offset:  512 Size:    16 [unused]
//   float4 PS_REG_COMMON_FOG_RAYLEIGH_FACTOR_X2;// Offset:  528 Size:    16 [unused]
//   float4 PS_REG_COMMON_FOG_COLOR;    // Offset:  544 Size:    16 [unused]
//   float4 PS_REG_COMMON_FOG_COLOR_2;  // Offset:  560 Size:    16 [unused]
//   float4 PS_REG_COMMON_CAM_POS_WORLD;// Offset:  576 Size:    16 [unused]
//   float4 PS_REG_COMMON_CAM_Z_TO_W_KOEFFS;// Offset:  592 Size:    16 [unused]
//   float4 PS_REG_COMMON_ALPHA_KILL_REF;// Offset:  608 Size:    16 [unused]
//   float4 VS_REG_COMMON_VIEWPROJ_MATRIX[4];// Offset:  624 Size:    64 [unused]
//   float4 VS_REG_COMMON_FPWEAPON_VIEWPROJ_MATRIX[4];// Offset:  688 Size:    64 [unused]
//   float4 VS_REG_COMMON_FOG_PARAMS;   // Offset:  752 Size:    16 [unused]
//   float4 VS_REG_COMMON_LIGHTING_PARAMS;// Offset:  768 Size:    16 [unused]
//   float4 VS_REG_COMMON_STEREO_3D;    // Offset:  784 Size:    16 [unused]
//   float4 VS_REG_COMMON_VIEW_POSITION;// Offset:  800 Size:    16 [unused]
//   float4 VS_REG_COMMON_COMPR_VERT_OFFSET;// Offset:  816 Size:    16 [unused]
//   float4 VS_REG_COMMON_COMPR_VERT_SCALE;// Offset:  832 Size:    16 [unused]
//   float4 VS_REG_COMMON_TEX_OFFSET;   // Offset:  848 Size:    16 [unused]
//   float4 VS_REG_COMMON_COMPR_TEX[2]; // Offset:  864 Size:    32 [unused]
//   float4 VS_REG_COMMON_WORLD_MATRIX[3];// Offset:  896 Size:    48 [unused]
//   float4 VS_REG_COMMON_COLOR;        // Offset:  944 Size:    16 [unused]
//   float4 VS_REG_COMMON_INSTANCING;   // Offset:  960 Size:    16 [unused]
//   float4 VS_REG_COMMON_SKIN_BONE_INDEX;// Offset:  976 Size:    16 [unused]
//   float4 VS_REG_FOG_SHARED_COLOR;    // Offset:  992 Size:    16 [unused]
//   float4 VS_REG_FOG_SHARED_COLOR_2;  // Offset: 1008 Size:    16 [unused]
//   float4 VS_REG_FOG_SHARED_SUN_DIR;  // Offset: 1024 Size:    16 [unused]
//   float4 VS_REG_FOG_SHARED_RAYLEIGH_FACTOR;// Offset: 1040 Size:    16 [unused]
//   float4 VS_REG_FOG_SHARED_VOL_1[8]; // Offset: 1056 Size:   128 [unused]
//   float4 VS_REG_FOG_SHARED_VOL_2[8]; // Offset: 1184 Size:   128 [unused]
//   float4 VS_REG_FOG_SHARED_COLOR_PARAMS[8];// Offset: 1312 Size:   128 [unused]
//
// }
//
//
// Resource Bindings:
//
// Name                                 Type  Format         Dim      HLSL Bind  Count
// ------------------------------ ---------- ------- ----------- -------------- ------
// SAMPLERS[4]                       sampler      NA          NA             s4      1 
// TEXTURES_2D[0]                    texture  float4          2d             t0      1 
// CB_PS_COMMON                      cbuffer      NA          NA            cb0      1 
//
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_Position              0   xyzw        0      POS   float       
// TEXCOORD                 0   xy          1     NONE   float   xy  
// TEXCOORD                 1   xyzw        2     NONE   float   xyzw
// TEXCOORD                 2   xyzw        3     NONE   float   xyzw
// TEXCOORD                 3   xyzw        4     NONE   float   xyzw
// TEXCOORD                 4   xyzw        5     NONE   float   xyzw
*/

static const float4 _63[5] = { float4(1.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 1.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 1.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), 0.0f.xxxx };

cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m[25] : packoffset(c0);
};

SamplerState s4 : register(s4);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> bloom : register(t1);

static float2 TEXCOORD;
static float4 TEXCOORD1;
static float4 TEXCOORD2;
static float4 TEXCOORD3;
static float4 TEXCOORD4;
static float4 SV_Target;

#include "./Includes/Common.hlsl"

struct SPIRV_Cross_Input
{
    float4 SV_Position : SV_Position0;
    float2 TEXCOORD  : TEXCOORD0;
    float4 TEXCOORD1 : TEXCOORD1;
    float4 TEXCOORD2 : TEXCOORD2;
    float4 TEXCOORD3 : TEXCOORD3;
    float4 TEXCOORD4 : TEXCOORD4;
};

struct SPIRV_Cross_Output
{
    float4 SV_Target : SV_Target0;
};

float dp4_f32(float4 a, float4 b)
{
    precise float _90 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _90)));
}

float dp3_f32(float3 a, float3 b)
{
    precise float _75 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _75));
}

void frag_main()
{
    float4 _113 = t0.SampleLevel(s4, float2(TEXCOORD2.x, TEXCOORD2.y), 0.0f);
    float _114 = _113.x;
    float _115 = _113.y;
    float4 _123 = t0.SampleLevel(s4, float2(TEXCOORD1.x, TEXCOORD1.y), 0.0f);
    float _124 = _123.x;
    float _125 = _123.y;
    float4 _133 = t0.SampleLevel(s4, float2(TEXCOORD.x, TEXCOORD.y), 0.0f);
    float _134 = _133.x;
    float _135 = _133.y;
    float _136 = _133.z;

    #if ALLOW_AA == 0
        SV_Target = _133;
        return;
    #endif

    float4 _144 = t0.SampleLevel(s4, float2(TEXCOORD1.z, TEXCOORD1.w), 0.0f);
    float _145 = _144.x;
    float _146 = _144.y;
    float4 _154 = t0.SampleLevel(s4, float2(TEXCOORD2.z, TEXCOORD2.w), 0.0f);
    float _155 = _154.x;
    float _156 = _154.y;
    float _158 = mad(_115, 1.96321070194244384765625f, _114);
    float _159 = mad(_125, 1.96321070194244384765625f, _124);
    float _160 = mad(_146, 1.96321070194244384765625f, _145);
    float _161 = mad(_156, 1.96321070194244384765625f, _155);
    float _162 = mad(_135, 1.96321070194244384765625f, _134);
    float _170 = max(_162, max(max(_158, _159), max(_160, _161)));
    float _171 = _170 - min(_162, min(min(_160, _161), min(_158, _159)));
    float _520;
    float _521;
    float _522;
    if (_171 >= max(_170 * 0.125f, 0.0625f))
    {
        float _198 = min(max((abs(dp4_f32(float4(_158, _159, _160, _161), 0.25f.xxxx) - _162) / _171) - 0.25f, 0.0f) * 1.33333337306976318359375f, 0.75f);
        float4 _205 = t0.SampleLevel(s4, float2(TEXCOORD3.x, TEXCOORD3.y), 0.0f);
        float _206 = _205.x;
        float _207 = _205.y;
        float4 _215 = t0.SampleLevel(s4, float2(TEXCOORD4.z, TEXCOORD4.w), 0.0f);
        float _216 = _215.x;
        float _217 = _215.y;
        float4 _225 = t0.SampleLevel(s4, float2(TEXCOORD4.x, TEXCOORD4.y), 0.0f);
        float _226 = _225.x;
        float _227 = _225.y;
        float4 _235 = t0.SampleLevel(s4, float2(TEXCOORD3.z, TEXCOORD3.w), 0.0f);
        float _236 = _235.x;
        float _237 = _235.y;
        float _252 = mad(_217, 1.96321070194244384765625f, _216);
        float _253 = mad(_227, 1.96321070194244384765625f, _226);
        float _254 = mad(_237, 1.96321070194244384765625f, _236);
        float _257 = mad(_207, 1.96321070194244384765625f, _206) * 0.25f;
        bool _286 = ((abs(mad(_252, 0.25f, _257 + (_158 * (-0.5f)))) + abs(mad(_160, 0.5f, mad(_159, 0.5f, -_162)))) + abs(mad(_254, 0.25f, (_161 * (-0.5f)) + (_253 * 0.25f)))) <= ((abs(mad(_161, 0.5f, mad(_158, 0.5f, -_162))) + abs(mad(_253, 0.25f, _257 + (_159 * (-0.5f))))) + abs(mad(_254, 0.25f, (_252 * 0.25f) + (_160 * (-0.5f)))));
        float _291 = asfloat(cb0_m[24u].y);
        float _294 = asfloat(cb0_m[24u].x);
        float _295 = _286 ? _291 : _294;
        float _297 = _286 ? _158 : _159;
        float _298 = _286 ? _161 : _160;
        float _299 = _297 - _162;
        float _300 = _298 - _162;
        bool _307 = abs(_299) >= abs(_300);
        float _308 = _307 ? ((_162 + _297) * 0.5f) : ((_162 + _298) * 0.5f);
        float _311 = _307 ? (-_295) : _295;
        float _312 = _311 * 0.5f;
        float _314 = TEXCOORD.x + (_286 ? 0.0f : _312);
        float _316 = TEXCOORD.y + (_286 ? _312 : 0.0f);
        float _317 = abs(_307 ? _299 : _300) * 0.25f;
        float _318 = _286 ? _294 : 0.0f;
        float _319 = _286 ? 0.0f : _291;
        float _320 = _318 * 3.0f;
        float _321 = _319 * 3.0f;
        float _322 = mad(_318, -2.0f, _314);
        float _323 = mad(_319, -2.0f, _316);
        float _324 = mad(_318, 2.0f, _314);
        float _325 = mad(_319, 2.0f, _316);
        float _332 = mad(_318, -9.0f, _322);
        float _333 = mad(_319, -9.0f, _323);
        float _336 = mad(_318, 9.0f, _324);
        float _337 = mad(_319, 9.0f, _325);
        float4 _340 = t0.Sample(s4, float2(_322, _323));
        float4 _345 = t0.Sample(s4, float2(_324, _325));
        float4 _350 = t0.Sample(s4, float2(mad(_318, -3.0f, _322), mad(_319, -3.0f, _323)));
        float4 _355 = t0.Sample(s4, float2(mad(_318, 3.0f, _324), mad(_319, 3.0f, _325)));
        float4 _360 = t0.Sample(s4, float2(mad(_318, -6.0f, _322), mad(_319, -6.0f, _323)));
        float4 _365 = t0.Sample(s4, float2(mad(_318, 6.0f, _324), mad(_319, 6.0f, _325)));
        float4 _370 = t0.Sample(s4, float2(_332, _333));
        float4 _375 = t0.Sample(s4, float2(_336, _337));
        float _378 = mad(_340.y, 1.96321070194244384765625f, _340.x);
        uint _379 = asuint(_378);
        float _380 = mad(_345.y, 1.96321070194244384765625f, _345.x);
        uint _381 = asuint(_380);
        float _382 = mad(_350.y, 1.96321070194244384765625f, _350.x);
        float _383 = mad(_355.y, 1.96321070194244384765625f, _355.x);
        float _384 = mad(_360.y, 1.96321070194244384765625f, _360.x);
        float _385 = mad(_365.y, 1.96321070194244384765625f, _365.x);
        float _386 = mad(_370.y, 1.96321070194244384765625f, _370.x);
        float _387 = mad(_375.y, 1.96321070194244384765625f, _375.x);
        bool _394 = _317 <= abs(_378 - _308);
        bool _395 = _317 <= abs(_382 - _308);
        bool _396 = _317 <= abs(_384 - _308);
        bool _403 = _317 <= abs(_380 - _308);
        bool _404 = _317 <= abs(_383 - _308);
        bool _405 = _317 <= abs(_385 - _308);
        float _410;
        float _412;
        float _416;
        float _418;
        _410 = _333;
        _412 = _332;
        _416 = _337;
        _418 = _336;
        uint _408;
        float _411;
        float _413;
        uint _415;
        float _417;
        float _419;
        uint _421;
        uint _407 = _379;
        uint _414 = _381;
        uint _420 = 3u;
        float _422;
        float _423;
        for (;;)
        {
            _422 = asfloat(_407);
            _423 = asfloat(_414);
            if (int(_420) < 1)
            {
                break;
            }
            _421 = _420 - 1u;
            uint _434 = min(_421, 4u);
            float3 _447 = float3(_63[_434].x, _63[_434].y, _63[_434].z);
            float _448 = dp3_f32(float3(float(_403), float(_404), float(_405)), _447);
            _419 = mad(-_448, _320, _418);
            _417 = mad(-_448, _321, _416);
            uint _453 = min(_420, 4u);
            float4 _466 = float4(_63[_453].x, _63[_453].y, _63[_453].z, _63[_453].w);
            _415 = (_448 > 0.0f) ? asuint(dp4_f32(float4(_380, _383, _385, _387), _466)) : _414;
            float _473 = dp3_f32(float3(float(_394), float(_395), float(_396)), _447);
            _413 = mad(_473, _320, _412);
            _411 = mad(_473, _321, _410);
            _408 = (_473 > 0.0f) ? asuint(dp4_f32(float4(_378, _382, _384, _386), _466)) : _407;
            _407 = _408;
            _410 = _411;
            _412 = _413;
            _414 = _415;
            _416 = _417;
            _418 = _419;
            _420 = _421;
            continue;
        }
        float _480 = _286 ? (TEXCOORD.x - _412) : (TEXCOORD.y - _410);
        float _483 = _286 ? (_418 - TEXCOORD.x) : (_416 - TEXCOORD.y);
        bool _484 = _483 > _480;
        bool _487 = (_162 - _308) < 0.0f;
        bool _489 = ((_484 ? _422 : _423) - _308) < 0.0f;
        float _500 = mad(-(1.0f / (_483 + _480)), _484 ? _480 : _483, 0.5f) * (((!(_487 || _489)) || (_487 && _489)) ? 0.0f : _311);
        float4 _507 = t0.SampleLevel(s4, float2(TEXCOORD.x + (_286 ? 0.0f : _500), TEXCOORD.y + (_286 ? _500 : 0.0f)), 0.0f);
        float _508 = _507.x;
        float _509 = _507.y;
        float _510 = _507.z;
        _520 = mad(_198, mad((_235.z + (_225.z + (_205.z + _215.z))) + (_154.z + (_144.z + (_136 + (_113.z + _123.z)))), 0.111111111938953399658203125f, -_510), _510);
        _521 = mad(_198, mad((_237 + (_227 + (_207 + _217))) + (_156 + (_146 + (_135 + (_115 + _125)))), 0.111111111938953399658203125f, -_509), _509);
        _522 = mad(_198, mad((_155 + (_145 + (_134 + (_114 + _124)))) + (_236 + ((_206 + _216) + _226)), 0.111111111938953399658203125f, -_508), _508);
    }
    else
    {
        _520 = _136;
        _521 = _135;
        _522 = _134;
    }
    SV_Target.x = _522;
    SV_Target.y = _521;
    SV_Target.z = _520;
    SV_Target.w = _133.w;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    TEXCOORD1 = stage_input.TEXCOORD1;
    TEXCOORD2 = stage_input.TEXCOORD2;
    TEXCOORD3 = stage_input.TEXCOORD3;
    TEXCOORD4 = stage_input.TEXCOORD4;
    frag_main();

    // SV_Target.xyz = RenderIntermediatePass(SV_Target.xyz);
    if (HDR_ENABLED)
    {
        float3 x = SV_Target.xyz;
        x = max(x, 0);
        x = RenderIntermediatePass_Decode(x);

        #if HALO1_BLOOM > 0
            float3 b = sqrt(bloom.SampleLevel(s4, TEXCOORD.xy, 0).xyz) * (0.333 * GS.Bloom);
        #endif
        #if HALO1_BLOOM == 1
            x += b;
        #elif HALO1_BLOOM == 2
            x = b;
        #endif

        x = NeupowHQ(x, HDR_PEAK, 5.5 * GS.WhiteClip);

        x *= HDR_INTSCALING;
        x = RenderIntermediatePass_Encode(x);
        SV_Target.xyz = x;
    }
    
    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
