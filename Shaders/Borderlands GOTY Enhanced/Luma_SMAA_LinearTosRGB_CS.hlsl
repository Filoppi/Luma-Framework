#include "../Includes/Common.hlsl"

Texture2D tex0 : register(t0);
RWTexture2D<float4> uav0 : register(u0);
RWTexture2D<float4> uav1 : register(u1);

[numthreads(8,8,1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	// tex0 - linear input (scene-referred, scRGB HDR)
	// uav0 - linear copy output (SMAA neighborhood blending reads this)
	// uav1 - sRGB-encoded output (SMAA edge detection reads this)

	float4 color = tex0.Load(int3(dtid.xy, 0));
	uav0[dtid.xy] = color;
	color.rgb = linear_to_sRGB_gamma(color.rgb);
	uav1[dtid.xy] = color;
}
