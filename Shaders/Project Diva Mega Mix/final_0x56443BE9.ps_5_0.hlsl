// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 09 22:01:31 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);

Texture2D<float4> prev_texture : register(t1); // CUSTOM_PS4BLUR_1

cbuffer Quad : register(b0) // bound though unused
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

float3 Saturation(float3 x, float s) {
  x = UCSTo(x, CS_BT709);
  x.yz *= s;
  x = UCSFrom(x, CS_BT709);
  x = max(0, x); //clamp cs
  return x;
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0
#if CUSTOM_PS4BLUR_1 > 0
  , out float3 o1 : SV_Target1 //r11b11g10f
#endif
)
{
  o0.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw;
  o0.w = saturate(o0.w); // unorm

  float3 x = o0.xyz;
  x = max(0, x);
  #if CUSTOM_SDR_1 == 1
    x = min(x, 1); // unorm
  #endif

  #if CUSTOM_PS4BLUR_1 > 0
    o1.xyz = x; // save to prev
    float3 prev = prev_texture.Sample(g_sampler_s, v1.xy).xyz; // load prev

    #if CUSTOM_PS4BLUR_1 == 1 // lerp ghosting
      // x = sqrt(lerp(prev * prev, x * x, 0.75)); //TODO: each scene can have a different blend factor... could it be stored in PV file?
      x = sqrt(lerp(x * x, prev * prev, GS.FrameBlendRatio));
    #elif CUSTOM_PS4BLUR_1 == 2 // horizontal interlacing
      float2 texSize = g_texel_size.zw;
      bool isEvenFrame = LumaSettings.FrameIndex % 2 == 0;
      bool isEvenRow = floor(v1.y * texSize.y) % 2 == 0; 
      if ((isEvenFrame && isEvenRow) || (!isEvenFrame && !isEvenRow)) {
        // noop
      } else {
        x = prev;
      }
    #endif
  #endif

  #if CUSTOM_SDR_1 == 1
    o0.xyz = x;
    return;
  #endif

  // intermediate decode
  x = DecodeIntermediate(x);
  
  // Saturation
  #if CUSTOM_COLORGRADE_SATORDER == 1 || CUSTOM_ALTSAT == 1
    float s = 1;
    #if CUSTOM_ALTSAT == 0
      s *= GS.CGSaturation;
    #endif
    #if CUSTOM_ALTSAT == 1
      x = 1.f;
    #endif
    x = Saturation(x, GS.CGSaturation);
  #endif

  // intermediate scaling
  x *= GS.IntermediateScalingCached;

  // intermediate encode
  x = EncodeIntermediate(x);

  o0.xyz = x;
  return;
}

/*
// ShadPS4 RenderDoc capture of frame blend shader

#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
#if defined(GL_ARB_gpu_shader_int64)
#extension GL_ARB_gpu_shader_int64 : require
#else
#error No extension available for 64-bit integers.
#endif
#extension GL_EXT_fragment_shader_barycentric : require

struct full_result_i32x2
{
    int _m0;
    int _m1;
};

struct full_result_u32x2
{
    uint _m0;
    uint _m1;
};

struct frexp_result_f32
{
    float _m0;
    int _m1;
};

layout(set = 0, binding = 0, std430) readonly buffer ssbo_1
{
    uint data[];
} ssbo_1_1;

layout(push_constant, std430) uniform AuxData
{
    float xoffset;
    float yoffset;
    float xscale;
    float yscale;
    uvec4 ud_regs0;
    uvec4 ud_regs1;
    uvec4 ud_regs2;
    uvec4 ud_regs3;
    uvec4 buf_offsets0;
    uvec4 buf_offsets1;
    uvec2 buf_offsets2;
} push_data;

layout(set = 0, binding = 1) uniform texture2D fs_img0;
layout(set = 0, binding = 2) uniform texture2D fs_img1;
layout(set = 0, binding = 3) uniform sampler fs_samp0;

layout(location = 0) pervertexEXT in vec4 fs_in_attr0_p[3];
layout(location = 0) out vec4 frag_color0;

void main()
{
    // texcoord stuff
    precise float _87 = fs_in_attr0_p[1u].x - fs_in_attr0_p[0u].x;
    precise float _94 = fs_in_attr0_p[1u].y - fs_in_attr0_p[0u].y;
    precise float _100 = fs_in_attr0_p[2u].x - fs_in_attr0_p[0u].x;
    precise float _101 = fma(_100, gl_BaryCoordEXT.z, fma(_87, gl_BaryCoordEXT.y, fs_in_attr0_p[0u].x));
    precise float _106 = fs_in_attr0_p[2u].y - fs_in_attr0_p[0u].y;
    precise float _107 = fma(_106, gl_BaryCoordEXT.z, fma(_94, gl_BaryCoordEXT.y, fs_in_attr0_p[0u].y));

    // sampling
    vec4 _112 = textureLod(sampler2D(fs_img0, fs_samp0), vec2(_101, _107), 0.0); // prev
    float _113 = _112.x;
    float _114 = _112.y;
    float _115 = _112.z;
    float _116 = _112.w;

    vec4 _121 = textureLod(sampler2D(fs_img1, fs_samp0), vec2(_101, _107), 0.0); // curr
    float _122 = _121.x;
    float _123 = _121.y;
    float _124 = _121.z; // w is used later as _121.w

    // squared
    precise float _130 = _113 * _113;
    precise float _131 = _114 * _114;
    precise float _132 = _115 * _115;
    precise float _134 = _122 * _122;

    // lerp
    precise float _135 = _134 + (-_130);
    precise float _137 = _123 * _123;
    precise float _138 = _137 + (-_131);
    precise float _140 = _124 * _124;
    precise float _141 = _140 + (-_132);
    
    float _142 = uintBitsToFloat(ssbo_1_1.data[11u + (bitfieldExtract(push_data.buf_offsets0.x, int(0u), int(8u)) >> 2u)]); // lerp factor (0.75)

    precise float _143 = _142 * _135;
    precise float _144 = _143 + _130;
    precise float _145 = _142 * _138;
    precise float _146 = _145 + _131;
    precise float _147 = _142 * _141;
    precise float _148 = _147 + _132;
    precise float _152 = _121.w - _116;

    // sqrt
    precise float _153 = inversesqrt(_144) * _144;
    precise float _154 = inversesqrt(_146) * _146;
    precise float _155 = inversesqrt(_148) * _148;

    precise float _156 = _142 * _152;
    precise float _157 = _156 + _116;

    frag_color0.x = _153;
    frag_color0.y = _154;
    frag_color0.z = _155;
    frag_color0.w = _157;
}


*/