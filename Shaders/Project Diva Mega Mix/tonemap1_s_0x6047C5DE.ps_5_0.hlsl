// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 09 22:32:43 2025
cbuffer ToneMap : register(b1)
{
  float4 g_exposure : packoffset(c0);
  float4 g_fade_color : packoffset(c1);
  float4 g_tone_scale : packoffset(c2);
  float4 g_tone_offset : packoffset(c3);
  float4 g_texcoord_transforms[4] : packoffset(c4);
}

SamplerState g_samplers_0__s : register(s0);
SamplerState g_samplers_1__s : register(s1);
SamplerState g_samplers_2__s : register(s2);
SamplerState g_samplers_4__s : register(s4);
SamplerState g_samplers_5__s : register(s5);
SamplerState g_samplers_6__s : register(s6);
SamplerState g_samplers_7__s : register(s7);
Texture2D<float4> g_textures_0_ : register(t0);
Texture2D<float4> g_textures_1_ : register(t1);
Texture2D<float4> g_textures_2_ : register(t2);
Texture2D<float4> g_textures_4_ : register(t4);
Texture2D<float4> g_textures_5_ : register(t5);
Texture2D<float4> g_textures_6_ : register(t6); //prev sprite render target
Texture2D<float4> g_textures_7_ : register(t7);


// 3Dmigoto declarations
#define cmp -

#define TONEMAP_COMPLEX
#include "./common1.hlsl"



//Color + Bloom + Sprites + Tonemap + Fade (The biggest tone_map)
//tonemap_ycc_exponent_composite_back_alpha_mask_ps
//Default Future Tone w/ Sprites
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
    //w: sprites exposure?
  float4 v4 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  float3 colorUntonemapped, colorTonemapped;
  float colorUntonemappedMask;

  //color
  r0.xyzw = g_textures_0_.Sample(g_samplers_0__s, v1.xy).xyzw;
  #if CUSTOM_TESTBGSPRITES == 1
    r0 = 0;
  #endif
  colorUntonemappedMask = r0.w;
  
  //bloom
  r1.xyz = g_textures_1_.Sample(g_samplers_1__s, v1.zw).xyz * GS.BloomStrength;
  #if CUSTOM_TESTBGSPRITES == 1
    r1.xyz = 0;
  #endif
  r1.w = cmp(0 < v3.z); //threshold
  r1.xyz = r1.xyz + r0.xyz; //bloom add
  r0.xyz = r1.www ? r1.xyz : r0.xyz; //dont draw if not surpasses threshold

  //lensflare
  r1.x = cmp(0 < g_texcoord_transforms[0].w);
  if (r1.x != 0) {
    r1.xyz = g_textures_4_.Sample(g_samplers_4__s, v2.xy).xyz;
    r1.xyz = r1.xyz * r1.xyz;
    r0.xyz = r1.xyz * g_texcoord_transforms[0].www + r0.xyz;
  }
  r1.x = cmp(0 < g_texcoord_transforms[2].w);
  if (r1.x != 0) {
    r1.xyz = g_textures_5_.Sample(g_samplers_5__s, v2.zw).xyz;
    r1.xyz = r1.xyz * r1.xyz;
    r0.xyz = r1.xyz * g_texcoord_transforms[2].www + r0.xyz;
  }

  //idk
  r1.x = cmp(0 < v4.z);
  if (r1.x != 0) {
    r1.xyz = g_textures_7_.Sample(g_samplers_7__s, v4.xy).xyz;
    r0.xyz = r1.xyz + r0.xyz;
  }

  colorUntonemapped = r0.xyz;

  //composite sprites that was rendered before this (complex)
  {
    r1.xyz = g_textures_6_.Sample(g_samplers_6__s, v1.xy).xyz; //color
    #if CUSTOM_TESTBGSPRITES == 2
      r1.xyz = 0;
    #endif
    float3 colorSprites = r1.xyz;

    r1.xyz = saturate(r1.xyz); //clamp SDR (a must, else becomes black by compositing onto color)

    //luma inverse tonemap (up to ~400@203)
    r2.xyz = float3(0.959999979,0.959999979,0.959999979) * r1.xyz;
    r1.y = dot(r2.xyz, float3(0.300000012,0.589999974,0.109999999));

    r1.w = log2(abs(r1.y));
    r1.w = g_tone_offset.w * r1.w;
    r1.w = exp2(r1.w);

    r1.w = 1 + -r1.w;
    r2.x = cmp(0 < r1.w);
    r2.y = log2(r1.w);
    r1.w = r2.x ? r2.y : r1.w;

    r2.z = v3.w * r1.w; //sprites exposure?
    r1.w = r1.y * 2 + -1;
    r1.w = max(0, r1.w);

    r1.w = r1.w * r1.w; //bruh
    r1.w = r1.w * r1.w; //bruh
    r1.w = r1.w * r1.w; //bruh
    r1.w = -r1.w * r1.w + 1;

    r1.xz = r1.xz * float2(0.959999979,0.959999979) + -r1.yy;
    r1.y = r1.y * r1.w;
    r1.w = cmp(r1.y != 0.000000);
    r3.x = 1 / r1.y;
    r1.y = r1.w ? r3.x : r1.y;

    r1.xy = r1.xz * r1.yy + float2(1,1);
    r2.xy = r1.xy * r2.zz;
    r2.w = dot(r2.xzy, float3(-0.508469999,1.69490004,-0.186440006));
    
    r0.w = min(1, r0.w);
    r1.x = 1 + -r0.w;
    // r0.xyz = r2.xwy * r1.xxx + r0.xyz; //sprite * mask + 3d?
    Tonemap_SaveSprites(r2.xwy, r1.x, r0.xyz, colorUntonemapped);
  }

  //linear colorU
  //colorUntonemapped = gamma_to_linear(colorUntonemapped, GCT_POSITIVE, 2.2);
  colorUntonemapped = gamma_sRGB_to_linear(colorUntonemapped, GCT_POSITIVE);

  //tonemapper
  {
    // r0.y = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
    // r0.xz = r0.xz + -r0.yy;
    // r1.x = v3.y * r0.y; //exposure 
    // r1.y = 0;
    // r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, r1.xy, 0).yx;
    // r0.y = v3.x * r1.x; //sat
    // r1.xz = r0.yy * r0.xz;
    // r0.xz = r0.yy * r0.xz + r1.yy;
    // r0.y = dot(r1.xyz, float3(-0.508475006,1,-0.186441004));
    // r0.xyz = r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz;
    // r0.xyz = /* saturate */(r0.xyz);

    Tonemap_ResolveComplexWithExposure(r0.xyz, colorUntonemapped, v3);
  }

  r0.xyz = Tonemap_Do(colorUntonemapped, r0.xyz, v1.xy, g_textures_0_/* , g_tone_offset */);

  //fade
  {
    r1.x = cmp(0 < g_fade_color.w);
    r1.yzw = g_fade_color.xyz + -r0.xyz;
    r1.yzw = g_fade_color.www * r1.yzw + r0.xyz;
    r2.xy = cmp(g_tone_scale.ww == float2(0,2));
    r3.xyz = g_fade_color.xyz + r0.xyz;
    r4.xyz = g_fade_color.xyz * r0.xyz;
    r2.yzw = r2.yyy ? r3.xyz : r4.xyz;
    r1.yzw = r2.xxx ? r1.yzw : r2.yzw;
    r0.xyz = r1.xxx ? r1.yzw : r0.xyz;
  }

  //out
  o0 = r0;
  Tonemap_Out(o0);
  return;
}

/*
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_textures_0_.Sample(g_samplers_0__s, v1.xy).xyzw;
  r1.xyz = g_textures_1_.Sample(g_samplers_1__s, v1.zw).xyz;
  r1.w = cmp(0 < v3.z);
  r1.xyz = r1.xyz + r0.xyz;
  r0.xyz = r1.www ? r1.xyz : r0.xyz;
  r1.x = cmp(0 < g_texcoord_transforms[0].w);
  if (r1.x != 0) {
    r1.xyz = g_textures_4_.Sample(g_samplers_4__s, v2.xy).xyz;
    r1.xyz = r1.xyz * r1.xyz;
    r0.xyz = r1.xyz * g_texcoord_transforms[0].www + r0.xyz;
  }
  r1.x = cmp(0 < g_texcoord_transforms[2].w);
  if (r1.x != 0) {
    r1.xyz = g_textures_5_.Sample(g_samplers_5__s, v2.zw).xyz;
    r1.xyz = r1.xyz * r1.xyz;
    r0.xyz = r1.xyz * g_texcoord_transforms[2].www + r0.xyz;
  }
  r1.x = cmp(0 < v4.z);
  if (r1.x != 0) {
    r1.xyz = g_textures_7_.Sample(g_samplers_7__s, v4.xy).xyz;
    r0.xyz = r1.xyz + r0.xyz;
  }
  r1.xyz = g_textures_6_.Sample(g_samplers_6__s, v1.xy).xyz;
  r2.xyz = float3(0.959999979,0.959999979,0.959999979) * r1.xyz;
  r1.y = dot(r2.xyz, float3(0.300000012,0.589999974,0.109999999));
  r1.w = log2(abs(r1.y));
  r1.w = g_tone_offset.w * r1.w;
  r1.w = exp2(r1.w);
  r1.w = 1 + -r1.w;
  r2.x = cmp(0 < r1.w);
  r2.y = log2(r1.w);
  r1.w = r2.x ? r2.y : r1.w;
  r2.z = v3.w * r1.w;
  r1.w = r1.y * 2 + -1;
  r1.w = max(0, r1.w);
  r1.w = r1.w * r1.w;
  r1.w = r1.w * r1.w;
  r1.w = r1.w * r1.w;
  r1.w = -r1.w * r1.w + 1;
  r1.xz = r1.xz * float2(0.959999979,0.959999979) + -r1.yy;
  r1.y = r1.y * r1.w;
  r1.w = cmp(r1.y != 0.000000);
  r3.x = 1 / r1.y;
  r1.y = r1.w ? r3.x : r1.y;
  r1.xy = r1.xz * r1.yy + float2(1,1);
  r2.xy = r1.xy * r2.zz;
  r2.w = dot(r2.xzy, float3(-0.508469999,1.69490004,-0.186440006));
  r0.w = min(1, r0.w);
  r1.x = 1 + -r0.w;
  r0.xyz = r2.xwy * r1.xxx + r0.xyz;
  r0.y = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
  r0.xz = r0.xz + -r0.yy;
  r1.x = v3.y * r0.y;
  r1.y = 0;
  r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, r1.xy, 0).yx;
  r0.y = v3.x * r1.x;
  r1.xz = r0.yy * r0.xz;
  r0.xz = r0.yy * r0.xz + r1.yy;
  r0.y = dot(r1.xyz, float3(-0.508475006,1,-0.186441004));
  r0.xyz = saturate(r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz);
  r1.x = cmp(0 < g_fade_color.w);
  r1.yzw = g_fade_color.xyz + -r0.xyz;
  r1.yzw = g_fade_color.www * r1.yzw + r0.xyz;
  r2.xy = cmp(g_tone_scale.ww == float2(0,2));
  r3.xyz = g_fade_color.xyz + r0.xyz;
  r4.xyz = g_fade_color.xyz * r0.xyz;
  r2.yzw = r2.yyy ? r3.xyz : r4.xyz;
  r1.yzw = r2.xxx ? r1.yzw : r2.yzw;
  o0.xyz = r1.xxx ? r1.yzw : r0.xyz;
  o0.w = r0.w;
  return;
*/

/*
ARB Future Tone Arcade

!!ARBfp1.0
OPTION ARB_fragment_program_shadow;
OPTION NV_fragment_program2;
ATTRIB a_color0 = fragment.color;
ATTRIB a_color1 = fragment.color.secondary;
ATTRIB a_tex_color0 = fragment.texcoord[0];
ATTRIB a_tex_color1 = fragment.texcoord[1];
ATTRIB a_tex_color2 = fragment.texcoord[2];
ATTRIB a_tex_normal0 = fragment.texcoord[0];
ATTRIB a_tex_normal1 = fragment.texcoord[0];
ATTRIB a_tex_specular = fragment.texcoord[0];
ATTRIB a_tex_parency = fragment.texcoord[1];
ATTRIB a_tex_lucency = fragment.texcoord[1];
ATTRIB a_tex_shadow0 = fragment.texcoord[2];
ATTRIB a_tex_shadow1 = fragment.texcoord[7];
ATTRIB a_tex_litproj = fragment.texcoord[7];
ATTRIB a_eye = fragment.texcoord[6];
ATTRIB a_normal = fragment.texcoord[5];
ATTRIB a_tangent = fragment.texcoord[3];
ATTRIB a_binormal = fragment.texcoord[4];
ATTRIB a_reflect = fragment.texcoord[5];
ATTRIB a_lit = fragment.texcoord[2];
ATTRIB a_aniso_tangent = fragment.texcoord[7];
ATTRIB a_reflect_v = fragment.texcoord[7];
ATTRIB a_fogcoord = fragment.fogcoord;
ATTRIB a_position = fragment.position;
ATTRIB a_facing = fragment.facing;
SHORT OUTPUT o_color = result.color;
LONG OUTPUT o_depth = result.depth;
SHORT TEMP _tmp0, _tmp1, _tmp2;
PARAM p_fb_isize = program.env[0];
PARAM p_blend_color = program.env[2];
PARAM p_offset_color = program.env[3];
PARAM p_fog_color = program.env[4];
PARAM p_height_fog_color = program.env[11];
PARAM p_lit_luce = program.env[8];
PARAM p_max_alpha = program.env[21];
PARAM p_lit_dir = program.env[5];
PARAM p_esm_k = program.env[23];
PARAM p_chara_color_rim = program.env[33];
PARAM p_chara_color0 = program.env[34];
PARAM p_chara_color1 = program.env[35];
PARAM nt_mtx[3] = { program.env[26 .. 28] };
PARAM _red_coef_709 = { 1.5748, 1.0, 0.0000 };
PARAM _grn_coef_709 = { -0.4681, 1.0, -0.1873 };
PARAM _blu_coef_709 = { 0.0000, 1.0, 1.8556 };
PARAM _red_coef_601 = { 1.4022, 1.0, 0.0 };
PARAM _grn_coef_601 = { -0.714486, 1.0, -0.345686 };
PARAM _blu_coef_601 = { 0.0, 1.0, 1.7710 };
PARAM _y_coef_601 = { 0.2989, 0.5866, 0.1145 };
PARAM _cb_coef_601 = { -0.1687747, -0.3312253, 0.5 };
PARAM _cr_coef_601 = { 0.5, -0.4183426, -0.0816574 };
ATTRIB a_tex0 = fragment.texcoord[0];
ATTRIB a_tex1 = fragment.texcoord[1];
ATTRIB a_tex2 = fragment.texcoord[2];
ATTRIB a_tex3 = fragment.texcoord[3];
ATTRIB exposure = fragment.texcoord[4];
PARAM to_ybr = { 0.30, 0.59, 0.11 };
PARAM to_rgb = { -0.508475, 1.0, -0.186441 };
PARAM p_flare_coef = program.local[1];
PARAM p_fade_color = program.local[2];
PARAM p_tone_scale = program.local[4];
PARAM p_tone_offset = program.local[5];
PARAM p_fade_func = program.local[6];
PARAM p_inv_tone = program.local[7];
TEMP col, sum, back;
TEMP ybr;
TEMP res, tmp;
 TEX sum, a_tex0, texture[0], 2D;
 TEX col, a_tex1, texture[1], 2D;
 MOVC col.w, exposure.z;
 ADD sum.xyz (GT.w), sum, col;
  TEX col, a_tex2, texture[4], 2D;
  MUL col.xyz, col, col;
  MAD sum.xyz, col, p_flare_coef.x, sum;
   TEX col, a_tex3, texture[5], 2D;
   MUL col.xyz, col, col;
   MAD sum.xyz, col, p_flare_coef.y, sum;
  TEX col, a_tex1, texture[6], 2D;
  ADD sum.xyz, sum, col;
  TEX back, a_tex0, texture[6], 2D;
  MUL back.xyz, back, 0.96;
   DP3 tmp.x, back, {0.3, 0.59, 0.11};
   POW tmp.y, tmp.x, p_inv_tone.x;
   SUBC tmp.y, 1, tmp.y;
   LG2 tmp.y (GT.y), tmp.y;
   MUL tmp.y, tmp.y, exposure.w;
   MAD tmp.z, tmp.x, 2.0, -1.0;
   MAX tmp.z, tmp.z, 0.0;
   MUL tmp.z, tmp.z, tmp.z;
   MUL tmp.z, tmp.z, tmp.z;
   MUL tmp.z, tmp.z, tmp.z;
   MUL tmp.z, tmp.z, tmp.z;
   SUB tmp.z, 1.0, tmp.z;
   SUB back.xyz, back, tmp.x;
   MULC tmp.x, tmp.x, tmp.z;
   RCP tmp.x (NE.x), tmp.x;
   MUL back.xyz, back, tmp.x;
   ADD back.xyz, back, 1.0;
   MUL back.xyz, back, tmp.y;
   MOV back.y, tmp.y;
   DP3 back.y, back, {-0.50847, 1.6949, -0.18644};
  MIN sum.w, sum.w, 1;
  SUB tmp.w, 1, sum.w;
  MAD sum.xyz, tmp.w, back, sum;
 MOV o_color.w, sum.w;
  DP3 ybr.y, sum, to_ybr;
  SUB ybr.xz, sum, ybr.y;
  MUL ybr.y, ybr.y, exposure.y;
  TEX col, ybr.y, texture[2], 1D;
  MUL col.w, col.w, exposure.x;
  MUL col.xz, col.w, ybr;
  ADD res.xz, col.y, col;
  DP3 res.y, col, to_rgb;
 MAD_SAT res.xyz, res, p_tone_scale, p_tone_offset;
  SUBC tmp.xyz, p_fade_func.x, { 0, 1, 2 };
  ADD o_color.xyz, p_fade_color , res;
  LRP o_color.xyz (EQ.x), p_fade_color.w, p_fade_color, res;
  MUL o_color.xyz (EQ.y), p_fade_color , res;
END

*/