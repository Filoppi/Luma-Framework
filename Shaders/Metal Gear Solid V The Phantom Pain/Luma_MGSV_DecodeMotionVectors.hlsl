// Motion vectors for SR, in pixels from the current to the previous position (what FSR and DLSS take), without the jitter.
// The game's velocity pass (0xA13321B6) computes the same camera motion, but only stores it in 8 bit (0.5 pixel steps), so camera motion
// is rebuilt here in full precision from depth, the same way the game does it, and the game's object motion is kept where objects drew.
#include "Includes/Common.hlsl"

Texture2D<float> g_depth : register(t0);           // The game's depth (reverse Z), as its velocity pass reads it
Texture2D<float4> g_objectVelocity : register(t1); // The game's object velocity: .x object mask, .zw velocity (see below)
RWTexture2D<float2> g_motionVectors : register(u0);
RWTexture2D<float> g_depthCopy : register(u1);     // The same depth as plain R32_FLOAT for SR (the game's is D32S8)

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
	const float2 renderSize = LumaData.GameData.RenderSize;
	if (any(pixel >= uint2(renderSize)))
	{
		return;
	}

	const float depth = g_depth[pixel];
	g_depthCopy[pixel] = depth;

	// Like the game's velocity pass: this pixel's position without the camera offset (our jitter), its view position from depth,
	// then its previous clip position. The game goes through world space; the CPU combined both matrices instead (in double), because
	// world positions far from the map origin lose float precision and the motion vectors would get noisy close to the camera.
	const float2 ndc = ((float2(pixel) + 0.5) / renderSize) * float2(2.0, -2.0) + float2(-1.0, 1.0);
	const float2 clipXY = ndc - LumaData.GameData.CameraCenterOffset;
	const float4 projectionParam = LumaData.GameData.ProjectionParam;
	const float viewZ = projectionParam.z / (depth - projectionParam.w);
	const float4 viewPos = float4(clipXY * projectionParam.xy * viewZ, viewZ, 1.0);
	const float2 prevClipXY = float2(dot(LumaData.GameData.ViewToPrevClip[0], viewPos), dot(LumaData.GameData.ViewToPrevClip[1], viewPos));
	const float prevClipW = dot(LumaData.GameData.ViewToPrevClip[3], viewPos);

	// NDC to pixels (y flips). Points that were behind the previous camera have no valid previous position (and would give inf or NaN,
	// which would poison SR's history): no motion, SR treats them as disoccluded.
	float2 motionVector = 0.0;
	if (prevClipW > 1e-6)
	{
		const float2 prevNdc = prevClipXY / prevClipW;
		motionVector = (prevNdc - clipXY) * renderSize * float2(0.5, -0.5);
	}

	// Where objects drew, the game stored their own velocity, in the same 8 bit encoding as its camera motion:
	// (current - previous) / 64 pixels, x right and y down, length clamped to 1, as "v * 0.5 + 0.5".
	// Its zero is ±0.25 pixels off (127 or 128), so it's only used where the object moves relative to the camera:
	// where it doesn't match our camera motion in that same encoding (static objects keep the exact camera motion).
	const float4 objectVelocity = g_objectVelocity[pixel];
	if (objectVelocity.x > 0.5)
	{
		float2 cameraVelocity = -motionVector / 64.0;
		const float cameraVelocityLength = length(cameraVelocity);
		if (cameraVelocityLength > 1.0)
		{
			cameraVelocity /= cameraVelocityLength;
		}
		const float2 cameraCode = floor((cameraVelocity * 0.5 + 0.5) * 255.0 + 0.5);
		const float2 objectCode = floor(objectVelocity.zw * 255.0 + 0.5);
		if (any(abs(objectCode - cameraCode) > 1.0))
		{
			motionVector = (0.5 - objectVelocity.zw) * 128.0;
		}
	}

	g_motionVectors[pixel] = motionVector;
}
