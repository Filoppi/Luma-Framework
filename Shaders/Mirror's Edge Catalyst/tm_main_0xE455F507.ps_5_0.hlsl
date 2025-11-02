#include "./Includes/tonemap-mecatalyst.hlsli"

// cbuffer _Globals : register(b0)
// {
//    float2 invPixelSize : packoffset(c0);
//    float preBlendAmount : packoffset(c0.z);
//    float postAddAmount : packoffset(c0.w);
//    float4 parametricTonemapParams : packoffset(c1);
//    float4 parametricTonemapToeCoeffs : packoffset(c2);
//    float4 parametricTonemapShoulderCoeffs : packoffset(c3);
//    float3 filmGrainColorScale : packoffset(c4);
//    float4 filmGrainTextureScaleAndOffset : packoffset(c5);
//    float4 color : packoffset(c6);
//    float4 colorMatrix0 : packoffset(c7);
//    float4 colorMatrix1 : packoffset(c8);
//    float4 colorMatrix2 : packoffset(c9);
//    float4 ironsightsDofParams : packoffset(c10);
//    float4 filmicLensDistortParams : packoffset(c11);
//    float4 colorScale : packoffset(c12);
//    float4 runnersVisionColor : packoffset(c13);
//    float3 depthScaleFactors : packoffset(c14);
//    float4 dofParams : packoffset(c15);
//    float4 dofParams2 : packoffset(c16);
//    float4 dofDebugParams : packoffset(c17);
//    float3 bloomScale : packoffset(c18);
//    float3 lensDirtExponent : packoffset(c19);
//    float3 lensDirtFactor : packoffset(c20);
//    float3 lensDirtBias : packoffset(c21);
//    float4 tonemapCoeffA : packoffset(c22);
//    float4 tonemapCoeffB : packoffset(c23);
//    float3 luminanceVector : packoffset(c24);
//    float3 vignetteParams : packoffset(c25);
//    float4 vignetteColor : packoffset(c26);
//    float4 chromostereopsisParams : packoffset(c27);
//    float4 distortionScaleOffset : packoffset(c28);
//    float3 maxClampColor : packoffset(c29);
//    float fftBloomSpikeDampingScale : packoffset(c29.w);
//    float4 fftKernelSampleScales : packoffset(c30);
// }

SamplerState mainTextureSampler_s : register(s0);
SamplerState colorGradingTextureSampler_s : register(s1);
SamplerState distortionTextureSampler_s : register(s2);
SamplerState tonemapBloomTextureSampler_s : register(s3);
SamplerState runnersVisionAlphaMaskTextureSampler_s : register(s4);
SamplerState lensDirtTextureSampler_s : register(s5);
Texture2D<float4> mainTexture : register(t0);
Texture3D<float4> colorGradingTexture : register(t1);
Texture2D<float4> distortionTexture : register(t2);
Texture2D<float4> tonemapBloomTexture : register(t3);
Texture2D<float4> runnersVisionAlphaMaskTexture : register(t4);
Texture2D<float4> lensDirtTexture : register(t5);

// 3Dmigoto declarations
#define cmp -

void main(float4 v0: SV_Position0, float4 v1: TEXCOORD0, float2 v2: TEXCOORD1, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2, r3;
   uint4 bitmask, uiDest;
   float4 fDest;

   // lens firt + distortion + vignette, shared in many permutations
   r0.xyz = lensDirtTexture.Sample(lensDirtTextureSampler_s, v2.xy).xyz;
   r0.xyz = log2(r0.xyz);
   r0.xyz = lensDirtExponent.xyz * r0.xyz;
   r0.xyz = exp2(r0.xyz);
   r0.xyz = r0.xyz * lensDirtFactor.xyz + lensDirtBias.xyz;
   r1.xyz = distortionTexture.Sample(distortionTextureSampler_s, v2.xy).xyz;
   r1.xy = r1.xy * distortionScaleOffset.xy + distortionScaleOffset.zw;
   r1.xy = v2.xy + r1.xy;
   r2.xyz = mainTexture.Sample(mainTextureSampler_s, r1.xy).xyz;

   float3 mainTex = r2.xyz;

   r1.xyw = tonemapBloomTexture.Sample(tonemapBloomTextureSampler_s, r1.xy).xyz;
   r3.xyz = r1.xyw + -r2.xyz;
   r2.xyz = r1.zzz * r3.xyz + r2.xyz;
   r1.xyz = bloomScale.xyz * r1.xyw;
   r0.xyz = r1.xyz * r0.xyz + r2.xyz;
   r0.xyz = colorScale.xyz * r0.xyz;
   r1.xy = float2(-0.5, -0.5) + v2.xy;
   r1.xy = vignetteParams.xy * r1.xy;
   r0.w = dot(r1.xy, r1.xy);
   r0.w = saturate(-r0.w * vignetteColor.w + 1);
   r0.w = log2(r0.w);
   r0.w = vignetteParams.z * r0.w;
   r0.w = exp2(r0.w);
   r0.xyz = r0.xyz * r0.www;
   float3 workingColor = r0.rgb;

   {
      // // pre compute coefficients and apply exposure
      // r1.xyz = tonemapCoeffA.xzx / tonemapCoeffA.ywy;
      // r1.xyz =
      //     r1.xyz * float3(-0.199999988, 0.229999989, 0.180000007) + float3(0.569999993, 0.00999999978, 0.0199999996);
      // r0.w = r1.y * r1.x;
      // r1.y = tonemapCoeffB.z * 0.200000003 + r0.w;
      // r1.zw = float2(0.0199999996, 0.300000012) * r1.zz;
      // r1.y = tonemapCoeffB.z * r1.y + r1.z;
      // r2.x = tonemapCoeffB.z * 0.200000003 + r1.x;
      // r2.x = tonemapCoeffB.z * r2.x + r1.w;
      // r1.y = r1.y / r2.x;
      // r1.y = -0.0666666627 + r1.y;
      // r1.y = 1 / r1.y;

      // // per channel tonemap
      // r0.xyz = r1.yyy * r0.xyz;
      // r2.xyz = r0.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r0.www;
      // r2.xyz = r0.xyz * r2.xyz + r1.zzz;
      // r3.xyz = r0.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r1.xxx;
      // r0.xyz = r0.xyz * r3.xyz + r1.www;
      // r0.xyz = r2.xyz / r0.xyz;
      // r0.xyz = float3(-0.0666666627, -0.0666666627, -0.0666666627) + r0.xyz;
      // r0.xyz = r0.xyz * r1.yyy;
      // r0.xyz = r0.xyz / tonemapCoeffB.www;
      // workingColor = r0.rgb;

      workingColor = ApplyTonemapMirrorsEdge(workingColor);
   }

   {
      // srgb encode
      // r0.rgb = workingColor;
      // r1.xyz = log2(abs(r0.xyz));
      // r1.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r1.xyz;
      // r1.xyz = exp2(r1.xyz);
      // r1.xyz =
      //     r1.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
      // r2.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r0.xyz;
      // r0.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r0.xyz);
      // r0.xyz = r0.xyz ? r2.xyz : r1.xyz;

      // sample LUT
      // r0.xyz = r0.xyz * float3(0.96875, 0.96875, 0.96875) + float3(0.015625, 0.015625, 0.015625);
      // r0.xyz = colorGradingTexture.Sample(colorGradingTextureSampler_s, r0.xyz).xyz;
      // workingColor = r0.rgb;

      workingColor = SampleLUT32SRGBInSRGBOut(workingColor, colorGradingTexture, colorGradingTextureSampler_s);
   }

   {
      // r1.xyz = float3(1, 1, 1) + -r0.xyz;
      // r1.xyz = r1.xyz * float3(0.400000006, 0.400000006, 0.400000006) + r0.xyz;
      // r2.xyz = runnersVisionColor.xyz + -r1.xyz;
      // r1.xyz = preBlendAmount * r2.xyz + r1.xyz;
      // r1.xyz = float3(1, 1, 1) + -r1.xyz;
      // r1.xyz = r1.xyz / runnersVisionColor.xyz;
      // r1.xyz = float3(1, 1, 1) + -r1.xyz;
      // r1.xyz = runnersVisionColor.xyz * postAddAmount + r1.xyz;
      // r1.yzw = runnersVisionColor.xyz * float3(0.400000006, 0.400000006, 0.400000006) + r1.xyz;
      // r0.w = runnersVisionAlphaMaskTexture.Sample(runnersVisionAlphaMaskTextureSampler_s, v2.xy).x;
      // r1.x = max(r0.w, r1.y);
      // r1.xyz = r1.xzw + -r0.xyz;
      // o0.xyz = r0.www * r1.xyz + r0.xyz;
      // o0.w = dot(r0.xyz, float3(0.298999995, 0.587000012, 0.114));
      // workingColor = r0.rgb;
      workingColor =
          ApplyRunnersVision(workingColor, runnersVisionAlphaMaskTexture, runnersVisionAlphaMaskTextureSampler_s, v2);
   }

   workingColor = ApplyDisplayMapAndScaleMirrorsEdge(workingColor);

   o0.w = GetLuminance(workingColor); // o0.w = dot(r0.xyz, float3(0.298999995,0.587000012,0.114));
   o0.rgb = workingColor;
   return;
}
