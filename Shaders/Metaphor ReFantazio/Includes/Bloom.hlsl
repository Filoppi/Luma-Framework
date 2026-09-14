// TODO: find out if used outside bloom

// resolution on PS5 is 2944x1656 blur offsets are tuned to this, but are scaled with resolution
// bloom is rendered at quarter of output resolution
// for the first pass Blur13_0x2054ae6a.ps_5_0 offsets are correct at 4K, but at 1080p the same offsets are used despite the textures being at half resolution
// the subsequent passes for bloom Blur15_0x3635cfde.ps_5_0 are scaled up with resolution
// the subsequent passes for the star burst effect Blur8_0x59f6c52e.ps_5_0 are scaled inverse to the resolution, which makes no sense whatsoever
// all of this completely breaks the visual apperance with other resolutions so we scale them back to the intended look

static const float2 native_bloom_res = float2(736.0f, 414.0f);

float2 GetBloomScale(Texture2D tex)
{
	float2 sourceSize;
	tex.GetDimensions(sourceSize.x, sourceSize.y);
	return (native_bloom_res.y / sourceSize.y);
}

float2 GetStarburstScale(Texture2D tex)
{
	float2 sourceSize;
	tex.GetDimensions(sourceSize.x, sourceSize.y);
	return (sourceSize.y / native_bloom_res.y);
}