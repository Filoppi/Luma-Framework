// Borderlands 2 / The Pre-Sequel — Luma HDR pyramidal bloom (provides the Bloom * entry points DrawBloom calls,
// auto-registered by core when ENABLE_BLOOM=1). Replaces the game's single-level quarter-res UNORM-clamped bloom
// with a multi-mip fp16 bloom computed from the linear HDR scene (energy-correct: bright HDR sources glow
// proportionally instead of flattening at the 1.0 clamp).
//
// Threshold = soft-knee quadratic (Bloom.hlsl default LUMA_BLOOM_THRESHOLD_FUNCTION). Source is linear scene
// referred to paper white (1.0), so threshold 1.0 blooms above-paper-white highlights; soft knee 0.5 fades the
// transition in (avoids hard popping on cel-shade ink edges). Karis firefly weighting is
// applied separately (DrawKarisAverage) before this, since BL2 has no TAA to hide sparkle.

#include "../Includes/Color.hlsl" // GetLuminance, gamma helpers used by Bloom.hlsl

#define LUMA_BLOOM_THRESHOLD 1.0
#define LUMA_BLOOM_SOFT_KNEE 0.5

#include "../Includes/Bloom.hlsl"
