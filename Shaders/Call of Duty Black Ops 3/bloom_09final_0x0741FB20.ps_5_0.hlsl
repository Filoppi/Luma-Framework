// ---- Created with 3Dmigoto v1.3.16 on Thu Mar 26 12:48:18 2026

cbuffer PostFxCBuffer : register(b8)
{
  float4 postFxControl0 : packoffset(c0);
  float4 postFxControl1 : packoffset(c1);
  float4 postFxControl2 : packoffset(c2);
  float4 postFxControl3 : packoffset(c3);
  float4 postFxControl4 : packoffset(c4);
  float4 postFxControl5 : packoffset(c5);
  float4 postFxControl6 : packoffset(c6);
  float4 postFxControl7 : packoffset(c7);
  float4 postFxControl8 : packoffset(c8);
  float4 postFxControl9 : packoffset(c9);
  float4 postFxControlA : packoffset(c10);
  float4 postFxControlB : packoffset(c11);
  float4 postFxControlC : packoffset(c12);
  float4 postFxControlD : packoffset(c13);
  float4 postFxControlE : packoffset(c14);
  float4 postFxControlF : packoffset(c15);
  float4 postFxConst00 : packoffset(c16);
  float4 postFxConst01 : packoffset(c17);
  float4 postFxConst02 : packoffset(c18);
  float4 postFxConst03 : packoffset(c19);
  float4 postFxConst04 : packoffset(c20);
  float4 postFxConst05 : packoffset(c21);
  float4 postFxConst06 : packoffset(c22);
  float4 postFxConst07 : packoffset(c23);
  float4 postFxConst08 : packoffset(c24);
  float4 postFxConst09 : packoffset(c25);
  float4 postFxConst10 : packoffset(c26);
  float4 postFxConst11 : packoffset(c27);
  float4 postFxConst12 : packoffset(c28);
  float4 postFxConst13 : packoffset(c29);
  float4 postFxConst14 : packoffset(c30);
  float4 postFxConst15 : packoffset(c31);
  float4 postFxConst16 : packoffset(c32);
  float4 postFxConst17 : packoffset(c33);
  float4 postFxConst18 : packoffset(c34);
  float4 postFxConst19 : packoffset(c35);
  float4 postFxConst20 : packoffset(c36);
  float4 postFxConst21 : packoffset(c37);
  float4 postFxConst22 : packoffset(c38);
  float4 postFxConst23 : packoffset(c39);
  float4 postFxConst24 : packoffset(c40);
  float4 postFxConst25 : packoffset(c41);
  float4 postFxConst26 : packoffset(c42);
  float4 postFxConst27 : packoffset(c43);
  float4 postFxConst28 : packoffset(c44);
  float4 postFxConst29 : packoffset(c45);
  float4 postFxConst30 : packoffset(c46);
  float4 postFxConst31 : packoffset(c47);
  float4 postFxConst32 : packoffset(c48);
  float4 postFxConst33 : packoffset(c49);
  float4 postFxConst34 : packoffset(c50);
  float4 postFxConst35 : packoffset(c51);
  float4 postFxConst36 : packoffset(c52);
  float4 postFxConst37 : packoffset(c53);
  float4 postFxConst38 : packoffset(c54);
  float4 postFxConst39 : packoffset(c55);
  float4 postFxConst40 : packoffset(c56);
  float4 postFxConst41 : packoffset(c57);
  float4 postFxConst42 : packoffset(c58);
  float4 postFxConst43 : packoffset(c59);
  float4 postFxConst44 : packoffset(c60);
  float4 postFxConst45 : packoffset(c61);
  float4 postFxConst46 : packoffset(c62);
  float4 postFxConst47 : packoffset(c63);
  float4 postFxConst48 : packoffset(c64);
  float4 postFxConst49 : packoffset(c65);
  float4 postFxConst50 : packoffset(c66);
  float4 postFxConst51 : packoffset(c67);
  float4 postFxConst52 : packoffset(c68);
  float4 postFxConst53 : packoffset(c69);
  float4 postFxConst54 : packoffset(c70);
  float4 postFxConst55 : packoffset(c71);
  float4 postFxConst56 : packoffset(c72);
  float4 postFxConst57 : packoffset(c73);
  float4 postFxConst58 : packoffset(c74);
  float4 postFxConst59 : packoffset(c75);
  float4 postFxConst60 : packoffset(c76);
  float4 postFxConst61 : packoffset(c77);
  float4 postFxConst62 : packoffset(c78);
  float4 postFxConst63 : packoffset(c79);
  float4 postFxBloom00 : packoffset(c80);
  float4 postFxBloom01 : packoffset(c81);
  float4 postFxBloom02 : packoffset(c82);
  float4 postFxBloom03 : packoffset(c83);
  float4 postFxBloom04 : packoffset(c84);
  float4 postFxBloom05 : packoffset(c85);
  float4 postFxBloom06 : packoffset(c86);
  float4 postFxBloom07 : packoffset(c87);
  float4 postFxBloom08 : packoffset(c88);
  float4 postFxBloom09 : packoffset(c89);
  float4 postFxBloom10 : packoffset(c90);
  float4 postFxBloom11 : packoffset(c91);
  float4 postFxBloom12 : packoffset(c92);
  float4 postFxBloom13 : packoffset(c93);
  float4 postFxBloom14 : packoffset(c94);
  float4 postFxBloom15 : packoffset(c95);
  float4 postFxBloom16 : packoffset(c96);
  float4 postFxBloom17 : packoffset(c97);
  float4 postFxBloom18 : packoffset(c98);
  float4 postFxBloom19 : packoffset(c99);
  float4 postFxBloom20 : packoffset(c100);
  float4 postFxBloom21 : packoffset(c101);
  float4 postFxBloom22 : packoffset(c102);
  float4 postFxBloom23 : packoffset(c103);
  float4 postFxBloom24 : packoffset(c104);
  float4 postFxBloom25 : packoffset(c105);
  float4 filterTap[8] : packoffset(c106);
  float4 postfxViewMatrix0 : packoffset(c114);
  float4 postfxViewMatrix1 : packoffset(c115);
  float4 postfxViewMatrix2 : packoffset(c116);
  float4 postfxViewMatrix3 : packoffset(c117);
  float4 postfxProjMatrix0 : packoffset(c118);
  float4 postfxProjMatrix1 : packoffset(c119);
  float4 postfxProjMatrix2 : packoffset(c120);
  float4 postfxProjMatrix3 : packoffset(c121);
  float4 postfxViewProjMatrix0 : packoffset(c122);
  float4 postfxViewProjMatrix1 : packoffset(c123);
  float4 postfxViewProjMatrix2 : packoffset(c124);
  float4 postfxViewProjMatrix3 : packoffset(c125);
}

SamplerState biLinearClamp_s : register(s0);
Texture2D<float4> codeTexture0 : register(t0);
Texture2D<float4> codeTexture1 : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

void main(
  float4 v0 : TEXCOORD0,
  nointerpolation float3 v1 : TEXCOORD1,
  float4 v2 : SV_POSITION0,
  out float3 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 0;
  r0.yw = v1.xy;
  r1.xyzw = v0.xyxy + -r0.xyxw;
  r0.xyzw = v0.xyxy + r0.xwxy;
  r2.xyzw = codeTexture0.Sample(biLinearClamp_s, r1.xy).xyzw;
  r1.xyzw = codeTexture0.Sample(biLinearClamp_s, r1.zw).xyzw;
  r1.xyzw = r2.xyzw + r1.xyzw;
  r2.x = 0;
  r2.y = v1.z;
  r2.zw = v0.xy + -r2.xy;
  r2.xy = v0.xy + r2.xy;
  r3.xyzw = codeTexture0.Sample(biLinearClamp_s, r2.xy).xyzw;
  r2.xyzw = codeTexture0.Sample(biLinearClamp_s, r2.zw).xyzw;
  r1.xyzw = r2.xyzw + r1.xyzw;
  r2.xyzw = codeTexture0.Sample(biLinearClamp_s, v0.xy).xyzw;
  r1.xyzw = r2.xyzw + r1.xyzw;
  r1.xyzw = r1.xyzw + r3.xyzw;
  r2.xyzw = codeTexture0.Sample(biLinearClamp_s, r0.xy).xyzw;
  r0.xyzw = codeTexture0.Sample(biLinearClamp_s, r0.zw).xyzw;
  r1.xyzw = r2.xyzw + r1.xyzw;
  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.w = 0.142857149 * r0.w; //1/7
  r0.xyz = postFxBloom02.xyz * r0.xyz;
  r1.xyz = postFxBloom03.xyz * r0.www;
  r0.xyz = r0.xyz * float3(0.142857149,0.142857149,0.142857149) + r1.xyz;
  r1.xyz = codeTexture1.Sample(biLinearClamp_s, v0.xy).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.xyz = max(0, r0.xyz);

  // tonemap (Reinhard)
  //  r1.xyz = 1 + r0.xyz;
  //  r0.xyz = r0.xyz / r1.xyz;
  #if CUSTOM_SDR == 0
    #if CUSTOM_BLOOM_TONEMAP == 0
      //max channel reinhard
      {
        float y = max(r0.x, max(r0.y, r0.z)); /* GetLuminance(r0.xyz, CS_BT709); */
        if (y > 0) {
          float y1 = Reinhard::ReinhardSimple(y, 1);
          float ratio = y1 / y;
          r0.xyz *= ratio;
        }
      }
    #elif CUSTOM_BLOOM_TONEMAP == 1
      // jodie
      r0.xyz = Reinhard::Jodie(r0.xyz, 1, 0.25, CS_BT709);
    #endif
  #else
    //per channel reinhard
    r0.xyz = Reinhard::ReinhardSimple(r0.xyz, 1);
  #endif

  //contrast and stuff
  r0.xyz = sqrt(r0.xyz);
  r0.xyz = saturate(r0.xyz * postFxBloom12.xyz + postFxBloom13.xyz); //tint?
  r0.xyz = log2(r0.xyz);
  r0.xyz = postFxBloom14.xyz * r0.xyz; //gamma
  r0.xyz = exp2(r0.xyz);
  r0.xyz = saturate(r0.xyz * postFxBloom15.xyz + postFxBloom16.xyz); //tint?
  r0.xyz = r0.xyz * r0.xyz;

  //luminance based perchannel strength
  r0.w = dot(r0.xyz, postFxBloom17.xyz); 
  r1.x = /* saturate */(postFxBloom17.w + r0.w); //r

  r0.w = dot(r0.xyz, postFxBloom18.xyz);
  r1.y = /* saturate */(postFxBloom18.w + r0.w); //g

  r0.x = dot(r0.xyz, postFxBloom19.xyz);
  r1.z = /* saturate */(postFxBloom19.w + r0.x); //g

  r1.xyz = max(0, r1.xyz); //clean
  // r0.xyz = log2(r1.xyz);
  // r0.xyz = float3(0.449999988,0.449999988,0.449999988) * r0.xyz;
  // r0.xyz = exp2(r0.xyz);
  // r0.xyz = r0.xyz * float3(1.09899998,1.09899998,1.09899998) + float3(-0.0989999995,-0.0989999995,-0.0989999995);
  // r2.xyz = cmp(float3(0.0179999992,0.0179999992,0.0179999992) >= r1.xyz);
  // r1.xyz = float3(4.5,4.5,4.5) * r1.xyz;
  // r0.xyz = r2.xyz ? r1.xyz : r0.xyz;
  r0.xyz = EncodeRec709(r1.xyz);

  o0.xyz = /* 256.f * */ r0.xyz;
  #if CUSTOM_SDR > 0
    o0.xyz *= GS_Bloom;
  #endif
  return;
}