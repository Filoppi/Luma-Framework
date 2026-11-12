// Trilogy-wide fp16 pyramidal bloom: native bright-pass selection over the shared fp16 Gaussian pyramid. Stage 1
// samples the result through the native bloom slot and keeps the game's tint, scene-gated screen blend, and user
// intensity. Bright pass 0xF8942FF1 supplies live BloomScale and Threshold in cb0.xy; C++ folds scale into the
// effective intensity and the prefilter reads threshold from GameSettings.BloomThreshold. Note the native pass
// weights every tap and only then accumulates, while the shared prefilter blurs first and calls this on the sum.

// DrawBloom binds only b11; main.cpp binds live LumaSettings at b13 before calling it, for the prefilter.
#include "Includes/Common.hlsl"

// Vanilla bloom was bounded to [0,1] by its R16G16B16A16_UNORM target alone; the bright pass clamps nothing.
// Vanilla clips after multiplying by the native BloomScale; the composite applies it later (inside BloomIntensity),
// so dividing by that scale alone keeps the cap scene independent: 4 * LUMA_BLOOM_SCALE lands on vanilla's 1.0. The
// user's Bloom Intensity multiplies after the clip, as on top of vanilla, and so still scales a capped source.
static const float kMELE_BloomCap = 4.0;

// Native max-channel soft knee.
float3 MELE_BloomThreshold(float3 color)
{
   // Restores the floor half of that [0,1] bound: negative values would blur in and be subtracted by the
   // composite, reading as a hue shift rather than as darkening. Non-finite ones poison a whole Gaussian kernel.
   color = MELE_IsFinite(color) ? max(color, 0.0) : 0.0; // A computed sum: the ordered test survives fxc

   float w = saturate((max3(color) - LumaSettings.GameSettings.BloomThreshold) * 0.5);
   color *= w;

   // Ceiling half, limited on the max channel so hue is preserved. It bounds the prefiltered sum (see the header);
   // a native-style per-tap bound would have to live in Shaders/Includes.
   const float mch = max3(color);
   const float ceiling = kMELE_BloomCap / max(LumaSettings.GameSettings.BloomScale, 1e-3);
   return color * (min(mch, ceiling) / max(mch, 1e-6));
}

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) MELE_BloomThreshold(color)
#define LUMA_BLOOM_SCALE                     0.25                  // 4 bilinear taps * 0.0625 = 0.25 of their mean.
#define LUMA_BLOOM_TINT                      float3(1.0, 1.0, 1.0) // Tonemap applies BloomTint downstream.

#include "../Includes/Bloom.hlsl"
