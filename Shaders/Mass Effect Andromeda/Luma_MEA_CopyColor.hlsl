// The SR output hand-off into the TAA resolve's targets, through the game's own typed UAVs. The in-game "Buffer Format"
// swaps the HDR color chain, the resolve target (u2) and history (u3) included, between rgba16f and r11g11b10_float, and a
// CopySubresourceRegion across formats silently no-ops (black scene, live UI): the typed store converts, and the view is
// writable since the native resolve stores through it.
// copy_color_history_cs: the native resolve (0xD7E13B2A) keeps its history log encoded (see "kHistoryLogOffset"). Raw linear
// RGB there reads back as garbage on the frames the native resolve runs (SR <-> native switches, the dialogue run-native path),
// which its neighborhood clamp turns into a history reset (1-2 frames of shimmer); encoding makes the hand-off seamless. w
// carries no current-frame data natively: 0.
// Above the render size both downsample, bilinear at the target's pixel centres (a 2x2 box at 50%): the targets stay render
// sized for the passes that read them and for the native fallback.
Texture2D<float4> src : register(t0);   // Luma SR output (rgba16f), at the target's size or larger
RWTexture2D<float4> dst : register(u0); // the game's resolve/history target, bound via its own typed UAV
SamplerState linear_sampler : register(s0);

// The native history encoding: enc = (log2(c + offset) - log2(100)) / range + 1, stored x512 and read back as sampled / 512
static const float kHistoryLogOffset = 0.008632;
static const float kHistoryLog2Of100 = 6.643856;
static const float kHistoryLogRange = 13.5;
static const float kHistoryScale = 512.0;

// The source at a target pixel: a 1:1 load at the same size (exact), else a bilinear sample at its centre
float4 SampleSource(uint2 dtid, uint2 dst_size)
{
   uint src_w, src_h;
   src.GetDimensions(src_w, src_h);
   // One exit: an early return inside the inlined helper trips fxc's X4000
   float4 color;
   [branch] if (src_w == dst_size.x && src_h == dst_size.y)
   {
      color = src.Load(int3(dtid, 0));
   }
   else
   {
      color = src.SampleLevel(linear_sampler, (dtid + 0.5) / float2(dst_size), 0);
   }
   return color;
}

// clang-format glues the attribute to the function and indents the second entry point.
// clang-format off
[numthreads(8, 8, 1)]
void copy_color_cs(uint2 dtid : SV_DispatchThreadID)
{
   uint w, h;
   dst.GetDimensions(w, h);
   if (dtid.x >= w || dtid.y >= h)
      return;
   dst[dtid] = SampleSource(dtid, uint2(w, h));
}

[numthreads(8, 8, 1)]
void copy_color_history_cs(uint2 dtid : SV_DispatchThreadID)
{
   uint w, h;
   dst.GetDimensions(w, h);
   if (dtid.x >= w || dtid.y >= h)
      return;
   // Not a range clamp: below -kHistoryLogOffset the log2 is NaN (the encode has no sign)
   float3 c = max(SampleSource(dtid, uint2(w, h)).rgb, 0.0);
   float3 enc = (log2(c + kHistoryLogOffset) - kHistoryLog2Of100) / kHistoryLogRange + 1.0;
   dst[dtid] = float4(enc * kHistoryScale, 0.0);
}
// clang-format on
