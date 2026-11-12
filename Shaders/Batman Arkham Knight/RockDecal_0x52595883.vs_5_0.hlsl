// Batman: Arkham Knight - FRockDecalVertexShader (CRC32 0x52595883), replaced.
//
// Draws the box of a deferred decal. Every decal pixel shader (FRockDecalPixelShader, one per material) rebuilds the position
// of the scene under the pixel from the clip position passed in TEXCOORD0 and the scene depth:
//    ndc = TEXCOORD0.xy / TEXCOORD0.w
//    depth pixel = uint(ndc * ScreenPositionScaleBias * buffer size)
//    view position = float3(ndc * (1 / projection scale), 1) * view depth
// and then moves it to decal space, discarding the pixel if it's outside of the decal box.
// The view position math assumes a projection centered on the view axis, but with super resolution the projection is jittered (off center),
// so the rebuilt position was off by the jitter (up to half a pixel, in a different direction every frame):
// decal textures didn't jitter with the scene they're projected on, and pixels close to the faces of (thin) decal boxes flickered in and out of them.
// The pixel shaders (more than a thousand) are left alone: this passes them the clip position with the jitter removed, which makes the view position exact.
// The depth pixel is still the right one: the jitter is always smaller than half a pixel, and the pixel shaders truncate the pixel coordinates (from the pixel center).
// The jitter is taken from the view projection itself, so views that have none are unaffected.
// Bindings and signatures are identical to the original.

cbuffer ViewCB : register(b1)
{
   float4 c1[4];
   // c1[0..3]: translated world to clip (row vectors), jittered for the main view
}

struct VSInput
{
   float4 Position : POSITION;
   float4 LocalToTranslatedWorld[4] : TEXCOORD0; // Per instance
   float4 InstanceData[7] : TEXCOORD4;           // Per instance (translated world to decal etc), only used by the pixel shaders
   float4 Tangent : TANGENT;
   float4 Binormal : BINORMAL;
   float4 Normal : NORMAL;
};

struct VSOutput
{
   float4 Position : SV_Position;
   float3 Tangent : TANGENT;
   float3 Binormal : BINORMAL;
   float3 Normal : NORMAL;
   float4 ClipPosition : TEXCOORD0;
   float4 InstanceData[7] : TEXCOORD4;
};

VSOutput main(VSInput input)
{
   VSOutput output;

   float4 translated_world = input.Position.x * input.LocalToTranslatedWorld[0] + input.Position.y * input.LocalToTranslatedWorld[1] + input.Position.z * input.LocalToTranslatedWorld[2] + input.Position.w * input.LocalToTranslatedWorld[3];
   float4 clip = translated_world.x * c1[0] + translated_world.y * c1[1] + translated_world.z * c1[2] + translated_world.w * c1[3];

   // For perspective projections, clip w is the view depth, so its coefficients are the view forward axis, and that direction projects to the third row of the projection matrix:
   // (jitter x, jitter y, depth scale, 1). This is 0 for centered projections, and for orthographic ones (the forward axis is 0 then).
   float3 view_forward = float3(c1[0].w, c1[1].w, c1[2].w);
   float2 jitter = view_forward.x * c1[0].xy + view_forward.y * c1[1].xy + view_forward.z * c1[2].xy;

   output.Position = clip;
   output.ClipPosition = float4(clip.xy - jitter * clip.w, clip.zw);
   output.Tangent = input.Tangent.xyz;
   output.Binormal = input.Binormal.xyz;
   output.Normal = input.Normal.xyz;
   output.InstanceData = input.InstanceData;
   return output;
}
