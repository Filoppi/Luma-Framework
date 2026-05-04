// need more info: motion blur likely requires the most patching, depth, velocity and other info are in render resolution size it also uses stale screen dimensions.

#include "./common.hlsl"

cbuffer cbMotion : register(b0)
{
  float4x4 g_motionMat : packoffset(c0);
  float4 g_unprojectParams : packoffset(c4);
  float4 g_fullscreenDims : packoffset(c5);
  float g_sharpness : packoffset(c6);
  float g_linearDepthThresholdInv : packoffset(c6.y);
  float g_foreVersusBackLinearDepthThresholdInv : packoffset(c6.z);
  float g_temporalLinearDepthThreshold : packoffset(c6.w);
  float g_gamePaused : packoffset(c7);
  float g_velocityScaleStatic : packoffset(c7.y);
  float g_velocityScaleDynamic : packoffset(c7.z);
  float g_camMotionBlend_velocityScaleStatic : packoffset(c7.w);
  float g_camMotionBlend_oneMinusCameraMotionStrength : packoffset(c8);
}

SamplerState linearClampSampler_s : register(s0);
SamplerState pointClampSampler_s : register(s1);
Texture2D<float4> g_halfColorTex : register(t0);
Texture2D<uint> g_mip1VelTex : register(t1);
Texture2D<uint> g_maxTileTex : register(t2);
Texture2D<float4> g_velMaskTex : register(t3);

// Ensure X coordinate has bit 0 set (odd value) for texture load
uint adjustLoadX(uint baseX) {
    return (baseX & 0xFFFFFFFEu) | 1u;
}

// Load and decode velocity data from two texture samples
// loadCoord.xy = (baseX, y) where baseX is already shifted
float3 loadVelocity(Texture2D<uint> tex, uint2 loadCoord) {
    uint packed0 = tex.Load(uint3(loadCoord.x, loadCoord.y, 0)).x;
    uint packed1 = tex.Load(uint3(adjustLoadX(loadCoord.x), loadCoord.y, 0)).x;
    
    return float3(
        f16tof32(packed0 >> 16),       // velX
        f16tof32(packed0 & 0xFFFFu),   // velY
        f16tof32(packed1 & 0xFFFFu)    // depth
    );
}

// Load and decode max-tile data: returns (tileX, tileY, tileZ)
// packed0: high16=tileX, low16=tileY  |  packed1: low16=tileZ
float3 loadMaxTile(Texture2D<uint> tex, uint baseX, uint tileY) {
    uint packed0 = tex.Load(uint3(baseX,              tileY, 0)).x;
    uint packed1 = tex.Load(uint3(adjustLoadX(baseX),  tileY, 0)).x;
    return float3(
        f16tof32(packed0 >> 16),       // tileX
        f16tof32(packed0 & 0xFFFFu),   // tileY
        f16tof32(packed1 & 0xFFFFu)    // tileZ
    );
}


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18;
  uint4 bitmask, uiDest;
  float4 fDest;
  bool aboveThreshold;
  float2 luma_fullscreen_dims = g_fullscreenDims.xy;
  float2 luma_scale_from_dims = float2(1.0, 1.0);

  g_halfColorTex.GetDimensions(0, fDest.x, fDest.y, fDest.z);
  luma_scale_from_dims = float2(fDest.x, fDest.y) / max(float2(1.0, 1.0), g_fullscreenDims.xy);

  if (LumaData.GameData.isUpscaling != 0)
  {
    const float luma_rs = max(1.0e-6, LumaData.GameData.resolutionScale);
    const float luma_scale_from_cb = max(luma_rs, 1.0 / luma_rs);
    const bool luma_dims_scale_valid =
      all(luma_scale_from_dims > float2(0.25, 0.25)) &&
      all(luma_scale_from_dims < float2(4.0, 4.0));

    luma_fullscreen_dims = g_fullscreenDims.xy *
      (luma_dims_scale_valid ? luma_scale_from_dims : luma_scale_from_cb.xx);
  }

  r0.xyz = g_halfColorTex.SampleLevel(linearClampSampler_s, v1.xy, 0).xyz;
  r1.x = g_velMaskTex.SampleLevel(linearClampSampler_s, v1.xy, 0).x;
  float velMaskValue = r1.x;
  if (velMaskValue == 0.0f) {
    r1.yzw = r0.xyz;
  }
  r2.x = max(r0.x, r0.y);
  r2.x = max(r2.x, r0.z);
  r2.y = ddx_fine(r2.x);
  r2.x = ddy_fine(r2.x);
  float h, w, d;
  g_maxTileTex.GetDimensions(0, w, h, d);
  r3.xz = float2(w, h);
  r3.y = v1.x * r3.x;
  r4.y = 0.5;
  r4.z = v1.y;
  r2.zw = float2(0.5,0.5) / r3.xz;
  r2.zw = r4.yz * r3.yz + -r2.zw;
  r3.yz = (uint2)r2.zw;
  uint iMaxTileX = r3.y;
  uint iMaxTileY = r3.z;
  uint iMaxTileLoadX = iMaxTileX << 1;
  r3.xyz = loadMaxTile(g_maxTileTex, iMaxTileLoadX, iMaxTileY);
  g_mip1VelTex.GetDimensions(0, w, h, d);
  r2.zw = float2(w, h);
  r4.xy = float2(0.5,1) * r2.zw;
  r2.zw = float2(0.5,0.5) / r2.zw;
  r4.zw = v1.xy * r4.xy + -r2.zw;
  uint2 loadCoord0 = uint2((uint)r4.z << 1, (uint)r4.w);
  float3 vel0 = loadVelocity(g_mip1VelTex, loadCoord0);
  r3.w = dot(vel0.xy, vel0.xy); // sq-magnitude of 2-D velocity
  // r6 = (depth-tileZ, velX-tileX, velY-tileY, depth-tileZ)  [asm: add r6, -r3.zxyz, r5.zxyz]
  r6.xyzw = float4(vel0.z, vel0.x, vel0.y, vel0.z) - float4(r3.z, r3.x, r3.y, r3.z);
  r4.z = saturate(-r6.x * g_foreVersusBackLinearDepthThresholdInv + 1);
  r4.w = (r4.z == 0.000000) ? 1.0 : 0.0;
  r5.w = dot(r3.xy, r3.xy);
  r5.w = (r5.w < r3.w) ? 1.0 : 0.0;
  r4.w = (float)((uint)r4.w & (uint)r5.w);
  r4.z = r4.w ? 1.0 : r4.z;
  r7.xy = v1.xy + r3.xy;
  r7.xy = r7.xy * r4.xy + -r2.zw;
  r7.yz = (uint2)r7.xy;
  bitmask.x = ((~(-1 << 31)) << 1) & 0xffffffff;  r7.x = (((uint)r7.y << 1) & bitmask.x) | ((uint)1 & ~bitmask.x);
  r7.w = 0;
  r4.w = g_mip1VelTex.Load(r7.xzw).x;
  r4.w = asuint(r4.w) & 0x0000ffff;
  r4.w = f16tof32(r4.w);
  r4.w = r3.z / r4.w;
  r4.w = 1 + -r4.w;
  r4.w = 10 * abs(r4.w);
  r4.w = min(1, r4.w);
  r5.w = 1 + -r4.z;
  r4.z = r4.w * r5.w + r4.z;
  r3.xyz = r4.zzz * r6.yzw + r3.xyz;
  r4.zw = luma_fullscreen_dims * r3.xy;
  r4.z = dot(r4.zw, r4.zw);
  r4.z = sqrt(r4.z);
  r4.w = (45.0 < r4.z) ? 1.0 : 0.0;
  r4.z = 45 / r4.z;
  r6.xy = r4.zz * r3.xy;
  r3.xy = r4.ww ? r6.xy : r3.xy;
  r6.xyzw = r3.xyxy * float4(-0.0800000057,-0.0800000057,0.0400000028,0.0400000028) + v1.xyxy;
  r7.xyz = g_halfColorTex.SampleLevel(pointClampSampler_s, r6.xy, 0).xyz;
  r4.z = max(r7.x, r7.y);
  r4.z = max(r4.z, r7.z);
  r4.w = ddx_fine(r4.z);
  r4.z = ddy_fine(r4.z);
  r8.xyzw = r3.xyxy * float4(0.0400000028,0.0400000028,0.0400000028,0.0400000028) + r6.xyzw;
  r9.xyz = g_halfColorTex.SampleLevel(pointClampSampler_s, r8.xy, 0).xyz;
  r5.w = max(r9.x, r9.y);
  r5.w = max(r5.w, r9.z);
  r7.w = ddx_fine(r5.w);
  r5.w = ddy_fine(r5.w);
  r10.xyz = g_halfColorTex.SampleLevel(pointClampSampler_s, r6.zw, 0).xyz;
  r9.w = max(r10.x, r10.y);
  r9.w = max(r9.w, r10.z);
  r10.w = ddx_fine(r9.w);
  r9.w = ddy_fine(r9.w);
  r11.xyz = g_halfColorTex.SampleLevel(pointClampSampler_s, r8.zw, 0).xyz;
  r11.w = max(r11.x, r11.y);
  r11.w = max(r11.w, r11.z);
  r12.x = ddx_fine(r11.w);
  r11.w = ddy_fine(r11.w);
  if (velMaskValue == 0.0f) {
    // r1.x = cmp(100 < abs(r2.y));
    // r2.x = cmp(100 < abs(r2.x));
    // r1.x = (int)r1.x | (int)r2.x;
    aboveThreshold = any((abs(r2.xy) >= 100));
    if (!aboveThreshold) {
      r13.x = rsqrt(r3.w);
      r13.yz = r13.xx * vel0.xy; // normalise (velX, velY) — vel0 holds asm r5.xyz
      r1.x = dot(r3.xy, r3.xy);
      r2.x = (r1.x >= 9.99999997e-07) ? 1.0 : 0.0;
      r14.x = rsqrt(r1.x);
      r14.yz = r14.xx * r3.xy;
      r12.yzw = r2.xxx ? r14.xyz : 0;
      r15.xyz = float3(-0.584962606,-1.58496261,-1.58496237) * g_sharpness;
      r15.xyz = exp2(r15.xyz);
      r15.xyz = float3(1,1,1) + -r15.xyz;
      r2.xy = r6.xy * r4.xy + -r2.zw;
      uint2 loadCoord2 = uint2((uint)r2.x << 1, (uint)r2.y);
      float3 vel2 = loadVelocity(g_mip1VelTex, loadCoord2);
      r1.x = r3.z + -vel2.z; // vel2.z = depth
      r1.x = saturate(-r1.x * g_linearDepthThresholdInv + 1);
      r2.x = dot(r3.xy, r3.xy);
      r2.x = sqrt(r2.x);
      r2.y = max(0.00100000005, r2.x);
      r3.xy = r3.xy / r2.yy;
      r2.y = dot(r3.xy, r12.zw);
      r2.y = min(1, abs(r2.y));
      r2.x = r2.x * r12.y;
      r2.x = min(1, r2.x);
      r2.y = -1 + r2.y;
      r2.x = r2.x * r2.y + 1;
      r1.x = r2.x * r1.x;
      r1.x = r1.x * r15.x;
      // r2.xy = cmp(float2(100,100) < abs(r4.wz));
      // r2.x = (int)r2.y | (int)r2.x;
      aboveThreshold = any((abs(r4.wz) >= float2(100, 100)));
      r16.w = aboveThreshold ? 0 : r1.x;
      r16.xyz = r16.www * r7.xyz;
      r0.w = 1;
      r16.xyzw = r0.xyzw + r16.xyzw;
      r2.xy = r8.xy * r4.xy + -r2.zw;
      uint2 loadCoord3 = uint2((uint)r2.x << 1, (uint)r2.y);
      float3 vel3 = loadVelocity(g_mip1VelTex, loadCoord3);
      r0.w = r3.z + -vel3.z; // vel3.z = depth
      r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
      r1.x = dot(r2.xy, r2.xy);
      r1.x = sqrt(r1.x);
      r3.x = max(0.00100000005, r1.x);
      r2.xy = r2.xy / r3.xx;
      r2.x = dot(r2.xy, r12.zw);
      r2.x = min(1, abs(r2.x));
      r1.x = r1.x * r12.y;
      r1.x = min(1, r1.x);
      r2.x = -1 + r2.x;
      r1.x = r1.x * r2.x + 1;
      r0.w = r1.x * r0.w;
      r0.w = r0.w * r15.y;
      // r1.x = cmp(100 < abs(r7.w));
      // r2.x = cmp(100 < abs(r5.w));
      // r1.x = (int)r1.x | (int)r2.x;
      aboveThreshold = any((abs(float2(r7.w, r5.w)) >= float2(100, 100)));
      r7.w = aboveThreshold ? 0 : r0.w;
      r7.xyz = r9.xyz * r7.www;
      r7.xyzw = r16.xyzw + r7.xyzw;
      r2.xy = r6.zw * r4.xy + -r2.zw;
      uint2 loadCoord4 = uint2((uint)r2.x << 1, (uint)r2.y);
      float3 vel4 = loadVelocity(g_mip1VelTex, loadCoord4);
      r0.w = r3.z + -vel4.z; // vel4.z = depth
      r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
      r1.x = dot(r2.xy, r2.xy);
      r1.x = sqrt(r1.x);
      r3.x = max(0.00100000005, r1.x);
      r2.xy = r2.xy / r3.xx;
      r2.x = dot(r2.xy, r12.zw);
      r2.x = min(1, abs(r2.x));
      r1.x = r1.x * r12.y;
      r1.x = min(1, r1.x);
      r2.x = -1 + r2.x;
      r1.x = r1.x * r2.x + 1;
      r0.w = r1.x * r0.w;
      r0.w = r0.w * r15.z;
      // r1.x = cmp(100 < abs(r10.w));
      // r2.x = cmp(100 < abs(r9.w));
      // r1.x = (int)r1.x | (int)r2.x;
      aboveThreshold = any((abs(float2(r10.w, r9.w)) >= float2(100, 100)));
      r6.w = aboveThreshold ? 0 : r0.w;
      r6.xyz = r10.xyz * r6.www;
      r6.xyzw = r7.xyzw + r6.xyzw;
      r2.xy = r8.zw * r4.xy + -r2.zw;
      uint2 loadCoord5 = uint2((uint)r2.x << 1, (uint)r2.y);
      float3 vel5 = loadVelocity(g_mip1VelTex, loadCoord5);
      r0.w = r3.z + -vel5.z; // vel5.z = depth
      r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
      r1.x = dot(r2.xy, r2.xy);
      r1.x = sqrt(r1.x);
      r3.x = max(0.00100000005, r1.x);
      r2.xy = r2.xy / r3.xx;
      r2.x = dot(r2.xy, r12.zw);
      r2.x = min(1, abs(r2.x));
      r1.x = r1.x * r12.y;
      r1.x = min(1, r1.x);
      r2.x = -1 + r2.x;
      r1.x = r1.x * r2.x + 1;
      r0.w = r1.x * r0.w;
      r0.w = r0.w * r15.x;
      // r1.x = cmp(100 < abs(r12.x));
      // r2.x = cmp(100 < abs(r11.w));
      // r1.x = (int)r1.x | (int)r2.x;
      aboveThreshold = any((abs(float2(r12.x, r11.w)) >= float2(100, 100)));
      r7.w = aboveThreshold ? 0 : r0.w;
      r7.xyz = r11.xyz * r7.www;
      r6.xyzw = r7.xyzw + r6.xyzw;
      r0.w = dot(r13.yz, r14.yz);
      r0.w = (abs(r0.w) < 0.899999976) ? 1.0 : 0.0;
      if (r0.w != 0) {
        r0.w = (r3.w >= 9.99999997e-07) ? 1.0 : 0.0;
        r3.xyz = r0.www ? r13.xyz : 0;
        // asm: mad r7, r5.xyxy (=vel0.xy), l(-0.08,-0.08,0.04,0.04), v1.xyxy
        r7.xyzw = vel0.xyxy * float4(-0.0800000057,-0.0800000057,0.0400000028,0.0400000028) + v1.xyxy;
        r2.xy = r7.xy * r4.xy + -r2.zw;
        uint2 loadCoord6 = uint2((uint)r2.x << 1, (uint)r2.y);
        float3 vel6 = loadVelocity(g_mip1VelTex, loadCoord6);
        r0.w = vel0.z + -vel6.z; // centre depth vs vel6 depth
        r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
        r1.x = dot(r2.xy, r2.xy);
        r1.x = sqrt(r1.x);
        r3.w = max(0.00100000005, r1.x);
        r2.xy = r2.xy / r3.ww;
        r2.x = dot(r2.xy, r3.yz);
        r2.x = min(1, abs(r2.x));
        r1.x = r1.x * r3.x;
        r1.x = min(1, r1.x);
        r2.x = -1 + r2.x;
        r1.x = r1.x * r2.x + 1;
        r0.w = r1.x * r0.w;
        r8.w = r0.w * r15.x;
        r9.xyz = g_halfColorTex.SampleLevel(pointClampSampler_s, r7.xy, 0).xyz;
        r8.xyz = r9.xyz * r8.www;
        r8.xyzw = r8.xyzw + r6.xyzw;
        r9.xyzw = vel0.xyxy * float4(0.0400000028,0.0400000028,0.0400000028,0.0400000028) + r7.xyzw;
        r2.xy = r9.xy * r4.xy + -r2.zw; // UV -> texel for vel7
        uint2 loadCoord7 = uint2((uint)r2.x << 1, (uint)r2.y);
        float3 vel7 = loadVelocity(g_mip1VelTex, loadCoord7);
        r0.w = vel0.z + -vel7.z; // centre depth vs vel7 depth
        r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
        r1.x = dot(r2.xy, r2.xy);
        r1.x = sqrt(r1.x);
        r3.w = max(0.00100000005, r1.x);
        r2.xy = r2.xy / r3.ww;
        r2.x = dot(r2.xy, r3.yz);
        r2.x = min(1, abs(r2.x));
        r1.x = r1.x * r3.x;
        r1.x = min(1, r1.x);
        r2.x = -1 + r2.x;
        r1.x = r1.x * r2.x + 1;
        r0.w = r1.x * r0.w;
        r10.w = r0.w * r15.y;
        r5.xyw = g_halfColorTex.SampleLevel(pointClampSampler_s, r9.xy, 0).xyz;
        r10.xyz = r5.xyw * r10.www;
        r8.xyzw = r10.xyzw + r8.xyzw;
        r2.xy = r7.zw * r4.xy + -r2.zw; // UV -> texel for vel8
        uint2 loadCoord8 = uint2((uint)r2.x << 1, (uint)r2.y);
        float3 vel8 = loadVelocity(g_mip1VelTex, loadCoord8);
        r0.w = vel0.z + -vel8.z; // centre depth vs vel8 depth
        r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
        r1.x = dot(r2.xy, r2.xy);
        r1.x = sqrt(r1.x);
        r3.w = max(0.00100000005, r1.x);
        r2.xy = r2.xy / r3.ww;
        r2.x = dot(r2.xy, r3.yz);
        r2.x = min(1, abs(r2.x));
        r1.x = r1.x * r3.x;
        r1.x = min(1, r1.x);
        r2.x = -1 + r2.x;
        r1.x = r1.x * r2.x + 1;
        r0.w = r1.x * r0.w;
        r10.w = r0.w * r15.z;
        r5.xyw = g_halfColorTex.SampleLevel(pointClampSampler_s, r7.zw, 0).xyz;
        r10.xyz = r5.xyw * r10.www;
        r7.xyzw = r10.xyzw + r8.xyzw;
        r2.xy = r9.zw * r4.xy + -r2.zw; // UV -> texel for vel9
        uint2 loadCoord9 = uint2((uint)r2.x << 1, (uint)r2.y);
        float3 vel9 = loadVelocity(g_mip1VelTex, loadCoord9);
        r0.w = vel0.z + -vel9.z; // centre depth vs vel9 depth
        r0.w = saturate(-r0.w * g_linearDepthThresholdInv + 1);
        r1.x = dot(r2.xy, r2.xy);
        r1.x = sqrt(r1.x);
        r2.z = max(0.00100000005, r1.x);
        r2.xy = r2.xy / r2.zz;
        r2.x = dot(r2.xy, r3.yz);
        r2.x = min(1, abs(r2.x));
        r1.x = r1.x * r3.x;
        r1.x = min(1, r1.x);
        r2.x = -1 + r2.x;
        r1.x = r1.x * r2.x + 1;
        r0.w = r1.x * r0.w;
        r2.w = r0.w * r15.x;
        r3.xyz = g_halfColorTex.SampleLevel(pointClampSampler_s, r9.zw, 0).xyz;
        r2.xyz = r3.xyz * r2.www;
        r6.xyzw = r7.xyzw + r2.xyzw;
      }
      r1.yzw = r6.xyz / r6.www;
    } else {
      r1.yzw = r0.xyz;
    }
  }
  o0.xyz = r1.yzw;
  o0.w = 0;
  return;
}