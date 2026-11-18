// Under the engine's render scale with DLSS / FSR, the post passes from the scene's end up to stage 1 run at output size (the view
// swap in main.cpp), but what the scene wrote (depth, velocity) still fills only the top-left render share of its output sized
// target. This stretches that share over a whole copy the post passes read instead: nearest (point), so depth and velocity are
// never blended across edges. Values are copied unchanged; the destination format only differs to
// allow the UAV store (a depth target's r24 is stored as r32 float).

cbuffer RenderShareStretch : register(b0)
{
   float2 render_share; // Render size / view size (the output size, or less in a letterboxed cinematic)
};

Texture2D<float4> source : register(t0);
RWTexture2D<float4> destination : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   uint2 size;
   destination.GetDimensions(size.x, size.y);
   if (any(id.xy >= size))
      return;
   destination[id.xy] = source.Load(int3((id.xy + 0.5) * render_share, 0));
}
