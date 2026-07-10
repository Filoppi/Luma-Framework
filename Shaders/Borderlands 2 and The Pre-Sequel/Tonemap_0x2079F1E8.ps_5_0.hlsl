// Borderlands: The Pre-Sequel — tonemap / HDR injection point (dgVoodoo 2.81.3 -> ps_4_0, hash 0x2079F1E8).
// Older dgVoodoo builds emit ps_4_0 -> a different CSO hash for the same DX9 shader. Identical I/O + slot map
// (LightShaft@t1, +1 shift, Luma bloom -> t8) to the 2.87.3 TPS variant (0xFCFE623E), so it reuses the same
// TPS slot macros over the shared grade impl.
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
#include "Luma_BL2TPS_Tonemap.hlsl"

void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0,
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   o0 = RunTonemap(v5, v6);
}
