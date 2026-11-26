#ifndef SRC_HMA_BLOOM_HLSL
#define SRC_HMA_BLOOM_HLSL

// Bloom source encode: clamps c in place and returns vanilla's scaled encode (rgb = sqrt(c / maxc), a = 1 / maxc).
float4 HMABloomSourceEncode(inout float4 c)
{
#if TONEMAP_TYPE >= 1
  // Keep vanilla's scaled encode: the bright pass decodes rgb^2 / a back to the full HDR value. Only guard against NaN.
  c.rgb = max(c.rgb, 0.0);
  float s = saturate(1.0 / max(max(c.z, max(c.y, c.x)), 1e-6));
#else
  float s = saturate(1.0 / max(c.z, max(c.y, c.x)));
#endif
  return float4(sqrt(c.rgb * s), s);
}

#endif // SRC_HMA_BLOOM_HLSL
