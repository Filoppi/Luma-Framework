// MELE distortion apply (refraction of glass, heat haze; a global shader, the same hash in ME1/ME2/ME3): the scene at the pixel
// moved by the accumulated distortion. The scene color is only moved, never computed on, so its encoding doesn't matter here.
//
// The native shader scales the offset by a literal 0.25 in texture UV, right while the scene fills the whole texture. Below
// native render scale the scene fills only SceneColorRect (top left), so the same UV offset moved twice as far at 50%: glass
// showed the frames behind it doubled. The offset is scaled by the rect's size; at native the rect is the whole texture.

cbuffer _Globals : register(b0)
{
   float4 SceneColorRect : packoffset(c0); // Scene UV bounds in the texture: min xy, max zw
}

SamplerState SceneColorTextureSampler_s : register(s0);
SamplerState AccumulatedDistortionTextureSampler_s : register(s1);
Texture2D<float4> SceneColorTexture : register(t0);
Texture2D<float4> AccumulatedDistortionTexture : register(t1);

void main(
    float2 v0 : TEXCOORD0,
    out float4 o0 : SV_Target0)
{
   const float4 distortion = AccumulatedDistortionTexture.Sample(AccumulatedDistortionTextureSampler_s, v0.xy);
   float2 uv = (distortion.xy - distortion.zw) * float2(0.25, -0.25) * (SceneColorRect.zw - SceneColorRect.xy) + v0.xy;
   // Native: an offset leaving the scene's rect samples the pixel itself
   if (any(uv < SceneColorRect.xy) || any(SceneColorRect.zw < uv))
   {
      uv = v0.xy;
   }
   o0.xyz = SceneColorTexture.Sample(SceneColorTextureSampler_s, uv).xyz;
   o0.w = 0;
}
