cbuffer ExposureCB : register(b0)
{
	float DeltaTime;
	float Reset;
	float AdaptSpeed;
	float RangeStops;
	uint Width;
	uint Height;
	float Key;
	uint Mode;
	uint DepthValid;
	float DepthNumerator;
	float DepthOffset;
	float DepthPad;
};

cbuffer cb4 : register(b4)
{
	float4 cb4[64];
};

#define AE_MIN_LOG2 -14.0
#define AE_MAX_LOG2 6.0
#define AE_BINS 128
#define AE_ADAPTIVE_TILE_COLUMNS 8
#define AE_ADAPTIVE_TILE_ROWS 6
#define AE_ADAPTIVE_SAMPLE_STRIDE 4
#define AE_ADAPTIVE_GROUP_SAMPLES 16
#define AE_ADAPTIVE_TILE_RECORD_OFFSET 512
#define AE_ADAPTIVE_NEAR_DISTANCE_STOPS 3.0
#define AE_ADAPTIVE_NEAR_DISTANCE_WIDTH 4.0

Texture2D<float4> SceneTex : register(t0);
Texture2D<float> DepthTex : register(t1);
RWByteAddressBuffer Histogram : register(u0);

groupshared uint gs[AE_BINS];
groupshared float4 gsTile[256];

[numthreads(16,16,1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 GroupID : SV_GroupID, uint gidx : SV_GroupIndex)
{
	if (gidx < AE_BINS)
	{
		gs[gidx] = 0;
	}
	GroupMemoryBarrierWithGroupSync();
	if (Mode == 2)
	{
		gsTile[gidx] = 0.0;
		GroupMemoryBarrierWithGroupSync();
		uint2 tile = GroupID.xy;
		uint2 pixelBegin = uint2(tile.x * Width / AE_ADAPTIVE_TILE_COLUMNS, tile.y * Height / AE_ADAPTIVE_TILE_ROWS);
		uint2 pixelEnd = uint2((tile.x + 1) * Width / AE_ADAPTIVE_TILE_COLUMNS, (tile.y + 1) * Height / AE_ADAPTIVE_TILE_ROWS);
		uint2 sampleBegin = uint2(pixelBegin.x <= 2 ? 0 : (pixelBegin.x - 2 + AE_ADAPTIVE_SAMPLE_STRIDE - 1) / AE_ADAPTIVE_SAMPLE_STRIDE,
			pixelBegin.y <= 2 ? 0 : (pixelBegin.y - 2 + AE_ADAPTIVE_SAMPLE_STRIDE - 1) / AE_ADAPTIVE_SAMPLE_STRIDE);
		uint2 sampleEnd = uint2(pixelEnd.x <= 2 ? 0 : (pixelEnd.x - 2 + AE_ADAPTIVE_SAMPLE_STRIDE - 1) / AE_ADAPTIVE_SAMPLE_STRIDE,
			pixelEnd.y <= 2 ? 0 : (pixelEnd.y - 2 + AE_ADAPTIVE_SAMPLE_STRIDE - 1) / AE_ADAPTIVE_SAMPLE_STRIDE);
		uint2 lane = uint2(gidx % 16, gidx / 16);
		uint2 sampleStart = sampleBegin + lane;
		float colorSum = 0.0, colorCount = 0.0, depthSum = 0.0, depthWeight = 0.0;
		for (uint sy = sampleStart.y; sy < sampleEnd.y; sy += AE_ADAPTIVE_GROUP_SAMPLES)
		for (uint sx = sampleStart.x; sx < sampleEnd.x; sx += AE_ADAPTIVE_GROUP_SAMPLES)
		{
			uint2 pix = uint2(sx, sy) * AE_ADAPTIVE_SAMPLE_STRIDE + 2;
			float3 rgb = SceneTex.Load(int3(pix, 0)).rgb;
			float lum = dot(rgb, float3(0.2126, 0.7152, 0.0722));
			if (lum >= 0.0 && lum < 1e30)
			{
				float logLum = log2(max(lum, 1e-6));
				colorSum += logLum; colorCount += 1.0;
				int bin = clamp((int)((logLum - AE_MIN_LOG2) / (AE_MAX_LOG2 - AE_MIN_LOG2) * AE_BINS), 0, AE_BINS - 1);
				InterlockedAdd(gs[bin], 1);
				if (DepthValid != 0)
				{
					float d = DepthTex.Load(int3(pix, 0));
					float dist = DepthNumerator / (d + DepthOffset);
					if (dist > 0.0 && dist < 1e30)
					{
						float nearW = saturate(1.0 - abs(log2(max(dist, 1e-4)) - AE_ADAPTIVE_NEAR_DISTANCE_STOPS) / AE_ADAPTIVE_NEAR_DISTANCE_WIDTH);
						depthSum += logLum * nearW; depthWeight += nearW;
					}
				}
			}
		}
		gsTile[gidx] = float4(colorSum, colorCount, depthSum, depthWeight);
		GroupMemoryBarrierWithGroupSync();
		if (gidx < AE_BINS && gs[gidx] != 0)
		{
			Histogram.InterlockedAdd(gidx * 4, gs[gidx]);
		}
		if (gidx == 0)
		{
			float4 rec = 0.0;
			for (uint i = 0; i < 256; ++i) rec += gsTile[i];
			Histogram.Store4(AE_ADAPTIVE_TILE_RECORD_OFFSET + (tile.y * AE_ADAPTIVE_TILE_COLUMNS + tile.x) * 16, asuint(rec));
		}
		return;
	}
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
