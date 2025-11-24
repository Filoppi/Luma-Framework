Texture2D tex : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target
{
	return float4(tex.Load(int3(pos.xy, 0)).xxx, 1.0);
}