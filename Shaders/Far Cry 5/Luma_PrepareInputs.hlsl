Texture2D<float2> g_inputMotionVectors : register(t0);
RWTexture2D<float2> g_outputMotionVectors : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID)
{
	uint width, height, input_width, input_height;
	g_outputMotionVectors.GetDimensions(width, height);
	g_inputMotionVectors.GetDimensions(input_width, input_height);
	if (tid.x >= width || tid.y >= height)
	{
		return;
	}

	// The engine can dilate/downsample motion vectors before TAA. Cover the
	// entire output, including odd dimensions; never leave part of the UAV stale.
	// Nearest sample avoids interpolating the nonlinear encoding across edges.
	uint2 source_pixel = min(uint2((float2(tid) + 0.5f) *
		float2(input_width, input_height) / float2(width, height)),
		uint2(input_width - 1, input_height - 1));
	float2 encoded = g_inputMotionVectors[source_pixel].xy;
	float2 x = encoded - 0.498039216f;

	float2 abs_x = abs(x);
	float2 s = (x >= 0.0f) ? 1.0f : -1.0f;

	// Quartic region: s * (2 * abs_x)^4
	float2 two_x = 2.0f * abs_x;
	float2 two_x_sq = two_x * two_x;
	float2 quartic = s * (two_x_sq * two_x_sq);

	// Linear tails: 4.83004713 * x - s * 1.41502368
	float2 lin = 4.83004713f * x - s * 1.41502368f;

	float2 decoded = (abs_x < 0.334370166f) ? quartic : lin;

	// Far Cry 5 TAA computes UV displacement:
	// displacement_uv = 0.100000001f * decoded
	// previousUV = currentUV + displacement_uv
	//
	// Convert UV displacement to pixel displacement for DLSS:
	float2 uv_disp = 0.100000001f * decoded;
	float2 pixel_motion = uv_disp * float2(width, height);

	g_outputMotionVectors[tid] = pixel_motion;
}
