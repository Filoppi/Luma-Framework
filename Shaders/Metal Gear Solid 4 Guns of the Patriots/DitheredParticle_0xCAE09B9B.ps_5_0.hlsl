#include "../Includes/Common.hlsl"

// Stipple ("screen door") pattern LUT. Each texel is one 32 bit column of the pattern and the
// texture is 32 texels wide, so a single row of it is a full 32x32 on/off mask.
// The row picks the density (how much of the particle survives) and is constant per draw.
Texture2D<uint> StipplePatterns : register(t2);
// Opacity ramp looked up by the view angle (the game only reads the green channel)
Texture2D<float4> FresnelRamp : register(t1);
// Tiled in screen space, not with the particle's own UVs
Texture2D<float4> ScreenTexture : register(t0);

SamplerState FresnelRampSampler : register(s1);
SamplerState ScreenTextureSampler : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}
// cb0[0].rgb  color tint
// cb0[0].w    global opacity
// cb0[1].x    opacity curve exponent
// cb0[1].y    screen space texture size (the UV scale is this over 1024, so 1024 means 1 texel per pixel)
// cb0[8].xyz  camera world position
// cb0[9].x    stipple enabled when greater than 0
// cb0[9].y    stipple pattern row (density)

#if 1 // Workaround: looks better, the original resolution makes too chunky dither pixels, this is a good compromise
static const float NativeResolutionHeight = 1024.0;
#else
// The vertical resolution MGS4 rendered at on PS3 (1024x768, scaled out to 720p on output).
// The stipple grid was authored against this, so it's the reference we scale away from.
static const float NativeResolutionHeight = 768.0;
#endif

void main(
  float4 position    : SV_POSITION0,
  float4 vertexColor : COLOR0,
  float3 normal      : TEXCOORD6,
  float3 worldPos    : TEXCOORD7,
  out float4 output  : SV_TARGET0)
{
  if (cb0[9].x > 0.0)
  {
    uint2 pixelPos = (uint2)floor(position.xy);
    uint patternColumn = StipplePatterns.Load(int3(pixelPos.x & 31, (int)cb0[9].y, 0)).x;
    // D3D takes shift counts mod 32, hence the pattern wrapping vertically every 32 cells. Made explicit here.
    bool keep = (patternColumn & (1u << (pixelPos.y & 31))) != 0;
    if (!keep)
      discard;
  }

  float3 viewDirection = normalize(worldPos - cb0[8].xyz);
  float fresnel = 1.0 - abs(dot(normalize(normal), viewDirection));

  // Remapped into 0.05-0.99 to stay off the clamped edges of the ramp texture
  float alpha = FresnelRamp.Sample(FresnelRampSampler, float2(fresnel * 0.94 + 0.05, 0.5)).y;
  alpha = pow(alpha, cb0[1].x);
  alpha *= cb0[0].w * vertexColor.a;

  // Screen door transparency: a 32x32 bitmask anchored to screen pixels decides which pixels survive.
  // This is a hard 1 bit kill, there's no blending between the levels.
  float2 screenUV = position.xy * (cb0[1].y / 1024.0);
#if 1 // Luma: keep the stipple grid at the apparent size it had natively, instead of one cell per pixel.
  float ditherScale = min(NativeResolutionHeight / LumaSettings.SwapchainSize.y, 1.0);
#else // Vanilla
  float ditherScale = 1.0;
#endif
  screenUV *= ditherScale;
  float4 screenColor = ScreenTexture.Sample(ScreenTextureSampler, screenUV);

  output.rgb = screenColor.rgb * cb0[0].xyz;
  output.a = screenColor.a * alpha;

#if 0 // Luma: clamp to UNORM. Disabled as it doesn't seem to do anything here.
  output = saturate(output);
#endif

  // TODO1: clip to 1? Probably does nothing
}
