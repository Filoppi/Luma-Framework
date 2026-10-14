// Batman: Arkham Knight - unjittered motion vectors for super resolution.
// All the game velocities (our replaced camera velocity and the materials' object velocities) are computed
// from the jittered current and previous view projections, so they include the jitter change since the previous frame.
// That change is the same for every pixel, this removes it, so super resolution gets fully unjittered motion vectors
// (zero on static geometry with a static camera).

cbuffer UnjitterCB : register(b0)
{
   float4 Params; // xy: jitter change in UV (previous minus current frame), zw: render size in pixels
}

Texture2D<float2> Velocity : register(t0);
RWTexture2D<float2> UnjitteredVelocity : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   if (any(float2(id.xy) >= Params.zw))
   {
      return;
   }
   UnjitteredVelocity[id.xy] = Velocity.Load(int3(id.xy, 0)) - Params.xy;
}
