// RCAS sharpening for the SMAA output.
// Runs after the SMAA neighborhood-blend pass, on the gamma LDR color (the same gamma LDR SMAA consumed),
// before the result is copied back into the LDR buffer (core Display Composition does paper-white + scRGB
// downstream). paperWhite=1.0; the sharpness slider is the tuning knob. RCAS_LIMIT bounds the lobe so bright
// pixels don't over-sharpen.

#include "../Includes/RCAS.hlsl"

cbuffer SharpenCB : register(b0)
{
   float4 SharpenParams; // (width, height, sharpness[0..1], unused)
}

Texture2D<float4> tex0 : register(t0);    // SMAA output (gamma LDR)
Texture2D<float2> dummyMV : register(t1); // unused (dynamicSharpening = false)

float4 sharpen_ps(float4 pos : SV_Position) : SV_Target
{
   int2 p = int2(pos.xy);
   int2 maxPixel = int2((int)SharpenParams.x - 1, (int)SharpenParams.y - 1);
   return RCAS(p, int2(0, 0), maxPixel, SharpenParams.z, tex0, dummyMV, 1.0, false, (float4)0, false);
}
