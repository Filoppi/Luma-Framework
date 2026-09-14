Texture2D<float> depthTexture : register(t0);

void main(
	float4 pos : SV_POSITION0,
	out float oDepth: SV_Depth)
{
	float depth = depthTexture[uint2(pos.xy)];
	oDepth = depth;
}