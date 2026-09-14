cbuffer GFD_VSCONST_OUTLINE_PREV_DATA : register(b5)
{
    float4x4 mtxLocalToWorldPrev;
    float4x4 mtxViewProjPrev;
	float3 eyePositionPrev;
	bool skinned_mesh;
}

cbuffer GFD_VSCONST_SKIN_CACHE : register(b9)
{
	uint offset : packoffset(c0);
	uint stride : packoffset(c0.y);
}

ByteAddressBuffer CachedSkinVertices : register(t1); 

// for calculating normalOffset: POSITION0 is RGB32F, TEXCOORD0 is RG16F, COLOR0 is RBGA8
void GetPreviousVertexParameters(uint vertexID, uint normalOffset, inout float4 position, inout float3 normal)
{
	if(skinned_mesh)
	{
		position.xyz = asfloat(CachedSkinVertices.Load3(offset + vertexID * stride)).xyz;
		normal = asfloat(CachedSkinVertices.Load3(offset + vertexID * stride + normalOffset)).xyz;
	}
}