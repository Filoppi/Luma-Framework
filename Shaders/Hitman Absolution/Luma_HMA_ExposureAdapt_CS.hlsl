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
#define AE_ADAPTIVE_TILE_COLUMNS 8
#define AE_ADAPTIVE_TILE_ROWS 6
#define AE_ADAPTIVE_TILE_COUNT 48
#define AE_ADAPTIVE_TILE_RECORD_OFFSET 512
#define AE_ADAPTIVE_CENTER_WEIGHT_FLOOR 0.25
#define AE_ADAPTIVE_CENTER_WEIGHT_STRENGTH 0.75
#define AE_ADAPTIVE_CENTER_WEIGHT_RADIUS 1.4
#define AE_ADAPTIVE_OUTLIER_WEIGHT_STOPS 0.35
#define AE_ADAPTIVE_DEADBAND_STOPS 0.04
#define AE_ADAPTIVE_MAX_STOPS_PER_SECOND 1.0
#define AE_ADAPTIVE_MAX_HIGHLIGHT_STOPS_PER_SECOND 0.75
#define AE_ADAPTIVE_MAX_TIMESTEP 0.05

RWTexture2D<float> ExposureTex : register(u0);
RWByteAddressBuffer Histogram : register(u1);

groupshared uint gsHist[AE_BINS];
groupshared float gsTileEstimates[AE_ADAPTIVE_TILE_COUNT];
groupshared float gsTileSorted[AE_ADAPTIVE_TILE_COUNT];
groupshared float gsTileCenters[AE_ADAPTIVE_TILE_COUNT];
groupshared float gsTileDepthEstimates[AE_ADAPTIVE_TILE_COUNT];
groupshared float gsTileDepthCoverages[AE_ADAPTIVE_TILE_COUNT];

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
		float prevHighlight = ExposureTex[uint2(1, 0)];
		bool prevValid = prev > 0.0 && prev < 1e6;
		bool highlightValid = prevHighlight > 0.0 && prevHighlight < 1e30;
		float cappedDeltaTime = min(max(DeltaTime, 0.0), AE_ADAPTIVE_MAX_TIMESTEP);
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
			if (Mode == 2)
			{
				uint validTileCount = 0;
				for (int tileIndex = 0; tileIndex < AE_ADAPTIVE_TILE_COUNT; ++tileIndex)
				{
					float4 rec = asfloat(Histogram.Load4(AE_ADAPTIVE_TILE_RECORD_OFFSET + tileIndex * 16));
					if (rec.y > 0.0)
					{
						gsTileEstimates[validTileCount] = rec.x / rec.y;
						gsTileSorted[validTileCount] = gsTileEstimates[validTileCount];
						uint tileNumber = (uint)tileIndex;
						uint2 tile = uint2(tileNumber % AE_ADAPTIVE_TILE_COLUMNS, tileNumber / AE_ADAPTIVE_TILE_COLUMNS);
						float2 uv = (float2(tile) + 0.5) / float2(AE_ADAPTIVE_TILE_COLUMNS, AE_ADAPTIVE_TILE_ROWS);
						gsTileCenters[validTileCount] = AE_ADAPTIVE_CENTER_WEIGHT_FLOOR + AE_ADAPTIVE_CENTER_WEIGHT_STRENGTH * saturate(1.0 - length(uv - 0.5) * AE_ADAPTIVE_CENTER_WEIGHT_RADIUS);
						gsTileDepthEstimates[validTileCount] = rec.w > 0.0 ? rec.z / rec.w : gsTileEstimates[validTileCount];
						gsTileDepthCoverages[validTileCount] = rec.w / rec.y;
						validTileCount++;
					}
				}
				// Bounded insertion sort considers valid tiles only.
				for (uint sortIndex = 1; sortIndex < validTileCount; ++sortIndex)
				{
					float value = gsTileSorted[sortIndex];
					uint insertIndex = sortIndex;
					while (insertIndex > 0)
					{
						if (gsTileSorted[insertIndex - 1] <= value)
							break;
						gsTileSorted[insertIndex] = gsTileSorted[insertIndex - 1];
						insertIndex--;
					}
					gsTileSorted[insertIndex] = value;
				}
				float median = validTileCount > 0 ? 0.5 * (gsTileSorted[(validTileCount - 1) >> 1] + gsTileSorted[validTileCount >> 1]) : avgLog2;
				float colorSum = 0.0, centerWeightSum = 0.0;
				float depthSum = 0.0, depthWeightSum = 0.0;
				for (uint tileMix = 0; tileMix < validTileCount; ++tileMix)
				{
					float robustWeight = exp2(-AE_ADAPTIVE_OUTLIER_WEIGHT_STOPS * abs(gsTileEstimates[tileMix] - median));
					float zoneWeight = gsTileCenters[tileMix] * robustWeight;
					colorSum += gsTileEstimates[tileMix] * zoneWeight;
					centerWeightSum += zoneWeight;
					float geometryWeight = zoneWeight * saturate(gsTileDepthCoverages[tileMix]);
					depthSum += gsTileDepthEstimates[tileMix] * geometryWeight;
					depthWeightSum += geometryWeight;
				}
				float colorEstimate = centerWeightSum > 0.0 ? colorSum / centerWeightSum : avgLog2;
				float depthEstimate = depthWeightSum > 0.0 ? depthSum / depthWeightSum : colorEstimate;
				float confidence = centerWeightSum > 0.0 ? saturate(depthWeightSum / centerWeightSum) : 0.0;
				avgLog2 = lerp(colorEstimate, depthEstimate, confidence);
			}

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
			if (Mode == 2 && Reset <= 0.5 && prevValid)
			{
				float requestedStops = log2(target / prev);
				if (abs(requestedStops) <= AE_ADAPTIVE_DEADBAND_STOPS)
				{
					outV = prev;
				}
				else
				{
					float maxStep = AE_ADAPTIVE_MAX_STOPS_PER_SECOND * cappedDeltaTime;
					float appliedStops = clamp(log2(outV / prev), -maxStep, maxStep);
					outV = exp2(log2(prev) + appliedStops);
				}
			}
		}
		else
		{
			if (Mode == 2)
			{
				outV = Reset <= 0.5 && prevValid && highlightValid ? prev : 0.0;
			}
			else if (prevValid)
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
		float highlight;
		if (Mode == 2 && total <= 0.0)
			highlight = Reset <= 0.5 && prevValid && highlightValid ? prevHighlight : 0.0;
		else
			highlight = highlightValid ? prevHighlight : 1.0;
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
			float rawHighlight = max(exp2(percentileLog), 1e-6);
			float targetHighlight = rawHighlight * exposure;
			float highlightDeltaTime = Mode == 2 ? cappedDeltaTime : max(DeltaTime, 0.0);
			float t = 1.0 - exp(-highlightDeltaTime * max(AdaptSpeed, 0.0));
			if (Mode == 2)
			{
				float prevRaw = prevValid ? prevHighlight / prev : rawHighlight;
				float smoothedRaw = (Reset > 0.5 || !highlightValid || !prevValid) ? rawHighlight : exp2(lerp(log2(max(prevRaw, 1e-6)), log2(rawHighlight), t));
				if (Reset <= 0.5 && highlightValid && prevValid)
				{
					float maxHighlightStep = AE_ADAPTIVE_MAX_HIGHLIGHT_STOPS_PER_SECOND * cappedDeltaTime;
					float appliedHighlightStops = clamp(log2(smoothedRaw / max(prevRaw, 1e-6)), -maxHighlightStep, maxHighlightStep);
					smoothedRaw = exp2(log2(max(prevRaw, 1e-6)) + appliedHighlightStops);
				}
				highlight = smoothedRaw * outV;
			}
			else highlight = (Reset > 0.5 || !highlightValid) ? targetHighlight : exp2(lerp(log2(prevHighlight), log2(targetHighlight), t));
		}
		ExposureTex[uint2(1, 0)] = highlight;
	}

	GroupMemoryBarrierWithGroupSync();
	Histogram.Store(gidx * 4, 0);
}
