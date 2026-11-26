// G-buffer clamp, two passes selected by HMA_GBUFFER_CLAMP_ONE.
// Pass A (0): blended with MAX against the target, a zero source clamps negative values up to 0.
// Pass B (1): blended with MIN against the target, a one source clamps values above 1 down to 1.
#ifndef HMA_GBUFFER_CLAMP_ONE
#define HMA_GBUFFER_CLAMP_ONE 0
#endif

struct PS_OUTPUT
{
   float4 rt0 : SV_Target0;
   float4 rt1 : SV_Target1;
   float4 rt2 : SV_Target2;
};

PS_OUTPUT main(float4 pos : SV_Position)
{
   PS_OUTPUT output;
   const float v = HMA_GBUFFER_CLAMP_ONE ? 1.0 : 0.0;
   output.rt0 = float4(v, v, v, v);
   output.rt1 = float4(v, v, v, v);
   output.rt2 = float4(v, v, v, v);
   return output;
}
