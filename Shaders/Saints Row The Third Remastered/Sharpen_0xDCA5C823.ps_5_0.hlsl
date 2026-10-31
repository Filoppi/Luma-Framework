#include "Includes/Common.hlsl"

// anamorph_sharpen (display.ini "Sharpen"; the game skips the pass at 0). Vanilla sharpens the final image, GUI included,
// with an overlay blend that breaks above 1. Disabled: its input is passed through unchanged, whatever the pass
// writes to. Luma sharpens the scene alone in compose instead (RCAS Sharpness).

Texture2D<float4> BackBuffer : register(t0);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
   return float4(BackBuffer.Load(int3(pos.xy, 0)).rgb, 1.0);
}
