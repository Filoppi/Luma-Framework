#ifndef LUMA_MELE_TONEMAP_SCENE
#define LUMA_MELE_TONEMAP_SCENE

// Native pixel-shader offsets. The layout is identical in every stage-1 permutation.
cbuffer PSOffsetConstants : register(b2)
{
   float4 ScreenPositionScaleBias : packoffset(c0);
   float4 MinZ_MaxZRatio : packoffset(c1);
   float4 DynamicScale : packoffset(c2);
}

// Trilogy-wide native near/far depth-of-field composite, transcribed from the live CSOs. Register names mirror
// the decompiled source so the block stays diffable against the original bytecode. Include after _Globals and
// the permutation's DOF textures and samplers, which it reads at whatever slots that entry point assigns.
//   uv         scene coordinate already scaled by DynamicScale (decompiled r0.xy)
//   blurEnable near/far MinMaxBlurClamp enable flags            (decompiled r0.zw)
//   scene      sampled scene color                              (decompiled r1.xyz)
float3 MELE_CompositeDOF(float2 uv, float2 blurEnable, float3 scene)
{
   float4 r0, r1, r2, r3, r4;
   r0.xy = uv;
   r0.zw = blurEnable;
   r1.xyz = scene;
   r1.w = DOFTexture.Sample(DOFTextureSampler_s, r0.xy).x;
   r1.w = saturate(r1.w);
   r1.w = r1.w * MinZ_MaxZRatio.z + -MinZ_MaxZRatio.w;
   r1.w = max(1.00000001e-07, r1.w);
   r1.w = 1 / r1.w;
   r2.xyzw = DOFBlurredNear.Sample(DOFBlurredNearSampler_s, r0.xy).xyzw;
   r2.xyzw = r0.zzzz ? r2.xyzw : 0;
   r3.xyzw = DOFBlurredFar.Sample(DOFBlurredFarSampler_s, r0.xy).xyzw;
   r3.xyzw = r0.wwww ? r3.xyzw : 0;
   r0.z = cmp(0 < PackedParameters.w);
   r0.w = -PackedParameters.x + r1.w;
   r4.x = saturate(-PackedParameters.y * r0.w);
   r4.x = max(9.99999975e-05, r4.x);
   r4.x = log2(r4.x);
   r4.x = PackedParameters.w * r4.x;
   r4.x = exp2(r4.x);
   r1.w = PackedParameters.x + -r1.w;
   r4.yz = float2(300, 500) * PackedParameters.zz;
   r4.yz = max(float2(9.99999975e-05, 9.99999975e-05), r4.yz);
   r1.w = saturate(r1.w / r4.y);
   r4.x = r0.z ? r4.x : r1.w;
   r1.w = saturate(PackedParameters.y * r0.w);
   r1.w = max(9.99999975e-05, r1.w);
   r1.w = log2(r1.w);
   r1.w = PackedParameters.w * r1.w;
   r1.w = exp2(r1.w);
   r0.w = -PackedParameters.y + r0.w;
   r0.w = saturate(r0.w / r4.z);
   r4.y = r0.z ? r1.w : r0.w;
   r4.xy = MinMaxBlurClamp.xy * r4.xy;
   r4.xy = r4.xy * r4.xy;
   r0.w = max(r4.x, r2.w);
   r1.w = min(r4.y, r3.w);
   r2.w = saturate(r0.w);
   r3.w = r2.w * -2 + 3;
   r2.w = r2.w * r2.w;
   r4.y = r3.w * r2.w;
   r4.z = saturate(DOFKernelParams.y * r1.w);
   r4.x = saturate(DOFKernelParams.x * r0.w);
   r0.zw = r0.zz ? r4.yz : r4.xz;
   r3.xyz = r3.xyz + -r1.xyz;
   r3.xyz = r0.www * r3.xyz + r1.xyz;
   r2.xyz = -r3.xyz + r2.xyz;
   r1.xyz = r0.zzz * r2.xyz + r3.xyz;
   return r1.xyz;
}

// Native bloom screen blend, straight-RGB form used by the ME2 filmic, ME3, and analytic permutations. Tinted
// bloom and screen-blend weight come out separately because the filmic path composites twice: into the linear
// scene and into the native per-channel curve. The ME1/ME2 non-filmic path carries an internal BRG rotation on
// these same operations and is deliberately not routed through here.
float3 MELE_BloomScreenBlend(float2 uv, float3 scene, out float weight)
{
   float4 r0;
   r0.xyz = BlurredImageSeperateBloom.Sample(BlurredImageSeperateBloomSampler_s, uv).xyz * LumaSettings.GameSettings.BloomIntensity;
   r0.xyz = BloomTintAndScreenBlendThreshold.xyz * r0.xyz;
   r0.w = dot(scene, float3(0.298999995, 0.587000012, 0.114));
   r0.xyzw = float4(4, 4, 4, -3) * r0.xyzw;
   r0.w = exp2(r0.w);
   weight = saturate(BloomTintAndScreenBlendThreshold.w * r0.w);
   return r0.xyz;
}
#endif // LUMA_MELE_TONEMAP_SCENE