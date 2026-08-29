// Trilogy-wide fp16 pyramidal bloom: native bright-pass selection over the shared fp16 Gaussian pyramid. Stage 1
// samples the result through the native bloom slot and keeps the game's tint, luma-gated screen blend, and user
// intensity. Bright pass 0xF8942FF1 supplies live BloomScale and Threshold in cb0.xy; C++ folds scale into the
// effective intensity and the prefilter reads threshold from GameSettings.BloomThreshold. Note the native pass
// weights every tap and only then accumulates, while the shared prefilter blurs first and calls this on the sum.

// DrawBloom binds only b11; main.cpp explicitly preserves live LumaSettings in b13 for the prefilter.
#include "Includes/Common.hlsl"

// Vanilla bloom was bounded to [0,1] by its R16G16B16A16_UNORM target alone; the bright pass clamps nothing.
// Vanilla clips after multiplying by BloomScale and this runs before the composite applies BloomIntensity, so
// dividing by that factor keeps the cap scene independent: 4 * LUMA_BLOOM_SCALE lands on vanilla's 1.0.
static const float kMELE_BloomCap = 4.0;

// Native max-channel soft knee; the tonemap applies BloomTint downstream.
float3 me1_bloom_threshold(float3 color)
{
   // Restores the floor half of that [0,1] bound: negative values would blur in and be subtracted by the
   // composite, reading as a hue shift rather than as darkening. Non-finite ones poison a whole Gaussian kernel.
   color = (any(isnan(color)) || any(isinf(color))) ? 0.0 : max(color, 0.0);

   float w = saturate((max(color.r, max(color.g, color.b)) - LumaSettings.GameSettings.BloomThreshold) * 0.5);
   color *= w;

   // Ceiling half, limited on the max channel so hue is preserved. Not the native per-tap clamp: the shared
   // prefilter has already averaged the kernel, and bounding a tap first would have to live in Shaders/Includes.
   const float mch = max(color.r, max(color.g, color.b));
   const float ceiling = kMELE_BloomCap / max(LumaSettings.GameSettings.BloomIntensity, 1e-3);
   return color * (min(mch, ceiling) / max(mch, 1e-6));
}

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) me1_bloom_threshold(color)
#define LUMA_BLOOM_SCALE                     0.25                  // 4 bilinear taps * 0.0625 = 0.25 of their mean.
#define LUMA_BLOOM_TINT                      float3(1.0, 1.0, 1.0) // Tonemap applies BloomTint downstream.

#include "../Includes/Bloom.hlsl"
