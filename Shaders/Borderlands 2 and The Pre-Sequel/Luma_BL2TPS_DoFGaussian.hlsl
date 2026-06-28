// Separable Gaussian blur for the "Luma Gaussian" DoF mode (the only Luma DoF path; grain-free).
// Dispatched twice on the half-res prefiltered scene: horizontal then vertical. Each pixel is
// a dense, normalized 1D Gaussian along the axis -> no sparse taps, no jitter -> no graininess by
// construction. The tonemap then blends this blurred half-res buffer with the sharp scene by the game's
// per-pixel focus weight (the GPU Gems 3 Ch.28 single-radius-blur-blended-by-CoC model, which is also how
// the game's own vanilla DoF works). HDR is preserved (linear fp16 source) so bright defocused areas glow.

cbuffer cb_dof_blur : register(b0)
{
   float2 BlurAxisInvSize; // per-tap UV step along the blur axis: (1/halfW, 0) for H, (0, 1/halfH) for V
   float BlurSigma;        // gaussian sigma in half-res pixels (derived from the DoF Radius slider)
   float _dof_blur_pad;
}

Texture2D<float4> t0 : register(t0);         // half-res source (prefilter output for H, H output for V)
SamplerState s0 : register(s0);              // linear-clamp
RWTexture2D<float4> out_blur : register(u0); // half-res blurred output

[numthreads(8, 8, 1)] void dof_blur_cs(uint3 id : SV_DispatchThreadID) {
   uint w, h;
   t0.GetDimensions(w, h);
   if (id.x >= w || id.y >= h)
      return;

   const float2 uv = (float2(id.xy) + 0.5) / float2(w, h);
   const float sigma = max(BlurSigma, 0.5);
   const int radius = (int)clamp(ceil(2.0 * sigma), 1.0, 32.0); // up to 65 taps/axis
   const float inv2s2 = rcp(2.0 * sigma * sigma);

   float3 sum = 0.0;
   float wsum = 0.0;
   [loop] for (int i = -radius; i <= radius; i++)
   {
      float wt = exp(-(float)(i * i) * inv2s2);
      sum += t0.SampleLevel(s0, uv + (float)i * BlurAxisInvSize, 0).rgb * wt;
      wsum += wt;
   }
   out_blur[id.xy] = float4(sum * rcp(max(wsum, 1e-6)), 1.0);
}
