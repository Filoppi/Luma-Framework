// Borderlands 2 / The Pre-Sequel — linear-light copy of the post-tonemap LDR, for SMAA's neighborhood blend.
// Edges are detected on the ENCODED signal (the domain SMAA's thresholds are tuned in), but the blend AVERAGES through
// the hardware bilinear (SMAA.hlsl "We exploit bilinear filtering"), so it needs linear input up front: decoding after
// the sample would average encoded values. The encoding is the tonemap's plain gamma 2.2, NOT sRGB, so no SRGB view can
// stand in; the same shared helper decodes it, keeping DefaultGamma in step on both sides. GCT_MIRROR, as in the
// sibling mods: the tonemap's dither can undershoot black by up to one output step, and the mirror carries that
// negative half through the blend and the re-encode instead of clamping it. Alpha passes through untouched.
#include "../Includes/Color.hlsl"

Texture2D<float4> encoded : register(t0); // post-tonemap LDR snapshot (gamma 2.2)
RWTexture2D<float4> linear_out : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   const float4 c = encoded.Load(int3(id.xy, 0));
   linear_out[id.xy] = float4(gamma_to_linear(c.rgb, GCT_MIRROR), c.a);
}
