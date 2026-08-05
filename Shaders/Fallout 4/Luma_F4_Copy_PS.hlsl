Texture2D tex0 : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target
{
	return tex0.Load(int3(pos.xy, 0));
}