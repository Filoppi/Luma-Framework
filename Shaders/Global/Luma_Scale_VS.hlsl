// Full-screen triangle that also outputs normalized UVs (0..1), so a pixel shader
// can bilinearly sample a source texture into a different-sized render target.
struct VS_OUT
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
};

VS_OUT main(uint vertexIdx : SV_VertexID)
{
	VS_OUT o;
	float2 texcoord = float2(vertexIdx & 1, vertexIdx >> 1);
	o.pos = float4((texcoord.x - 0.5) * 2, -(texcoord.y - 0.5f) * 2, 0, 1);
	o.uv = texcoord;
	return o;
}
