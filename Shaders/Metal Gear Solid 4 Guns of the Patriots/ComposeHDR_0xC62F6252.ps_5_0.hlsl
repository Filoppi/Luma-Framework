#include "../Includes/Common.hlsl"

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

Texture2D<float4> t1 : register(t1); // Scene
Texture2D<float4> t0 : register(t0); // Sprite (e.g. vignette)

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

// Decodes HDR, composes it and then re-encodes HDR.
// Draws a additive+darkening sprite like a dirt screen effect.
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float2 uv = v2.xy;

  float4 r0,r1;
  r0.xyzw = t1.Sample(s1_s, uv).xyzw;
  r0.xyz = r0.xyz / r0.w;

#if ENABLE_LUMA && 0 // Disabled as it'd need testing, usually this would not help at all as source and target have the same res
  // Luma: fix overlays being stretched by doing a mirror+loop around 16:9
#if 1
  float width, height;
  t1.GetDimensions(width, height);
  float sourceAspectRatio = width / height;
#else
  float sourceAspectRatio = 16.0 / 9.0; // Assumed. Theoretically it'd need to be the default aspect ratio this target texture has when playing at 16:9, but we can't know that.
#endif
  float2 outputPixelSize = abs(ddx(uv)) + abs(ddy(uv));
  float targetAspectRatio = outputPixelSize.y / outputPixelSize.x;

  float2 scale = 1.0;

  if (targetAspectRatio >= sourceAspectRatio)
    scale.x = targetAspectRatio / sourceAspectRatio;
  else
    scale.y = sourceAspectRatio / targetAspectRatio;
    
  // Center the UVs before scaling them
  uv = (uv - 0.5) * scale + 0.5;

  uv = MirrorUV(uv);
#endif
  r1.xyzw = t0.Sample(s0_s, uv).xyzw;

  r0.xyz = r1.w * r0.xyz;
  r0.xyz = r0.xyz * 0.25 + r1.xyz;
  r0.w = max(r0.x, r0.y);
  r1.x = max(0.25, r0.z);
  r0.w = max(r1.x, r0.w);
  r0.w = 1 / r0.w;

  // Encode them like all the other direct rendering
  // TODO: skip this encode and then the respective decode in "0xF654B362" just after, it's a waste of operations and quality. Though it's hard to gurantee it.
  o0.xyz = r0.xyz * r0.w;
  o0.w = 0.25 * r0.w;
#if 0 // Disabled saturate as it's not helping
  o0.xyzw = saturate(o0.xyzw);
#endif
}