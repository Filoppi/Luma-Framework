// Builds the foliage mask of the VBAO pass (AO_SAO_GTVBAO_0xC86DCBDA) from the stencil of the G-buffer depth stencil.
// The G-buffer shaders write the material class in the high nibble of the stencil: 0xB0 is vegetation (ENV_Grass* / ENV_Leaf* materials, and nothing else).
// One output texel per AO pixel (half of the render resolution), set if any texel of its full res 2x2 block is foliage:
// alpha tested leaves only cover some of the texels of a block.

#define STENCIL_CLASS_FOLIAGE 0xBu // stencil >> 4

Texture2D<uint2> g_stencilTex : register(t0); // X32_TYPELESS_G8X24_UINT view of the depth stencil, the stencil is in .y
RWTexture2D<uint> g_foliageMask : register(u0); // R8_UINT, AO resolution

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
	uint2 maskSize;
	g_foliageMask.GetDimensions(maskSize.x, maskSize.y);
	if (any(tid >= maskSize))
	{
		return;
	}
	uint2 stencilSize;
	g_stencilTex.GetDimensions(stencilSize.x, stencilSize.y);

	uint foliage = 0;
	[unroll]
	for (uint i = 0; i < 4; i++)
	{
		uint2 texel = min(tid * 2 + uint2(i & 1, i >> 1), stencilSize - 1);
		foliage |= (g_stencilTex[texel].y >> 4) == STENCIL_CLASS_FOLIAGE ? 1u : 0u;
	}
	g_foliageMask[tid] = foliage;
}
