cbuffer ExposureCB : register(b0)
{
	float DeltaTime;
	float Reset;
	float AdaptSpeed;
	float RangeStops;
	uint Width;
	uint Height;
	float Key;
	uint Pad1;
};

cbuffer cb4 : register(b4)
{
	float4 cb4[64];
};

#define AE_MIN_LOG2 -14.0
#define AE_MAX_LOG2 6.0
#define AE_BINS 128
#define LUMA_AE_KEY 0.18
#define AE_LOW_PCT 0.5
#define AE_HIGH_PCT 0.95

Texture2D<float4> SceneTex : register(t0);
RWByteAddressBuffer Histogram : register(u0);

groupshared uint gs[AE_BINS];

[numthreads(16,16,1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gidx : SV_GroupIndex)
{
	if (gidx < AE_BINS)
	{
		gs[gidx] = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	uint2 pix = dtid.xy * 4 + 2;
	bool valid = pix.x < Width && pix.y < Height;
	if (valid)
	{
		float3 rgb = SceneTex.Load(int3(pix, 0)).rgb;
		float Y = dot(rgb, float3(0.2126, 0.7152, 0.0722));
		// Also rejects NaN (fails both comparisons) and Infinity.
		if (Y > 1e-6 && Y < 1e30)
		{
			float l2 = log2(Y);
			int bin = clamp((int)((l2 - AE_MIN_LOG2) / (AE_MAX_LOG2 - AE_MIN_LOG2) * AE_BINS), 0, AE_BINS - 1);

			float2 uv = float2(pix) / float2(Width, Height);
			float r = saturate(1.0 - length(uv - 0.5) * 2.0);
			uint w = 1 + (uint)round(3.0 * pow(r, 2.0));
			InterlockedAdd(gs[bin], w);
		}
	}
	GroupMemoryBarrierWithGroupSync();

	if (gidx < AE_BINS)
	{
		uint c = gs[gidx];
		if (c != 0)
		{
			Histogram.InterlockedAdd(gidx * 4, c);
		}
	}
}
