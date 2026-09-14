// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 25 13:28:34 2026

// 0: Vanilla bilinear downscale
// 1: Area-sampled downscale (Dolphin)
#ifndef ENABLE_AREA_SAMPLING
#define ENABLE_AREA_SAMPLING 1
#endif

cbuffer cb_screen : register(b2)
{
  float4 rtdim : packoffset(c0);
  float4 depth_xform : packoffset(c1);
  float4 envmap_color : packoffset(c2);
  float4 sph_r[3] : packoffset(c3);
  float4 sph_g[3] : packoffset(c6);
  float4 sph_b[3] : packoffset(c9);
}

SamplerState s_clamp_bi_s : register(s6);
Texture2D<float4> t_fxaa_image : register(t0);


// 3Dmigoto declarations
#define cmp -


// By Sam Belliveau and Filippo Tarpini. Public Domain license.
// Adapted from Dolphin's mathematically accurate downscale area filter.
float4 LoadSourcePixel(float2 pixel, uint2 source_size)
{
  int2 coordinate = clamp(int2(pixel), int2(0, 0), int2(source_size) - 1);
  return t_fxaa_image.Load(int3(coordinate, 0));
}

float4 AreaSampling(float2 target_position)
{
  uint source_width;
  uint source_height;
  t_fxaa_image.GetDimensions(source_width, source_height);
  uint2 source_size_uint = uint2(source_width, source_height);
  float2 source_size = float2(source_size_uint);

  // Convert the target pixel box to source pixel space.
  float2 target_begin = floor(target_position);
  float2 target_end = target_begin + 1.0;
  float2 begin = target_begin * rtdim.xy * source_size;
  float2 end = target_end * rtdim.xy * source_size;
  float2 floor_begin = floor(begin);
  float2 floor_end = floor(end);

  // Calculate the source-pixel coverage along each edge of the box.
  float area_west = 1.0 - frac(begin.x);
  float area_north = 1.0 - frac(begin.y);
  float area_east = frac(end.x);
  float area_south = frac(end.y);
  float area_north_west = area_north * area_west;
  float area_north_east = area_north * area_east;
  float area_south_west = area_south * area_west;
  float area_south_east = area_south * area_east;

  float4 average_color = 0.0;
  average_color += area_north_west * LoadSourcePixel(float2(floor_begin.x, floor_begin.y), source_size_uint);
  average_color += area_north_east * LoadSourcePixel(float2(floor_end.x, floor_begin.y), source_size_uint);
  average_color += area_south_west * LoadSourcePixel(float2(floor_begin.x, floor_end.y), source_size_uint);
  average_color += area_south_east * LoadSourcePixel(float2(floor_end.x, floor_end.y), source_size_uint);

  int x_range = int(floor_end.x - floor_begin.x - 0.5);
  int y_range = int(floor_end.y - floor_begin.y - 0.5);

  // A fixed bound avoids unbounded-loop compilation failures on D3D11/12.
  const int max_iterations = 16;
  x_range = min(x_range, max_iterations);
  y_range = min(y_range, max_iterations);

  // Accumulate the north and south edges.
  [unroll]
  for (int ix = 0; ix < max_iterations; ++ix)
  {
    if (ix < x_range)
    {
      float x = floor_begin.x + 1.0 + float(ix);
      average_color += area_north * LoadSourcePixel(float2(x, floor_begin.y), source_size_uint);
      average_color += area_south * LoadSourcePixel(float2(x, floor_end.y), source_size_uint);
    }
  }

  // Accumulate the west and east edges, then the center pixels.
  [unroll]
  for (int iy = 0; iy < max_iterations; ++iy)
  {
    if (iy < y_range)
    {
      float y = floor_begin.y + 1.0 + float(iy);
      average_color += area_west * LoadSourcePixel(float2(floor_begin.x, y), source_size_uint);
      average_color += area_east * LoadSourcePixel(float2(floor_end.x, y), source_size_uint);

      [unroll]
      for (int ix = 0; ix < max_iterations; ++ix)
      {
        if (ix < x_range)
        {
          float x = floor_begin.x + 1.0 + float(ix);
          average_color += LoadSourcePixel(float2(x, y), source_size_uint);
        }
      }
    }
  }

  float corner_area = area_north_west + area_north_east + area_south_west + area_south_east;
  float edge_area = float(x_range) * (area_north + area_south) + float(y_range) * (area_west + area_east);
  float center_area = float(x_range * y_range);
  return average_color / (corner_area + edge_area + center_area);
}


void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

#if ENABLE_AREA_SAMPLING
  o0 = AreaSampling(v0.xy);
#else
  r0.xy = rtdim.xy * v0.xy;
  o0.xyzw = t_fxaa_image.Sample(s_clamp_bi_s, r0.xy).xyzw;
#endif
  return;
}