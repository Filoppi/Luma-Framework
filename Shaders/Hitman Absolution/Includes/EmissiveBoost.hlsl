#ifndef SRC_HMA_EMISSIVE_BOOST_HLSL
#define SRC_HMA_EMISSIVE_BOOST_HLSL

// Boosts an emissive sample in place by LumaSettings.GameSettings.EmissiveHDRBoost (cubic in luminance). No-op under TONEMAP_TYPE 0.
// Requires Common.hlsl (LumaSettings, GetLuminance) to be included first.
void ApplyEmissiveHDRBoost(inout float4 emissive)
{
#if TONEMAP_TYPE >= 1
  float eY = GetLuminance(saturate(emissive.rgb));
  emissive.rgb *= 1.0 + LumaSettings.GameSettings.EmissiveHDRBoost * 3.0 * eY * eY * eY;
#endif
}

#endif // SRC_HMA_EMISSIVE_BOOST_HLSL
