// Borderlands: The Pre-Sequel — tonemap / HDR injection point (dgVoodoo->DX11 hash 0xFCFE623E).
// TPS runs the SAME UE3/Gearbox uber-post tonemap as Borderlands 2, but its native shader
// (tps_tonemap_0xF8997849) inserts a LightShaftTexture at sampler slot 1, which shifts bloom/vignette/LUT/DOF
// DOWN one slot vs BL2 (the DX9 BL2 tonemap_0x54ED86A0 has LUT@s3/DOF@s4; TPS has lightshaft@s1, LUT@s4,
// DOF@s5). dgVoodoo maps DX9 sN 1:1 onto DX11 tN, so the same shift lands in the DX11 register space Luma
// sees. Reusing the BL2 bindings verbatim made the grade sample the Vignette texture as the LUT (grayscale /
// black-and-white) and the LUT as the DOF buffer (broken DoF) — hence the dedicated slot map below.
//
// The grade MATH is identical between the two games, so we set the slot-map + light-shaft macros here and
// pull in the shared BL2 body (one source of truth; matched/replaced by the 0x<HASH> in this filename).
// Injected Luma bloom moves to t8 because TPS's native DOF occupies t5; the Gaussian DoF prefilter (t7)
// stays put — free on TPS. The mod binds Luma bloom at t8 when it detects this tonemap hash.
#define TM_HAS_LIGHTSHAFT 1
#define TM_T_LIGHTSHAFT   t1 // LightShaftTexture (god rays) — TPS-only, inserted at slot 1
#define TM_T_BLOOM        t2 // FilterColor1Texture (screen-blend bloom)
#define TM_T_VIGNETTE     t3 // VignetteTexture
#define TM_T_LUT          t4 // ColorGradingLUT (256x16, 16-slice)
#define TM_T_DOF          t5 // LowResPostProcessBuffer (half-res DOF)
#define TM_T_LUMABLOOM    t8 // injected Luma HDR bloom (t5 is the native DOF on TPS — bind higher to avoid the clash)
#define TM_S_BLOOM        s2
#define TM_S_VIGNETTE     s3
#define TM_S_LUT          s4
#define TM_S_DOF          s5
#include "Tonemap_0xD00AA2A7.ps_5_0.hlsl"
