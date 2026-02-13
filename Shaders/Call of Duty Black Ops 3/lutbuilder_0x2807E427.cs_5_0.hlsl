// ---- Created with 3Dmigoto v1.2.45 on Fri Nov 28 19:25:04 2025

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

Texture2D<float3> postEffect0 : register(t0); //LUT: 1024x1024 = 32x32x32

// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

Texture1D<float4> IniParams : register(t120);
Texture2D<float4> StereoParams : register(t125);
RWTexture3D<float4> lut3D : register(u0); // dcl_uav_typed_texture3d (float,float,float,float) u0



float3 RestoreChrominance1(float3 targetColor, float sourceChrominance, float targetChrominance, uint colorSpace = CS_DEFAULT)
{
	float chrominanceRatio = safeDivision(sourceChrominance, targetChrominance, 1);
	return SetChrominance(targetColor, chrominanceRatio);
}

[numthreads(4,4,4)] //dcl_thread_group 4, 4, 4
void main(uint3 vThreadID : SV_DispatchThreadID)
{

  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;
  

  /// LOAD TEX (w/ some PostFX idk) ///
  r0.xz = vThreadID.zz;                                 //mov r0.xz, vThreadID.zzzz
  r1.xyzw = (uint4)postFxControlE.xzyw;                 //ftou r1.xyzw, cb8[14].xzyw
  r0.yw = r1.xz;                                        // mov r0.yw, r1.xxxz
  r0.xyzw = (uint4)r0.xyzw << int4(5,5,5,5);            //ishl r0.xyzw, r0.xyzw, l(5, 5, 5, 5)
  r2.xy = (int2)r0.zw + (int2)vThreadID.xy;             //iadd r2.xy, r0.zwzz, vThreadID.xyxx
  r0.xy = (int2)r0.xy + (int2)vThreadID.xy;             //iadd r0.xy, r0.xyxx, vThreadID.xyxx
  r2.zw = float2(0,0);                                  //mov r2.zw, l(0,0,0,0)
  r2.xyz = postEffect0.Load(r2.xyz).xyz;                //ld_indexable(texture2d)(float,float,float,float) r2.xyz, r2.xyzw, t0.xyzw
  r2.xyz = postFxControlF.yyy * r2.xyz; //scaling

  r0.zw = float2(0,0);
  r0.xyz = postEffect0.Load(r0.xyz).xyz;
  r0.xyz = r0.xyz * postFxControlF.xxx + r2.xyz; //scaling and apply

  r1.xz = vThreadID.zz;
  r1.xyzw = (uint4)r1.xyzw << int4(5,5,5,5);
  r2.xy = (int2)r1.xy + (int2)vThreadID.xy;
  r1.xy = (int2)r1.zw + (int2)vThreadID.xy;
  r2.zw = float2(0,0);
  r2.xyz = postEffect0.Load(r2.xyz).xyz;
  r0.xyz = r2.xyz * postFxControlF.zzz + r0.xyz; //scaling and apply

  r1.zw = float2(0,0);
  r1.xyz = postEffect0.Load(r1.xyz).xyz;
  r0.xyz = r1.xyz * postFxControlF.www + r0.xyz; //scaling and apply

  /// COLOR GRADE (but gets applied later) ///
  r0.w = dot(r0.xyz, postFxConst15.xyz);
  r1.xyzw = saturate(r0.wwww * postFxConst16.xzyw + postFxConst17.xzyw);
  r1.y = r1.y * r1.w;
  r2.xyz = r1.xyz * r1.xyz;
  r1.xyz = r1.xyz * float3(-2,-2,-2) + float3(3,3,3);
  r1.xzw = r2.xyz * r1.xyz;
  r0.w = r1.x + r1.w;
  r0.w = r2.y * r1.y + r0.w;
  r0.w = 1 / r0.w;
  r1.xyz = r1.xzw * r0.www;

#if CUSTOM_LUTBUILDER_NEUTRAL > 0
  r0.xyz = LUTNeutralize(vThreadID.xyz, r0.xyz, 32u, GS.LUTBuilderNeutral);
#endif

  /// DECODE Rec709 ///
  // {
  //   r2.xyz = float3(0.0989999995,0.0989999995,0.0989999995) + r0.xyz; 
  //   r2.xyz = float3(0.909918129,0.909918129,0.909918129) * r2.xyz;

  //   r2.xyz = log2(r2.xyz);
  //   r2.xyz = float3(2.22222233,2.22222233,2.22222233) * r2.xyz;
  //   r2.xyz = exp2(r2.xyz);

  //   r3.xyz = cmp(float3(0.0810000002,0.0810000002,0.0810000002) >= r0.xyz);
  //   r4.xyz = float3(0.222222224,0.222222224,0.222222224) * r0.xyz;
  //   r2.xyz = r3.xyz ? r4.xyz : r2.xyz;
  // }
  r2.xyz = DecodeRec709(r0.xyz);

  // float3 colorRaw = r2.xyz;

  // lut3D[uint3(vThreadID.xyz)] = float4(r2.xyz, 1);
  // return;
  
  /// COLOR GRADE ///
  r0.w = dot(r2.xyz, postFxConst18.xyz);
  r3.x = postFxConst18.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst21.xyz);
  r3.y = postFxConst21.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst24.xyz);
  r3.z = postFxConst24.w + r0.w;
  r0.w = saturate(dot(r3.xyz, r1.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst27.w * r0.w;
  r3.x = exp2(r0.w);
  r0.w = dot(r2.xyz, postFxConst19.xyz);
  r4.x = postFxConst19.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst22.xyz);
  r4.y = postFxConst22.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst25.xyz);
  r4.z = postFxConst25.w + r0.w;
  r0.w = saturate(dot(r4.xyz, r1.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst28.w * r0.w;
  r3.y = exp2(r0.w);
  r0.w = dot(r2.xyz, postFxConst20.xyz);
  r4.x = postFxConst20.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst23.xyz);
  r4.y = postFxConst23.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst26.xyz);
  r4.z = postFxConst26.w + r0.w;
  r0.w = saturate(dot(r4.xyz, r1.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst29.w * r0.w;
  r3.z = exp2(r0.w);
  r1.x = saturate(dot(r3.xyz, postFxConst27.xyz));
  r1.y = saturate(dot(r3.xyz, postFxConst28.xyz));
  r1.z = saturate(dot(r3.xyz, postFxConst29.xyz));
  r1.xyz = r1.xyz + -r2.xyz;
  r1.xyz = postFxConst15.www * r1.xyz + r2.xyz;
  r1.xyz = postFxConst60.yyy * r1.xyz;
  r0.w = dot(r0.xyz, postFxConst00.xyz);
  r0.x = dot(r0.xyz, postFxConst30.xyz);
  r3.xyzw = saturate(r0.xxxx * postFxConst31.xzyw + postFxConst32.xzyw);
  r0.xyzw = saturate(r0.wwww * postFxConst01.xzyw + postFxConst02.xzyw);
  r0.y = r0.y * r0.w;
  r4.xyz = r0.xyz * r0.xyz;
  r0.xyz = r0.xyz * float3(-2,-2,-2) + float3(3,3,3);
  r0.xzw = r4.xyz * r0.xyz;
  r1.w = r0.x + r0.w;
  r0.y = r4.y * r0.y + r1.w;
  r0.y = 1 / r0.y;
  r0.xyz = r0.xzw * r0.yyy;

  // lut3D[uint3(vThreadID.xyz)] = float4(r0.xyz, 1);
  // return;

  r0.w = dot(r2.xyz, postFxConst03.xyz);
  r4.x = postFxConst03.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst06.xyz);
  r4.y = postFxConst06.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst09.xyz);
  r4.z = postFxConst09.w + r0.w;
  r0.w = saturate(dot(r4.xyz, r0.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst12.w * r0.w;
  r4.x = exp2(r0.w);
  r0.w = dot(r2.xyz, postFxConst04.xyz);
  r5.x = postFxConst04.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst07.xyz);
  r5.y = postFxConst07.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst10.xyz);
  r5.z = postFxConst10.w + r0.w;
  r0.w = saturate(dot(r5.xyz, r0.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst13.w * r0.w;
  r4.y = exp2(r0.w);
  r0.w = dot(r2.xyz, postFxConst05.xyz);
  r5.x = postFxConst05.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst08.xyz);
  r5.y = postFxConst08.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst11.xyz);
  r5.z = postFxConst11.w + r0.w;
  r0.x = saturate(dot(r5.xyz, r0.xyz));
  r0.x = log2(r0.x);
  r0.x = postFxConst14.w * r0.x;
  r4.z = exp2(r0.x);
  r0.x = saturate(dot(r4.xyz, postFxConst12.xyz));
  r0.y = saturate(dot(r4.xyz, postFxConst13.xyz));
  r0.z = saturate(dot(r4.xyz, postFxConst14.xyz));
  r0.xyz = r0.xyz + -r2.xyz;
  r0.xyz = postFxConst00.www * r0.xyz + r2.xyz;
  r0.xyz = r0.xyz * postFxConst60.xxx + r1.xyz;
  r3.y = r3.y * r3.w;
  r1.xyz = r3.xyz * r3.xyz;
  r3.xyz = r3.xyz * float3(-2,-2,-2) + float3(3,3,3);
  r1.xzw = r3.xyz * r1.xyz;
  r0.w = r1.x + r1.w;
  r0.w = r1.y * r3.y + r0.w;
  r0.w = 1 / r0.w;
  r1.xyz = r1.xzw * r0.www;

  // lut3D[uint3(vThreadID.xyz)] = float4(r0.xyz, 1);
  // return;

  r0.w = dot(r2.xyz, postFxConst33.xyz);
  r3.x = postFxConst33.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst36.xyz);
  r3.y = postFxConst36.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst39.xyz);
  r3.z = postFxConst39.w + r0.w;
  r0.w = saturate(dot(r3.xyz, r1.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst42.w * r0.w;
  r3.x = exp2(r0.w);
  r0.w = dot(r2.xyz, postFxConst34.xyz);
  r4.x = postFxConst34.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst37.xyz);
  r4.y = postFxConst37.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst40.xyz);
  r4.z = postFxConst40.w + r0.w;
  r0.w = saturate(dot(r4.xyz, r1.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst43.w * r0.w;
  r3.y = exp2(r0.w);
  r0.w = dot(r2.xyz, postFxConst35.xyz);
  r4.x = postFxConst35.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst38.xyz);
  r4.y = postFxConst38.w + r0.w;
  r0.w = dot(r2.xyz, postFxConst41.xyz);
  r4.z = postFxConst41.w + r0.w;
  r0.w = saturate(dot(r4.xyz, r1.xyz));
  r0.w = log2(r0.w);
  r0.w = postFxConst44.w * r0.w;
  r3.z = exp2(r0.w);
  r1.x = saturate(dot(r3.xyz, postFxConst42.xyz));
  r1.y = saturate(dot(r3.xyz, postFxConst43.xyz));
  r1.z = saturate(dot(r3.xyz, postFxConst44.xyz));
  r1.xyz = r1.xyz + -r2.xyz;
  r1.xyz = postFxConst30.www * r1.xyz + r2.xyz;
  r0.xyz = r1.xyz * postFxConst60.zzz + r0.xyz;
  r0.xyz = r2.xyz * postFxConst60.www + r0.xyz;

  // lut3D[uint3(vThreadID.xyz)] = float4(r0.xyz, 1);
  // return;

  r0.w = dot(r0.xyz, postFxControl0.xyz);
  r0.w = postFxControl0.w + r0.w;
  r1.x/* w */ = saturate(r0.w/* w */);
  r0.w = dot(r0.xyz, postFxControl1.xyz); //Start of WEIRD SWIZZLE?
  r0.x = dot(r0.xyz, postFxControl2.xyz);
  r1.z = saturate(postFxControl2.w + r0.x);
  r1.y = saturate(postFxControl1.w + r0.w);

  /// COLOR GRADE DONE ///
  // lut3D[uint3(vThreadID.xyz)] = float4(r1.xyz, 1);
  // return;

  /// ENCODE Rec709 ///
  // {
  //   r0.xyzw = log2(r1.xyzw);
  //   r0.xyzw = float4(0.449999988,0.449999988,0.449999988,0.449999988) * r0.xyzw;
  //   r0.xyzw = exp2(r0.xyzw);
  //   r0.xyzw = r0.xyzw * float4(1.09899998,1.09899998,1.09899998,1.09899998) + float4(-0.0989999995,-0.0989999995,-0.0989999995,-0.0989999995);
  //   r2.xyzw = cmp(float4(0.0179999992,0.0179999992,0.0179999992,0.0179999992) >= r1/* .wyzw */); //WEIRD SWIZZLE!
  //   r1.xyzw = float4(4.5,4.5,4.5,4.5) * r1/* .wyzw */; //WEIRD SWIZZLE!
  //   r0.xyzw = r2.xyzw ? r1.xyzw : r0.xyzw;
  // }
  r0.xyz = EncodeRec709(r1.xyz);

  /// SATURATION (very rare) ///
  r1.x = dot(r0.xyz/* wyz */, float3(0.212599993,0.715200007,0.0722000003)); //WEIRD SWIZZLE!
  r2.xyzw = saturate(r1.xxxx * postFxControl3.xyzw + postFxControl4.xyzw);
  r1.xyzw = saturate(r1.xxxx * postFxControl5.xyzw + postFxControl6.xyzw);
  r3.x = dot(r2.xyzw, postFxControl8.xyzw);
  r3.y = dot(r1.xyzw, postFxControl9.xyzw);
  r3.x = r3.x + r3.y;
  r3.x/* w */ = postFxControl7.x/* x */ + r3.x/* x */; //r
  r4.x = dot(r2.xyzw, postFxControlA.xyzw);
  r2.x = dot(r2.xyzw, postFxControlC.xyzw);
  r2.y = dot(r1.xyzw, postFxControlB.xyzw);
  r1.x = dot(r1.xyzw, postFxControlD.xyzw);
  r1.x = r2.x + r1.x;
  r3.z = postFxControl7.z + r1.x; //b
  r1.x = r4.x + r2.y;
  r3.y = postFxControl7.y + r1.x; //g
  r1.xyz/* w */ = r3.xyz/* w */ + -r0.xyz/* .wyzw */; //WEIRD SWIZZLE!
  r0.xyz/* w */ = postFxControl7.w * r1.xyz/* w */ + r0.xyz/* w */; //desat final

  /// DECODE Rec709 ///

  // {
  //   r1.xyzw = float4(0.0989999995,0.0989999995,0.0989999995,0.0989999995) + r0.xyzw;
  //   r1.xyzw = float4(0.909918129,0.909918129,0.909918129,0.909918129) * r1.xyzw;

  //   r1.xyzw = log2(r1.xyzw);
  //   r1.xyzw = float4(2.22222233,2.22222233,2.22222233,2.22222233) * r1.xyzw;
  //   r1.xyzw = exp2(r1.xyzw);

  //   r2.xyzw = cmp(float4(0.0810000002,0.0810000002,0.0810000002,0.0810000002) >= r0/* .wyzw */); //WEIRD SWIZZLE!
  //   r0.xyzw = float4(0.222222224,0.222222224,0.222222224,0.222222224) * r0/* .wyzw */;  //WEIRD SWIZZLE!
  //   r0.xyzw = r2.xyzw ? r0.xyzw : r1.xyzw;
  // }


#if CUSTOM_LUTBUILDER_DECODE == 0
  r0.xyz = BT709_To_BT2020(DecodeRec709(r0.xyz));
#else
  float3 color709 = r0.xyz;
  float3 color2020 = BT709_To_BT2020(r0.xyz);

  color709 = DecodeRec709(color709);
  color2020 = DecodeRec709(color2020);

  color709 = BT709_To_BT2020(color709);

  r0.xyz = RestoreHueAndChrominance(color2020, color709, 1, GS.LUTBuilderExpansionChrominanceCorrect, 0, FLT_MAX, GS.LUTBuilderExpansionLuminanceCorrect, CS_BT2020);
#endif

#if CUSTOM_LUTBUILDER_HIGHLIGHTSAT > 0
  r0.xyz = CorrectPerChannelTonemapHiglightsDesaturationBo3(r0.xyz, 1.0, 1 - GS.LUTBuilderHighlightSat, GS.LUTBuilderHighlightSatHighlightsOnly, CS_BT2020);
#endif 

  // r0.xyzw = float4(32768,32768,32768,32768) * r0.xyzw; //eww
  // r0.xyz = max(0, r0.xyz);

  lut3D[uint3(vThreadID.xyz)].xyzw = float4(r0.xyz, 1); //store_uav_typed u0.xyzw, vThreadID.xyzz, r0.xyzw
  return;
}

/* //
// Generated by Microsoft (R) HLSL Shader Compiler 9.30.9200.16384
//
//
// Buffer Definitions: 
//
// cbuffer PostFxCBuffer
// {
//
//   float4 postFxControl0;             // Offset:    0 Size:    16
//   float4 postFxControl1;             // Offset:   16 Size:    16
//   float4 postFxControl2;             // Offset:   32 Size:    16
//   float4 postFxControl3;             // Offset:   48 Size:    16
//   float4 postFxControl4;             // Offset:   64 Size:    16
//   float4 postFxControl5;             // Offset:   80 Size:    16
//   float4 postFxControl6;             // Offset:   96 Size:    16
//   float4 postFxControl7;             // Offset:  112 Size:    16
//   float4 postFxControl8;             // Offset:  128 Size:    16
//   float4 postFxControl9;             // Offset:  144 Size:    16
//   float4 postFxControlA;             // Offset:  160 Size:    16
//   float4 postFxControlB;             // Offset:  176 Size:    16
//   float4 postFxControlC;             // Offset:  192 Size:    16
//   float4 postFxControlD;             // Offset:  208 Size:    16
//   float4 postFxControlE;             // Offset:  224 Size:    16
//   float4 postFxControlF;             // Offset:  240 Size:    16
//   float4 postFxConst00;              // Offset:  256 Size:    16
//   float4 postFxConst01;              // Offset:  272 Size:    16
//   float4 postFxConst02;              // Offset:  288 Size:    16
//   float4 postFxConst03;              // Offset:  304 Size:    16
//   float4 postFxConst04;              // Offset:  320 Size:    16
//   float4 postFxConst05;              // Offset:  336 Size:    16
//   float4 postFxConst06;              // Offset:  352 Size:    16
//   float4 postFxConst07;              // Offset:  368 Size:    16
//   float4 postFxConst08;              // Offset:  384 Size:    16
//   float4 postFxConst09;              // Offset:  400 Size:    16
//   float4 postFxConst10;              // Offset:  416 Size:    16
//   float4 postFxConst11;              // Offset:  432 Size:    16
//   float4 postFxConst12;              // Offset:  448 Size:    16
//   float4 postFxConst13;              // Offset:  464 Size:    16
//   float4 postFxConst14;              // Offset:  480 Size:    16
//   float4 postFxConst15;              // Offset:  496 Size:    16
//   float4 postFxConst16;              // Offset:  512 Size:    16
//   float4 postFxConst17;              // Offset:  528 Size:    16
//   float4 postFxConst18;              // Offset:  544 Size:    16
//   float4 postFxConst19;              // Offset:  560 Size:    16
//   float4 postFxConst20;              // Offset:  576 Size:    16
//   float4 postFxConst21;              // Offset:  592 Size:    16
//   float4 postFxConst22;              // Offset:  608 Size:    16
//   float4 postFxConst23;              // Offset:  624 Size:    16
//   float4 postFxConst24;              // Offset:  640 Size:    16
//   float4 postFxConst25;              // Offset:  656 Size:    16
//   float4 postFxConst26;              // Offset:  672 Size:    16
//   float4 postFxConst27;              // Offset:  688 Size:    16
//   float4 postFxConst28;              // Offset:  704 Size:    16
//   float4 postFxConst29;              // Offset:  720 Size:    16
//   float4 postFxConst30;              // Offset:  736 Size:    16
//   float4 postFxConst31;              // Offset:  752 Size:    16
//   float4 postFxConst32;              // Offset:  768 Size:    16
//   float4 postFxConst33;              // Offset:  784 Size:    16
//   float4 postFxConst34;              // Offset:  800 Size:    16
//   float4 postFxConst35;              // Offset:  816 Size:    16
//   float4 postFxConst36;              // Offset:  832 Size:    16
//   float4 postFxConst37;              // Offset:  848 Size:    16
//   float4 postFxConst38;              // Offset:  864 Size:    16
//   float4 postFxConst39;              // Offset:  880 Size:    16
//   float4 postFxConst40;              // Offset:  896 Size:    16
//   float4 postFxConst41;              // Offset:  912 Size:    16
//   float4 postFxConst42;              // Offset:  928 Size:    16
//   float4 postFxConst43;              // Offset:  944 Size:    16
//   float4 postFxConst44;              // Offset:  960 Size:    16
//   float4 postFxConst45;              // Offset:  976 Size:    16 [unused]
//   float4 postFxConst46;              // Offset:  992 Size:    16 [unused]
//   float4 postFxConst47;              // Offset: 1008 Size:    16 [unused]
//   float4 postFxConst48;              // Offset: 1024 Size:    16 [unused]
//   float4 postFxConst49;              // Offset: 1040 Size:    16 [unused]
//   float4 postFxConst50;              // Offset: 1056 Size:    16 [unused]
//   float4 postFxConst51;              // Offset: 1072 Size:    16 [unused]
//   float4 postFxConst52;              // Offset: 1088 Size:    16 [unused]
//   float4 postFxConst53;              // Offset: 1104 Size:    16 [unused]
//   float4 postFxConst54;              // Offset: 1120 Size:    16 [unused]
//   float4 postFxConst55;              // Offset: 1136 Size:    16 [unused]
//   float4 postFxConst56;              // Offset: 1152 Size:    16 [unused]
//   float4 postFxConst57;              // Offset: 1168 Size:    16 [unused]
//   float4 postFxConst58;              // Offset: 1184 Size:    16 [unused]
//   float4 postFxConst59;              // Offset: 1200 Size:    16 [unused]
//   float4 postFxConst60;              // Offset: 1216 Size:    16
//   float4 postFxConst61;              // Offset: 1232 Size:    16 [unused]
//   float4 postFxConst62;              // Offset: 1248 Size:    16 [unused]
//   float4 postFxConst63;              // Offset: 1264 Size:    16 [unused]
//   float4 postFxBloom00;              // Offset: 1280 Size:    16 [unused]
//   float4 postFxBloom01;              // Offset: 1296 Size:    16 [unused]
//   float4 postFxBloom02;              // Offset: 1312 Size:    16 [unused]
//   float4 postFxBloom03;              // Offset: 1328 Size:    16 [unused]
//   float4 postFxBloom04;              // Offset: 1344 Size:    16 [unused]
//   float4 postFxBloom05;              // Offset: 1360 Size:    16 [unused]
//   float4 postFxBloom06;              // Offset: 1376 Size:    16 [unused]
//   float4 postFxBloom07;              // Offset: 1392 Size:    16 [unused]
//   float4 postFxBloom08;              // Offset: 1408 Size:    16 [unused]
//   float4 postFxBloom09;              // Offset: 1424 Size:    16 [unused]
//   float4 postFxBloom10;              // Offset: 1440 Size:    16 [unused]
//   float4 postFxBloom11;              // Offset: 1456 Size:    16 [unused]
//   float4 postFxBloom12;              // Offset: 1472 Size:    16 [unused]
//   float4 postFxBloom13;              // Offset: 1488 Size:    16 [unused]
//   float4 postFxBloom14;              // Offset: 1504 Size:    16 [unused]
//   float4 postFxBloom15;              // Offset: 1520 Size:    16 [unused]
//   float4 postFxBloom16;              // Offset: 1536 Size:    16 [unused]
//   float4 postFxBloom17;              // Offset: 1552 Size:    16 [unused]
//   float4 postFxBloom18;              // Offset: 1568 Size:    16 [unused]
//   float4 postFxBloom19;              // Offset: 1584 Size:    16 [unused]
//   float4 postFxBloom20;              // Offset: 1600 Size:    16 [unused]
//   float4 postFxBloom21;              // Offset: 1616 Size:    16 [unused]
//   float4 postFxBloom22;              // Offset: 1632 Size:    16 [unused]
//   float4 postFxBloom23;              // Offset: 1648 Size:    16 [unused]
//   float4 postFxBloom24;              // Offset: 1664 Size:    16 [unused]
//   float4 postFxBloom25;              // Offset: 1680 Size:    16 [unused]
//   float4 filterTap[8];               // Offset: 1696 Size:   128 [unused]
//   float4 postfxViewMatrix0;          // Offset: 1824 Size:    16 [unused]
//   float4 postfxViewMatrix1;          // Offset: 1840 Size:    16 [unused]
//   float4 postfxViewMatrix2;          // Offset: 1856 Size:    16 [unused]
//   float4 postfxViewMatrix3;          // Offset: 1872 Size:    16 [unused]
//   float4 postfxProjMatrix0;          // Offset: 1888 Size:    16 [unused]
//   float4 postfxProjMatrix1;          // Offset: 1904 Size:    16 [unused]
//   float4 postfxProjMatrix2;          // Offset: 1920 Size:    16 [unused]
//   float4 postfxProjMatrix3;          // Offset: 1936 Size:    16 [unused]
//   float4 postfxViewProjMatrix0;      // Offset: 1952 Size:    16 [unused]
//   float4 postfxViewProjMatrix1;      // Offset: 1968 Size:    16 [unused]
//   float4 postfxViewProjMatrix2;      // Offset: 1984 Size:    16 [unused]
//   float4 postfxViewProjMatrix3;      // Offset: 2000 Size:    16 [unused]
//
// }
//
//
// Resource Bindings:
//
// Name                                 Type  Format         Dim      HLSL Bind  Count
// ------------------------------ ---------- ------- ----------- -------------- ------
// postEffect0                       texture  float3          2d             t0      1 
// lut3D                                 UAV  float3          3d             u0      1 
// PostFxCBuffer                     cbuffer      NA          NA            cb8      1 
//
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// no Input
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// no Output
      0x00000000: cs_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB8[77], immediateIndexed
      0x0000001C: dcl_resource_texture2d (float,float,float,float) t0
      0x0000002C: dcl_uav_typed_texture3d (float,float,float,float) u0
      0x0000003C: dcl_input vThreadID.xyz
      0x00000044: dcl_temps 6
      0x0000004C: dcl_thread_group 4, 4, 4
   0  0x0000005C: mov r0.xz, vThreadID.zzzz /
   1  0x0000006C: ftou r1.xyzw, cb8[14].xzyw /
   2  0x00000084: mov r0.yw, r1.xxxz /
   3  0x00000098: ishl r0.xyzw, r0.xyzw, l(5, 5, 5, 5) /
   4  0x000000C0: iadd r2.xy, r0.zwzz, vThreadID.xyxx /
   5  0x000000D8: iadd r0.xy, r0.xyxx, vThreadID.xyxx /
   6  0x000000F0: mov r2.zw, l(0,0,0,0) /
   7  0x00000110: ld_indexable(texture2d)(float,float,float,float) r2.xyz, r2.xyzw, t0.xyzw //
   8  0x00000134: mul r2.xyz, r2.xyzx, cb8[15].yyyy
   9  0x00000154: mov r0.zw, l(0,0,0,0)
  10  0x00000174: ld_indexable(texture2d)(float,float,float,float) r0.xyz, r0.xyzw, t0.xyzw
  11  0x00000198: mad r0.xyz, r0.xyzx, cb8[15].xxxx, r2.xyzx
  12  0x000001C0: mov r1.xz, vThreadID.zzzz
  13  0x000001D0: ishl r1.xyzw, r1.xyzw, l(5, 5, 5, 5)
  14  0x000001F8: iadd r2.xy, r1.xyxx, vThreadID.xyxx
  15  0x00000210: iadd r1.xy, r1.zwzz, vThreadID.xyxx
  16  0x00000228: mov r2.zw, l(0,0,0,0)
  17  0x00000248: ld_indexable(texture2d)(float,float,float,float) r2.xyz, r2.xyzw, t0.xyzw
  18  0x0000026C: mad r0.xyz, r2.xyzx, cb8[15].zzzz, r0.xyzx
  19  0x00000294: mov r1.zw, l(0,0,0,0)
  20  0x000002B4: ld_indexable(texture2d)(float,float,float,float) r1.xyz, r1.xyzw, t0.xyzw
  21  0x000002D8: mad r0.xyz, r1.xyzx, cb8[15].wwww, r0.xyzx
  22  0x00000300: dp3 r0.w, r0.xyzx, cb8[31].xyzx
  23  0x00000320: mad_sat r1.xyzw, r0.wwww, cb8[32].xzyw, cb8[33].xzyw
  24  0x0000034C: mul r1.y, r1.w, r1.y
  25  0x00000368: mul r2.xyz, r1.xyzx, r1.xyzx
  26  0x00000384: mad r1.xyz, r1.xyzx, l(-2.000000, -2.000000, -2.000000, 0.000000), l(3.000000, 3.000000, 3.000000, 0.000000)
  27  0x000003C0: mul r1.xzw, r1.xxyz, r2.xxyz
  28  0x000003DC: add r0.w, r1.w, r1.x
  29  0x000003F8: mad r0.w, r2.y, r1.y, r0.w
  30  0x0000041C: div r0.w, l(1.000000, 1.000000, 1.000000, 1.000000), r0.w
  31  0x00000444: mul r1.xyz, r0.wwww, r1.xzwx
  32  0x00000460: add r2.xyz, r0.xyzx, l(0.099000, 0.099000, 0.099000, 0.000000)
  33  0x00000488: mul r2.xyz, r2.xyzx, l(0.909918, 0.909918, 0.909918, 0.000000)
  34  0x000004B0: log r2.xyz, r2.xyzx
  35  0x000004C4: mul r2.xyz, r2.xyzx, l(2.222222, 2.222222, 2.222222, 0.000000)
  36  0x000004EC: exp r2.xyz, r2.xyzx
  37  0x00000500: ge r3.xyz, l(0.081000, 0.081000, 0.081000, 0.000000), r0.xyzx
  38  0x00000528: mul r4.xyz, r0.xyzx, l(0.222222, 0.222222, 0.222222, 0.000000)
  39  0x00000550: movc r2.xyz, r3.xyzx, r4.xyzx, r2.xyzx
  40  0x00000574: dp3 r0.w, r2.xyzx, cb8[34].xyzx
  41  0x00000594: add r3.x, r0.w, cb8[34].w
  42  0x000005B4: dp3 r0.w, r2.xyzx, cb8[37].xyzx
  43  0x000005D4: add r3.y, r0.w, cb8[37].w
  44  0x000005F4: dp3 r0.w, r2.xyzx, cb8[40].xyzx
  45  0x00000614: add r3.z, r0.w, cb8[40].w
  46  0x00000634: dp3_sat r0.w, r3.xyzx, r1.xyzx
  47  0x00000650: log r0.w, r0.w
  48  0x00000664: mul r0.w, r0.w, cb8[43].w
  49  0x00000684: exp r3.x, r0.w
  50  0x00000698: dp3 r0.w, r2.xyzx, cb8[35].xyzx
  51  0x000006B8: add r4.x, r0.w, cb8[35].w
  52  0x000006D8: dp3 r0.w, r2.xyzx, cb8[38].xyzx
  53  0x000006F8: add r4.y, r0.w, cb8[38].w
  54  0x00000718: dp3 r0.w, r2.xyzx, cb8[41].xyzx
  55  0x00000738: add r4.z, r0.w, cb8[41].w
  56  0x00000758: dp3_sat r0.w, r4.xyzx, r1.xyzx
  57  0x00000774: log r0.w, r0.w
  58  0x00000788: mul r0.w, r0.w, cb8[44].w
  59  0x000007A8: exp r3.y, r0.w
  60  0x000007BC: dp3 r0.w, r2.xyzx, cb8[36].xyzx
  61  0x000007DC: add r4.x, r0.w, cb8[36].w
  62  0x000007FC: dp3 r0.w, r2.xyzx, cb8[39].xyzx
  63  0x0000081C: add r4.y, r0.w, cb8[39].w
  64  0x0000083C: dp3 r0.w, r2.xyzx, cb8[42].xyzx
  65  0x0000085C: add r4.z, r0.w, cb8[42].w
  66  0x0000087C: dp3_sat r0.w, r4.xyzx, r1.xyzx
  67  0x00000898: log r0.w, r0.w
  68  0x000008AC: mul r0.w, r0.w, cb8[45].w
  69  0x000008CC: exp r3.z, r0.w
  70  0x000008E0: dp3_sat r1.x, r3.xyzx, cb8[43].xyzx
  71  0x00000900: dp3_sat r1.y, r3.xyzx, cb8[44].xyzx
  72  0x00000920: dp3_sat r1.z, r3.xyzx, cb8[45].xyzx
  73  0x00000940: add r1.xyz, -r2.xyzx, r1.xyzx
  74  0x00000960: mad r1.xyz, cb8[31].wwww, r1.xyzx, r2.xyzx
  75  0x00000988: mul r1.xyz, r1.xyzx, cb8[76].yyyy
  76  0x000009A8: dp3 r0.w, r0.xyzx, cb8[16].xyzx
  77  0x000009C8: dp3 r0.x, r0.xyzx, cb8[46].xyzx
  78  0x000009E8: mad_sat r3.xyzw, r0.xxxx, cb8[47].xzyw, cb8[48].xzyw
  79  0x00000A14: mad_sat r0.xyzw, r0.wwww, cb8[17].xzyw, cb8[18].xzyw
  80  0x00000A40: mul r0.y, r0.w, r0.y
  81  0x00000A5C: mul r4.xyz, r0.xyzx, r0.xyzx
  82  0x00000A78: mad r0.xyz, r0.xyzx, l(-2.000000, -2.000000, -2.000000, 0.000000), l(3.000000, 3.000000, 3.000000, 0.000000)
  83  0x00000AB4: mul r0.xzw, r0.xxyz, r4.xxyz
  84  0x00000AD0: add r1.w, r0.w, r0.x
  85  0x00000AEC: mad r0.y, r4.y, r0.y, r1.w
  86  0x00000B10: div r0.y, l(1.000000, 1.000000, 1.000000, 1.000000), r0.y
  87  0x00000B38: mul r0.xyz, r0.yyyy, r0.xzwx
  88  0x00000B54: dp3 r0.w, r2.xyzx, cb8[19].xyzx
  89  0x00000B74: add r4.x, r0.w, cb8[19].w
  90  0x00000B94: dp3 r0.w, r2.xyzx, cb8[22].xyzx
  91  0x00000BB4: add r4.y, r0.w, cb8[22].w
  92  0x00000BD4: dp3 r0.w, r2.xyzx, cb8[25].xyzx
  93  0x00000BF4: add r4.z, r0.w, cb8[25].w
  94  0x00000C14: dp3_sat r0.w, r4.xyzx, r0.xyzx
  95  0x00000C30: log r0.w, r0.w
  96  0x00000C44: mul r0.w, r0.w, cb8[28].w
  97  0x00000C64: exp r4.x, r0.w
  98  0x00000C78: dp3 r0.w, r2.xyzx, cb8[20].xyzx
  99  0x00000C98: add r5.x, r0.w, cb8[20].w
 100  0x00000CB8: dp3 r0.w, r2.xyzx, cb8[23].xyzx
 101  0x00000CD8: add r5.y, r0.w, cb8[23].w
 102  0x00000CF8: dp3 r0.w, r2.xyzx, cb8[26].xyzx
 103  0x00000D18: add r5.z, r0.w, cb8[26].w
 104  0x00000D38: dp3_sat r0.w, r5.xyzx, r0.xyzx
 105  0x00000D54: log r0.w, r0.w
 106  0x00000D68: mul r0.w, r0.w, cb8[29].w
 107  0x00000D88: exp r4.y, r0.w
 108  0x00000D9C: dp3 r0.w, r2.xyzx, cb8[21].xyzx
 109  0x00000DBC: add r5.x, r0.w, cb8[21].w
 110  0x00000DDC: dp3 r0.w, r2.xyzx, cb8[24].xyzx
 111  0x00000DFC: add r5.y, r0.w, cb8[24].w
 112  0x00000E1C: dp3 r0.w, r2.xyzx, cb8[27].xyzx
 113  0x00000E3C: add r5.z, r0.w, cb8[27].w
 114  0x00000E5C: dp3_sat r0.x, r5.xyzx, r0.xyzx
 115  0x00000E78: log r0.x, r0.x
 116  0x00000E8C: mul r0.x, r0.x, cb8[30].w
 117  0x00000EAC: exp r4.z, r0.x
 118  0x00000EC0: dp3_sat r0.x, r4.xyzx, cb8[28].xyzx
 119  0x00000EE0: dp3_sat r0.y, r4.xyzx, cb8[29].xyzx
 120  0x00000F00: dp3_sat r0.z, r4.xyzx, cb8[30].xyzx
 121  0x00000F20: add r0.xyz, -r2.xyzx, r0.xyzx
 122  0x00000F40: mad r0.xyz, cb8[16].wwww, r0.xyzx, r2.xyzx
 123  0x00000F68: mad r0.xyz, r0.xyzx, cb8[76].xxxx, r1.xyzx
 124  0x00000F90: mul r3.y, r3.w, r3.y
 125  0x00000FAC: mul r1.xyz, r3.xyzx, r3.xyzx
 126  0x00000FC8: mad r3.xyz, r3.xyzx, l(-2.000000, -2.000000, -2.000000, 0.000000), l(3.000000, 3.000000, 3.000000, 0.000000)
 127  0x00001004: mul r1.xzw, r1.xxyz, r3.xxyz
 128  0x00001020: add r0.w, r1.w, r1.x
 129  0x0000103C: mad r0.w, r1.y, r3.y, r0.w
 130  0x00001060: div r0.w, l(1.000000, 1.000000, 1.000000, 1.000000), r0.w
 131  0x00001088: mul r1.xyz, r0.wwww, r1.xzwx
 132  0x000010A4: dp3 r0.w, r2.xyzx, cb8[49].xyzx
 133  0x000010C4: add r3.x, r0.w, cb8[49].w
 134  0x000010E4: dp3 r0.w, r2.xyzx, cb8[52].xyzx
 135  0x00001104: add r3.y, r0.w, cb8[52].w
 136  0x00001124: dp3 r0.w, r2.xyzx, cb8[55].xyzx
 137  0x00001144: add r3.z, r0.w, cb8[55].w
 138  0x00001164: dp3_sat r0.w, r3.xyzx, r1.xyzx
 139  0x00001180: log r0.w, r0.w
 140  0x00001194: mul r0.w, r0.w, cb8[58].w
 141  0x000011B4: exp r3.x, r0.w
 142  0x000011C8: dp3 r0.w, r2.xyzx, cb8[50].xyzx
 143  0x000011E8: add r4.x, r0.w, cb8[50].w
 144  0x00001208: dp3 r0.w, r2.xyzx, cb8[53].xyzx
 145  0x00001228: add r4.y, r0.w, cb8[53].w
 146  0x00001248: dp3 r0.w, r2.xyzx, cb8[56].xyzx
 147  0x00001268: add r4.z, r0.w, cb8[56].w
 148  0x00001288: dp3_sat r0.w, r4.xyzx, r1.xyzx
 149  0x000012A4: log r0.w, r0.w
 150  0x000012B8: mul r0.w, r0.w, cb8[59].w
 151  0x000012D8: exp r3.y, r0.w
 152  0x000012EC: dp3 r0.w, r2.xyzx, cb8[51].xyzx
 153  0x0000130C: add r4.x, r0.w, cb8[51].w
 154  0x0000132C: dp3 r0.w, r2.xyzx, cb8[54].xyzx
 155  0x0000134C: add r4.y, r0.w, cb8[54].w
 156  0x0000136C: dp3 r0.w, r2.xyzx, cb8[57].xyzx
 157  0x0000138C: add r4.z, r0.w, cb8[57].w
 158  0x000013AC: dp3_sat r0.w, r4.xyzx, r1.xyzx
 159  0x000013C8: log r0.w, r0.w
 160  0x000013DC: mul r0.w, r0.w, cb8[60].w
 161  0x000013FC: exp r3.z, r0.w
 162  0x00001410: dp3_sat r1.x, r3.xyzx, cb8[58].xyzx
 163  0x00001430: dp3_sat r1.y, r3.xyzx, cb8[59].xyzx
 164  0x00001450: dp3_sat r1.z, r3.xyzx, cb8[60].xyzx
 165  0x00001470: add r1.xyz, -r2.xyzx, r1.xyzx
 166  0x00001490: mad r1.xyz, cb8[46].wwww, r1.xyzx, r2.xyzx
 167  0x000014B8: mad r0.xyz, r1.xyzx, cb8[76].zzzz, r0.xyzx
 168  0x000014E0: mad r0.xyz, r2.xyzx, cb8[76].wwww, r0.xyzx
 169  0x00001508: dp3 r0.w, r0.xyzx, cb8[0].xyzx
 170  0x00001528: add r0.w, r0.w, cb8[0].w
 171  0x00001548: mov_sat r1.xw, r0.wwww
 172  0x0000155C: dp3 r0.w, r0.xyzx, cb8[1].xyzx
 173  0x0000157C: dp3 r0.x, r0.xyzx, cb8[2].xyzx
 174  0x0000159C: add_sat r1.z, r0.x, cb8[2].w
 175  0x000015BC: add_sat r1.y, r0.w, cb8[1].w
 176  0x000015DC: log r0.xyzw, r1.xyzw
 177  0x000015F0: mul r0.xyzw, r0.xyzw, l(0.450000, 0.450000, 0.450000, 0.450000)
 178  0x00001618: exp r0.xyzw, r0.xyzw
 179  0x0000162C: mad r0.xyzw, r0.xyzw, l(1.099000, 1.099000, 1.099000, 1.099000), l(-0.099000, -0.099000, -0.099000, -0.099000)
 180  0x00001668: ge r2.xyzw, l(0.018000, 0.018000, 0.018000, 0.018000), r1.wyzw
 181  0x00001690: mul r1.xyzw, r1.wyzw, l(4.500000, 4.500000, 4.500000, 4.500000)
 182  0x000016B8: movc r0.xyzw, r2.xyzw, r1.xyzw, r0.xyzw
 183  0x000016DC: dp3 r1.x, r0.wyzw, l(0.212600, 0.715200, 0.072200, 0.000000)
 184  0x00001704: mad_sat r2.xyzw, r1.xxxx, cb8[3].xyzw, cb8[4].xyzw
 185  0x00001730: mad_sat r1.xyzw, r1.xxxx, cb8[5].xyzw, cb8[6].xyzw
 186  0x0000175C: dp4 r3.x, r2.xyzw, cb8[8].xyzw
 187  0x0000177C: dp4 r3.y, r1.xyzw, cb8[9].xyzw
 188  0x0000179C: add r3.x, r3.y, r3.x
 189  0x000017B8: add r3.xw, r3.xxxx, cb8[7].xxxx
 190  0x000017D8: dp4 r4.x, r2.xyzw, cb8[10].xyzw
 191  0x000017F8: dp4 r2.x, r2.xyzw, cb8[12].xyzw
 192  0x00001818: dp4 r2.y, r1.xyzw, cb8[11].xyzw
 193  0x00001838: dp4 r1.x, r1.xyzw, cb8[13].xyzw
 194  0x00001858: add r1.x, r1.x, r2.x
 195  0x00001874: add r3.z, r1.x, cb8[7].z
 196  0x00001894: add r1.x, r2.y, r4.x
 197  0x000018B0: add r3.y, r1.x, cb8[7].y
 198  0x000018D0: add r1.xyzw, -r0.wyzw, r3.xyzw
 199  0x000018F0: mad r0.xyzw, cb8[7].wwww, r1.xyzw, r0.xyzw
 200  0x00001918: add r1.xyzw, r0.xyzw, l(0.099000, 0.099000, 0.099000, 0.099000)
 201  0x00001940: mul r1.xyzw, r1.xyzw, l(0.909918, 0.909918, 0.909918, 0.909918)
 202  0x00001968: log r1.xyzw, r1.xyzw
 203  0x0000197C: mul r1.xyzw, r1.xyzw, l(2.222222, 2.222222, 2.222222, 2.222222)
 204  0x000019A4: exp r1.xyzw, r1.xyzw
 205  0x000019B8: ge r2.xyzw, l(0.081000, 0.081000, 0.081000, 0.081000), r0.wyzw
 206  0x000019E0: mul r0.xyzw, r0.wyzw, l(0.222222, 0.222222, 0.222222, 0.222222)
 207  0x00001A08: movc r0.xyzw, r2.xyzw, r0.xyzw, r1.xyzw
 208  0x00001A2C: mul r0.xyzw, r0.xyzw, l(32768.000000, 32768.000000, 32768.000000, 32768.000000)
 209  0x00001A54: store_uav_typed u0.xyzw, vThreadID.xyzz, r0.xyzw
 210  0x00001A6C: ret 
// Approximately 211 instruction slots used
 */