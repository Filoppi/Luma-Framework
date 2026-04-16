Texture2D<float4> sourceTexture : register(t0);

struct SPixelOutput
{
    float4 color : SV_Target0;
    float  depth : SV_Depth;
}; 

SPixelOutput main(
	float4 pos : SV_Position
	)
{
    SPixelOutput output = (SPixelOutput) 0;
    
    output.color = 0.0;  
	
	float depth = sourceTexture.Load(int3(pos.xy, 0)).x;
	output.depth = depth;
	
	return output;
}