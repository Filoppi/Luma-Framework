// Advanced Warfare weapon depth-of-field composite (RenoDX 0x85051478).
// Uses Luma's shared S1/H1 grading and HDR luminance reconstruction.
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);

SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[30];
}

#define COMMON_LUT
#include "h1_common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  // Filter RGB and the luminance saved in alpha by Luma's rolloff pass
  // with the same five-tap kernel.
  float4 offsets = cb2[29].zwzw * float4(0.5, -1.5, -1.5, -0.5) + v1.xyxy;
  float4 blur = t4.Sample(s4_s, offsets.xy) * 0.235294119;
  float4 sharp = t4.Sample(s4_s, v1.xy);
  blur += sharp * 0.0588235296;
  blur += t4.Sample(s4_s, offsets.zw) * 0.235294119;
  offsets = cb2[29].zwzw * float4(-0.5, 1.5, 1.5, 0.5) + v1.xyxy;
  blur += t4.Sample(s4_s, offsets.xy) * 0.235294119;
  blur += t4.Sample(s4_s, offsets.zw) * 0.235294119;

  // Preserve the game's depth-dependent blend weights. The alpha in t2
  // is a blend mask, not the luminance carried by t4.
  float depth = t5.SampleLevel(s3_s, v1.xy, 0).x;
  float4 nearColor = t2.Sample(s0_s, v1.xy);
  float blend = max(nearColor.w, saturate(cb2[1].y * depth + cb2[1].w));
  depth -= 0.984375;
  blend = depth > 0 ? saturate(cb2[2].x * depth + cb2[2].z) : blend;
  float4 weights = saturate(blend * cb2[3] + cb2[4]);
  weights.yz = min(1 - weights.xy, weights.yz);
  float3 farColor = t3.Sample(s2_s, v1.xy).xyz;

  float3 color = blur.xyz * weights.y + sharp.xyz * weights.x
               + nearColor.xyz * weights.z + farColor * weights.w;
  // Auxiliary DOF textures do not provide a dedicated luminance channel;
  // derive their contribution from RGB while retaining t4's saved luminance.
  float luminance = blur.w * weights.y + sharp.w * weights.x
                  + GetLuminance(nearColor.xyz, CS_BT709) * weights.z
                  + GetLuminance(farColor, CS_BT709) * weights.w;
  LUT_Color_Internal(color, luminance);
  LUT_Gamma();
  // This variant has no 3D LUT. Its saturation/tint constants start at cb2[8].
  LUT_SaturationAndTint(8);
  LUT_UpgradeAndTonemap();

  o0 = float4(li.r0, 1);
}
