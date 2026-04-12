#include "Includes/Common.hlsl"

Texture2D<float4> g_Texture0 : register(t0); // TAA/DLSS output
Texture2D<float4> g_Texture1 : register(t1); // Original TAA/DLSS input (for alpha)
SamplerState g_Texture1Sampler_s : register(s1);

void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
    const int3 uv = int3(v0.xy, 0);
    float2 uv_norm = v0.xy / LumaSettings.SwapchainSize;
    float4 color = g_Texture0.Load(uv);
    float alpha = g_Texture1.SampleLevel(g_Texture1Sampler_s, uv_norm, 0).a;

#if TONEMAP_AFTER_TAA
    // TAA=true path: linearize TAA output for late replay motion blur and tonemap
    color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_MIRROR);
#else
    // TAA=false path: convert linear DLSS output back to gamma
    // (PreSREncode linearized for DLSS; now normalize to gamma for rest of pipeline)
    color.rgb = linear_to_sRGB_gamma(color.rgb, GCT_MIRROR);
#endif

    color.a = alpha;
    o0 = color;
}
