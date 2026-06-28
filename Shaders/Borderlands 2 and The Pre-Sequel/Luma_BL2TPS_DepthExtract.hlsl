// Borderlands 2 / The Pre-Sequel — extract a normalized depth for SMAA predication.
//
// Both games (dgVoodoo DX9->11) expose NO separate depth buffer, but the fp16 scene-color buffer carries linear
// view-space Z in its ALPHA channel (UE3 pattern): geometry = NEGATIVE z (right-handed view space, |z| grows
// with distance), sky/invalid = a >= 0 (a large +sentinel). We read that, decode depth = -a, and perceptually
// compress it into [0,1] so silhouette deltas comfortably exceed SMAA's predication threshold (~0.01) at all
// distances. Output is single-channel (R) because SMAA predication Gathers the R channel (SMAA.hlsl).

Texture2D<float4> scene : register(t0); // fp16 scene color; .a = view-space Z (negative for geometry)
RWTexture2D<float> uav : register(u0);  // R16_FLOAT normalized predication depth

cbuffer PredCB : register(b0)
{
   float4 P; // P.x = compress constant k (world units); larger k = more spread pushed to the far field
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   float a = scene.Load(int3(id.xy, 0)).a;
   // Geometry: a < 0 -> linear depth = -a. Sky / invalid (a >= 0, e.g. the +65472 far sentinel) -> treat as
   // very far so foreground silhouettes against sky register a large predication delta.
   float depth = (a < 0.0) ? -a : 1e9;
   float k = max(P.x, 1.0);
   uav[id.xy] = depth / (depth + k); // near -> 0, far -> 1, monotonic; deltas are largest at silhouettes
}
