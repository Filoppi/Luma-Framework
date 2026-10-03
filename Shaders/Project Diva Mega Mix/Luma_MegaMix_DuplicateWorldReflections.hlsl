

cbuffer Batch : register(b1)
{
  float4 g_texture_lod : packoffset(c0);
  float4 g_color_scale : packoffset(c1);
  float4 g_color_offset : packoffset(c2);
}

cbuffer CBMegaMixColorAux : register(b11)
{
  float2 src_size;
  float2 inv_src_size;
  float2 axis;
  // float sigma;
}
#define SUGMA ((CUSTOM_DWR_SIGMA + 1) / 5.0) * 1.46

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

float3 downsample_guassian(float4 pos, float2 texcoord, float sigmaBoost = 0)
{
  // texcoord
  const float f = dot(frac(texcoord * src_size - 0.5), axis);
  const float2 tc = texcoord - f * inv_src_size * axis;
  // return tc.xy; // debug

  // accumulators
  float3 csum = 0.0;
  float wsum = 0.0;
  float sigma1 = SUGMA + sigmaBoost;

  // loop
  const float radius = ceil(sigma1 * 3.0);
  for (float i = 1.0 - radius; i <= radius; i++)
  {
    float3 c = tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0).xyz;

    const float w = get_gaussian_weight(i - f, sigma1);
    csum += c * w;
    wsum += w;
  }
  
  // normalize
  csum *= rcp(wsum);
  
  return csum;
}

float3 upsample_4taps(float4 pos, float2 texcoord) : SV_Target
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
  float3 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * inv_src_size, 0.0).xyz;
  float3 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * inv_src_size, 0.0).xyz;
  float3 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * inv_src_size, 0.0).xyz;
  float3 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * inv_src_size, 0.0).xyz;
  
  // weigh along the y-direction
  tex00 = lerp(tex01, tex00, g0.y);
  tex10 = lerp(tex11, tex10, g0.y);
  
  // weigh along the x-direction
  return lerp(tex10, tex00, g0.x);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

float3 Down(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  return downsample_guassian(pos, texcoord);
}

float3 DownFilter(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target
{
  float3 r = g_color_scale.xyz * 4; // normalize original 4 taps
  return downsample_guassian(pos, texcoord) * r;
}

float3 UpFilter(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target 
{
  return upsample_4taps(pos, texcoord) * g_color_scale.xyz + g_color_offset.xyz; //r11g11b10f is pos only
}

float3 Up(float4 pos: SV_Position, float2 texcoord: TEXCOORD) : SV_Target 
{
  return upsample_4taps(pos, texcoord);
}