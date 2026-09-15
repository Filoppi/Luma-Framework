#include "Includes/Common.hlsl"

Texture2D<float4> SceneDepthTex : register(t0);
Texture2D<float4> MotionVectorTex : register(t1);

#if CS
RWTexture2D<float2> OutMotionVectorTex;
#endif

SamplerState s0_s : register(s0);

cbuffer LumaCB1 : register(b1)
{
   float4 LumaCB1[140];
}

#define MOTIONDILATIONRADIUS 1

float2 EncodeMotionVector(float2 motionVec)
{
   return motionVec * (0.499f * 0.5f) + (32767.0f / 65535.0f);
}
float2 DecodeMotionVector(float2 motionSample)
{
   return (motionSample - (32767.0f / 65535.0f)) / (0.499f * 0.5f);
}

float2 ViewUVToNDC(float2 uv)
{
   return float2(2 * uv.x - 1, 1 - 2 * uv.y);
}

float4 SVToNDCPosition(float4 svPos)
{
   float2 viewMin = float2(LumaData.GameData.ViewportRect.xy);
   float4 viewSize = LumaData.GameData.RenderResolution; // Todo make it output to support not full res
   float2 pixelPos = svPos.xy - viewMin.xy;
   float3 NDCPos = float3((pixelPos * viewSize.zw - 0.5f) * float2(2, -2), svPos.z);

   return float4(NDCPos.xyz, 1) * svPos.w;
}

#if CS
[numthreads(8, 8, 1)]
void main(uint2 threadGroupId: SV_GroupID, uint2 globalThreadId: SV_DispatchThreadID, uint2 localThreadId: SV_GroupThreadID,
          uint localIndex: SV_GroupIndex)
#else
void main(float4 pos: SV_Position0, out float2 OutMotionVectorTex: SV_Target0)
#endif
{

    uint4 viewportRect = uint4((uint2)LumaData.GameData.ViewportRect.xy, (uint2)(LumaData.GameData.ViewportRect.xy +
                                                                                 LumaData.GameData.RenderResolution.xy));
    float4 renderRes = LumaData.GameData.RenderResolution;
    float4 viewExtent = LumaData.GameData.RenderResolution;

    float2 jitter =
      LumaData.GameData.JitterOffset.xy * LumaData.GameData.RenderResolution.xy * float2(0.5, -0.5);

#if !CS
   uint2 globalThreadId = (uint2)pos.xy;
#endif

   uint2 coords = min(globalThreadId + viewportRect.xy, viewportRect.zw - 1);
   uint2 outCoords = (uint2)viewportRect.xy + globalThreadId;
   const bool inViewport = all(coords.xy < viewportRect.zw);
   if (!inViewport) return;

   float2 offset = float2(0.0, 0.0);

   float2 nearestSampleUV = (coords + 0.5f) * renderRes.zw; //extent
   float2 uv = (float2(globalThreadId) + 0.5f) * renderRes.zw;

   float2 centerPixel = uv * renderRes.xy + jitter;

   float2 nearestPixel = floor(centerPixel) + 0.5;

   nearestSampleUV = viewExtent.zw * (viewportRect.xy + nearestPixel);

   float3 nearestPos; 
   nearestPos.xy = ViewUVToNDC(uv);
   nearestPos.z = SceneDepthTex.SampleLevel(s0_s, nearestSampleUV, 0).x;
   {
      float4 depth;
      depth.x = SceneDepthTex.SampleLevel(s0_s, nearestSampleUV, 0, int2(-MOTIONDILATIONRADIUS, -MOTIONDILATIONRADIUS)).x;
      depth.y = SceneDepthTex.SampleLevel(s0_s, nearestSampleUV, 0, int2(MOTIONDILATIONRADIUS, -MOTIONDILATIONRADIUS)).x;
      depth.z = SceneDepthTex.SampleLevel(s0_s, nearestSampleUV, 0, int2(-MOTIONDILATIONRADIUS, MOTIONDILATIONRADIUS)).x;
      depth.w = SceneDepthTex.SampleLevel(s0_s, nearestSampleUV, 0, int2(MOTIONDILATIONRADIUS, MOTIONDILATIONRADIUS)).x;

      float2 depthoffset = float2(MOTIONDILATIONRADIUS, MOTIONDILATIONRADIUS);
      float crossOffsetX = float(MOTIONDILATIONRADIUS);

      if (depth.x > depth.y)
      {
         crossOffsetX = -MOTIONDILATIONRADIUS;
      }
      if (depth.z > depth.w)
      {
         depthoffset.x = -MOTIONDILATIONRADIUS;
      }
      float maxCrossXY = max(depth.x, depth.y);
      float maxCrossZW = max(depth.z, depth.w);
      if (maxCrossXY > maxCrossZW)
      {
         depthoffset.y = -MOTIONDILATIONRADIUS;
         depthoffset.x = crossOffsetX;
      }
      float maxCrossDepth = max(maxCrossXY, maxCrossZW);
      if (maxCrossDepth > nearestPos.z)
      {
         offset = depthoffset * renderRes.zw; //extent
         nearestPos.z = maxCrossDepth;
      }
   }

   float4 currentClipPos = float4(nearestPos.xy, nearestPos.z, 1);

   float4x4 currToPrevClip =
      float4x4(LumaCB1[LumaData.GameData.ClipToPrevClipIndex], LumaCB1[LumaData.GameData.ClipToPrevClipIndex + 1],
               LumaCB1[LumaData.GameData.ClipToPrevClipIndex + 2], LumaCB1[LumaData.GameData.ClipToPrevClipIndex + 3]);
   float4 prevClipPos = mul(currentClipPos, currToPrevClip);
   float2 prevScreenPos = prevClipPos.xy / prevClipPos.w;
   float2 motionDelta = nearestPos.xy - prevScreenPos;

   float2 screenSpaceDelta = motionDelta * renderRes.xy;

   float4 motionSample = MotionVectorTex.SampleLevel(s0_s, nearestSampleUV + offset, 0);
   bool hasDynamicMotion = motionSample.x > 0.0;
   if (hasDynamicMotion)
   {
      motionDelta = DecodeMotionVector(motionSample.xy).xy;
   }
   screenSpaceDelta = motionDelta * renderRes.xy;

#if CS
   OutMotionVectorTex[outCoords].xy = -screenSpaceDelta * float2(0.5, -0.5);
#else
   OutMotionVectorTex.xy = -screenSpaceDelta * float2(0.5, -0.5);
#endif
}
