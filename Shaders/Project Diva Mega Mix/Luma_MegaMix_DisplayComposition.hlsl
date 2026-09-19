// Forces DisplayComposition to be inert while letting normal Brightness sliders be available.
Texture2D<float4> t0 : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target0
{
	return t0.Load((int3)pos.xyz);
}