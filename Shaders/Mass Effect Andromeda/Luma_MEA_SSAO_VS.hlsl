// Full screen VS for the game's SSAO pixel shaders (AO PS 0xF93F1662, blurs 0xB654AC8B / 0x0722BC95) at any target size.
// Same outputs as its own VS 0x75247A64 / 0xCD03DB44 (they take pixel positions from a vertex buffer made for the engine's
// size): TEXCOORD0 = UV and the 4x4 noise UV (one noise texel per pixel), TEXCOORD1 = UV.

cbuffer cb0 : register(b0)
{
   float4 texel_size; // xy: 1 / the target size (the AO and blur constants' c0)
}

struct VS_OUT
{
   float4 pos : SV_Position;
   float4 uv_noise_uv : TEXCOORD0;
   float2 uv : TEXCOORD1;
};

VS_OUT main(uint vertexIdx : SV_VertexID)
{
   VS_OUT o;
   const float2 uv = float2(vertexIdx & 1, vertexIdx >> 1);
   o.pos = float4((uv.x - 0.5) * 2, -(uv.y - 0.5) * 2, 0, 1);
   o.uv_noise_uv = float4(uv, uv / texel_size.xy * 0.25);
   o.uv = uv;
   return o;
}
