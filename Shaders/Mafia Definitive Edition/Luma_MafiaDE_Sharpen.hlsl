// RCAS sharpening for Mafia: Definitive Edition.
//
// Drawn immediately after our DLSS / DLAA output has been written into the game's AA render
// target. DLAA is well documented to come out slightly *softer* than a good TAA on static
// images (it trades sharpness for the absence of ghosting), and this game's own sharpening
// pass (0x747C6210) is tuned for the blurrier TAA it normally runs after. This adds a
// properly tuned RCAS pass on top, which is exactly what Luma's Mafia III mod does - its
// README lists "adds AMD RCAS sharpening" as part of why the image improves.
//
// Runs in the HDR buffer (R16G16B16A16_FLOAT, pre tonemap). paperWhite = 1.0 and
// dynamicSharpening = false; RCAS_LIMIT inside the include bounds the lobe so HDR highlights
// do not over-sharpen.

#include "../Includes/RCAS.hlsl"

cbuffer SharpenCB : register(b0)
{
   float4 SharpenParams; // (width, height, sharpness[0..1], unused)
}

Texture2D<float4> tex0 : register(t0);    // SR output (HDR)
Texture2D<float2> dummyMV : register(t1); // unused, dynamicSharpening = false

float4 sharpen_ps(float4 pos : SV_Position) : SV_Target
{
   int2 p = int2(pos.xy);
   int2 maxPixel = int2((int)SharpenParams.x - 1, (int)SharpenParams.y - 1);
   return RCAS(p, int2(0, 0), maxPixel, SharpenParams.z, tex0, dummyMV, 1.0, false, (float4)0, false);
}
