#include "./common1.hlsl"

cbuffer CBMegaMixColorAux : register(b11)
{
  float2 src_size;
  float2 inv_src_size;
  float2 axis;
  // float sigma;
}
#define SUGMA ((CUSTOM_ESM_COLOR_SIGMA + 1) / 5.0) * 0.5

SamplerState smp : register(s0);
Texture2D tex : register(t0);

/////////////////////////////////////////////////////////////////////////////////////////////////

void MainVS(uint vid: SV_VertexID, out float4 pos: SV_Position, out float2 texcoord: TEXCOORD)
{
  texcoord = float2((vid << 1) & 2, vid & 2);
  pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

float get_gaussian_weight(float x, float s)
{
  return exp(-x * x * rcp(2.0 * s * s));
}

float2 downsample_guassian(float4 pos, float2 texcoord, bool isSwizzleRW, float sigmaBoost = 0)
{
  // texcoord
  const float f = dot(frac(texcoord * src_size - 0.5), axis);
  const float2 tc = texcoord - f * inv_src_size * axis;
  // return tc.xy; // debug
  
  // accumulators
  float2 csum = 0.0;
  float wsum = 0.0;
  float sigma1 = SUGMA + sigmaBoost;

  // loop
  const float radius = ceil(sigma1 * 3.0);
  for (float i = 1.0 - radius; i <= radius; i++)
  {
    float4 cRaw = tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0);

    float2 c;
    if (isSwizzleRW) c = cRaw.xw; // discard monochrome shadows green & blue
    else c = cRaw.xy;

    const float w = get_gaussian_weight(i - f, sigma1);
    csum += c * w;
    wsum += w;
  }
  
  // normalize
  csum *= rcp(wsum);
  
  return csum;
}

float2 upsample_4taps(float4 pos, float2 texcoord) : SV_Target
{
  // transform the coordinate from [0,extent] to [-0.5, extent-0.5]
  float2 coord_grid = texcoord * src_size - 0.5;
  float2 index = floor(coord_grid);
  float2 fraction = coord_grid - index;
  float2 one_frac = 1.0 - fraction;
  float2 one_frac2 = one_frac * one_frac;
  float2 fraction2 = fraction * fraction;
  float2 w0 = 1.0 / 6.0 * one_frac2 * one_frac;
  float2 w1 = 2.0 / 3.0 - 0.5 * fraction2 * (2.0 - fraction);
  float2 w2 = 2.0 / 3.0 - 0.5 * one_frac2 * (2.0 - one_frac);
  float2 w3 = 1.0 / 6.0 * fraction2 * fraction;
  float2 g0 = w0 + w1;
  float2 g1 = w2 + w3;
  
  // h0 = w1/g0 - 1, move from [-0.5, extent-0.5] to [0, extent]
  float2 h0 = (w1 / g0) - 0.5 + index;
  float2 h1 = (w3 / g1) + 1.5 + index;
  
  // fetch the four linear interpolations
  float2 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * inv_src_size, 0.0).xy;
  float2 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * inv_src_size, 0.0).xy;
  float2 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * inv_src_size, 0.0).xy;
  float2 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * inv_src_size, 0.0).xy;
  
  // weigh along the y-direction
  tex00 = lerp(tex01, tex00, g0.y);
  tex10 = lerp(tex11, tex10, g0.y);
  
  // weigh along the x-direction
  return lerp(tex10, tex00, g0.x);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

float2 DownPreFilter(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  return downsample_guassian(pos, texcoord, true); // xyzw -> xw (discard monochrome shadows green & blue)
}

float2 Down(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  return downsample_guassian(pos, texcoord, false);
}

float2 Up(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target 
{
  return upsample_4taps(pos, texcoord);
}

float4 UpPostFilter(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  float2 c = upsample_4taps(pos, texcoord);
  return float4(c.xxx, c.y); // xw -> xxxw (back to rgba for forward lighting passes that are too many to edit)
}