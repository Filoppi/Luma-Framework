// Mass Effect (2007) - UE3 DOFAndBloomGather, the QualityBloom=TRUE permutation: sixteen taps, output * 1/64. Body and
// rationale in Includes/DofBloomGather.hlsl; the four-tap twin is DofBloomGather4_0x28F8DB16.
#include "Includes/DofBloomGather.hlsl"

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl). Tap pairs are (vN.xy,
// vN.wz) for N = 5..12 - note the swapped second pair.
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
   float4 sum = GatherTap(v5.xy) + GatherTap(v5.wz);
   sum += GatherTap(v6.xy) + GatherTap(v6.wz);
   sum += GatherTap(v7.xy) + GatherTap(v7.wz);
   sum += GatherTap(v8.xy) + GatherTap(v8.wz);
   sum += GatherTap(v9.xy) + GatherTap(v9.wz);
   sum += GatherTap(v10.xy) + GatherTap(v10.wz);
   sum += GatherTap(v11.xy) + GatherTap(v11.wz);
   sum += GatherTap(v12.xy) + GatherTap(v12.wz);
   o0 = sum * (1.0 / 64.0);
}
