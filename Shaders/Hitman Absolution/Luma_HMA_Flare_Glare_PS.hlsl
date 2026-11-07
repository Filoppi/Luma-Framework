// Adapted from Pretty Post Process Glare.usf (GlarePS; procedural line mask replaces T_GlareLine). See Includes/HMAFlare.hlsl for license.
#include "Includes/HMAFlare.hlsl"

float4 main(float4 pos : SV_Position, float2 quadUV : TEXCOORD0, float3 color : TEXCOORD1) : SV_Target0
{
  // u runs along the blade, v across it.
  float along = saturate(1.0 - abs(2.0 * quadUV.x - 1.0));
  along = along * along;
  float across = saturate(1.0 - abs(2.0 * quadUV.y - 1.0));
  across = smoothstep(0.0, 1.0, across);

  float mask = along * across;
  return float4(mask * color, 1.0);
}
