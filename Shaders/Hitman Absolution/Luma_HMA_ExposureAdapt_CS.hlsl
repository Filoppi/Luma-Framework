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
};

cbuffer cb4 : register(b4)
{
	float4 cb4[64];
};

#define AE_MIN_LOG2 -14.0
#define AE_MAX_LOG2 6.0
#define AE_BINS 128
#define LUMA_AE_KEY 0.11
#define AE_LOW_PCT 0.5
#define AE_HIGH_PCT 0.95

RWTexture2D<float> ExposureTex : register(u0);
RWByteAddressBuffer Histogram : register(u1);

groupshared uint gsHist[AE_BINS];

[numthreads(128,1,1)]
void main(uint gidx : SV_GroupIndex)
{
	gsHist[gidx] = Histogram.Load(gidx * 4);
	GroupMemoryBarrierWithGroupSync();

	if (gidx == 0)
	{
		float total = 0.0;
		for (int i = 0; i < AE_BINS; ++i)
		{
			total += (float)gsHist[i];
		}

		float vanilla = cb4[41].z;
		float prev = ExposureTex[uint2(0, 0)];
		bool prevValid = prev > 0.0 && prev < 1e6;
		float outV = 1.0;

		if (total > 0.0)
		{
			// Weighted mean of bin-center log2 luminance over the [LOW, HIGH] percentile band.
			float lo = AE_LOW_PCT * total;
			float hi = AE_HIGH_PCT * total;
			float cum = 0.0;
			float sumW = 0.0;
			float sumL = 0.0;
			float range = AE_MAX_LOG2 - AE_MIN_LOG2;
			for (int b = 0; b < AE_BINS; ++b)
			{
				float c = (float)gsHist[b];
				float bStart = cum;
				float bEnd = cum + c;
				float overlap = max(0.0, min(bEnd, hi) - max(bStart, lo));
				if (overlap > 0.0)
				{
					float centerLog2 = AE_MIN_LOG2 + ((float)b + 0.5) / AE_BINS * range;
					sumW += overlap;
					sumL += overlap * centerLog2;
				}
				cum = bEnd;
			}
			float avgLog2 = sumW > 0.0 ? sumL / sumW : 0.0;

			float target = (Key > 0 ? Key : LUMA_AE_KEY) / exp2(avgLog2);
			bool vanillaValid = vanilla > 0.0 && vanilla < 1e30;
			if (vanillaValid)
			{
				target = clamp(target, vanilla * exp2(-RangeStops), vanilla * exp2(RangeStops));
			}
			target = clamp(target, 0.01, 64.0);

			if (Reset > 0.5 || !prevValid)
			{
				outV = target;
			}
			else
			{
				float speed = (target < prev) ? AdaptSpeed * 2.0 : AdaptSpeed;
				float t = 1.0 - exp(-max(DeltaTime, 0.0) * speed);
				outV = exp2(lerp(log2(prev), log2(target), t));
			}
		}
		else
		{
			if (prevValid)
			{
				outV = prev;
			}
			else if (vanilla > 0.0 && vanilla < 1e30)
			{
				outV = vanilla;
			}
			else
			{
				outV = 1.0;
			}
		}

		ExposureTex[uint2(0, 0)] = outV;

		// Estimate the weighted 99.99th percentile within its log-luminance bin.
		float prevHighlight = ExposureTex[uint2(1, 0)];
		bool highlightValid = prevHighlight > 0.0 && prevHighlight < 1e30;
		float highlight = highlightValid ? prevHighlight : 1.0;
		if (total > 0.0)
		{
			float rank = total * 0.9999;
			float cum = 0.0;
			float percentileLog = AE_MAX_LOG2;
			for (int b = 0; b < AE_BINS; ++b)
			{
				float count = (float)gsHist[b];
				if (count > 0.0 && cum + count >= rank)
				{
					float fraction = saturate((rank - cum) / count);
					percentileLog = AE_MIN_LOG2 + ((float)b + fraction) / AE_BINS * (AE_MAX_LOG2 - AE_MIN_LOG2);
					break;
				}
				cum += count;
			}
			float exposure = Mode != 0 ? outV : ((vanilla > 0.0 && vanilla < 1e30) ? vanilla : 1.0);
			float targetHighlight = max(exp2(percentileLog) * exposure, 1e-6);
			float t = 1.0 - exp(-max(DeltaTime, 0.0) * max(AdaptSpeed, 0.0));
			highlight = (Reset > 0.5 || !highlightValid) ? targetHighlight : exp2(lerp(log2(prevHighlight), log2(targetHighlight), t));
		}
		ExposureTex[uint2(1, 0)] = highlight;
	}

	GroupMemoryBarrierWithGroupSync();
	Histogram.Store(gidx * 4, 0);
}
