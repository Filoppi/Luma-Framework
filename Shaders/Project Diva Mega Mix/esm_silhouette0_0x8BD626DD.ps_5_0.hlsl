
cbuffer cb0_buf : register(b0)
{
    uint4 cb0_m : packoffset(c0);
};

cbuffer cb2_buf : register(b2)
{
    uint4 cb2_m[40] : packoffset(c0);
    /*
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
      float4 g_max_alpha : packoffset(c39); // only this is used!
      float4 g_morph_weight : packoffset(c40);
      float4 g_sss_param : packoffset(c41);
      float4 g_bump_depth : packoffset(c42);
      float4 g_intensity : packoffset(c43);
      float4 g_reflect_uv_scale : packoffset(c44);
      float4 g_shadow_position[2] : packoffset(c45);
      float4 g_shadow_direction[2] : packoffset(c47);
      float4 g_shadow_param : packoffset(c49);
      uint4 g_skip_flags : packoffset(c50);
    */
};

SamplerState s0 : register(s0);
SamplerState s4 : register(s4);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t4 : register(t4);

static float4 TEXCOORD;
static float4 SV_Target;

struct SPIRV_Cross_Input
{
    float4 SV_Position : SV_Position0;
    float4 TEXCOORD : TEXCOORD0;
    float4 TEXCOORD1 : TEXCOORD1;
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

void frag_main()
{
    uint _44 = spvBitfieldUExtract(cb0_m.w, 9u, 1u);
    float _78;
    if (_44 != 0u)
    {
        float _67;
        if (_44 == 1u)
        {
            _67 = t4.Sample(s4, float2(TEXCOORD.z, TEXCOORD.w)).w;
        }
        else
        {
            _67 = 0.0f;
        }
        _78 = _67;
    }
    else
    {
        _78 = t0.Sample(s0, float2(TEXCOORD.x, TEXCOORD.y)).w;
    }

    #if ESM_SILHOUETTE_DO_ALPHA_DISCARD
        // if too transparent, discard to white
        if ((asfloat(cb2_m[39u].z) >= _78) && (_78 != 1.0f)) discard;
    #endif

    // black with alpha
    SV_Target.x = 0.0f;
    SV_Target.y = 0.0f;
    SV_Target.z = 0.0f;
    SV_Target.w = _78;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();

    // optimized rg8u
    #if CUSTOM_ESM_DEPTH_NEW_SIZE > 0
        SV_Target = float4(SV_Target.xw, 0, 0);
    #endif

    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
