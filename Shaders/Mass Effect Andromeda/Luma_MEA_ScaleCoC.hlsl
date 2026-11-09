// The depth of field CoC (signed, px) upsampled to the output size and scaled into its px. A plain nearest stretch steps the in
// focus / out of focus border in render pixels across output-sized color (a blurred light cut into a sharp cheek in blocks), so it's
// a joint (color guided) upsample: of the 4 render texels around an output pixel, the CoC of the one whose color is closest to the
// output color (the depth of field's color input, at both sizes), proximity breaking ties in flat areas. b0 = (render size /
// output size, CoC scale).

#include "../Includes/Math.hlsl"

Texture2D<float> cocTexture : register(t0);
Texture2D<float4> renderColorTexture : register(t1);
Texture2D<float4> outputColorTexture : register(t2);

cbuffer ScaleCoCConstants : register(b0)
{
   float2 sourceToTarget;
   float cocScale;
}

float3 CompressColor(float3 color)
{
   return color / (1.0 + max3(color.r, color.g, color.b));
}

float main(float4 position : SV_Position) : SV_Target
{
   uint2 sourceSize;
   cocTexture.GetDimensions(sourceSize.x, sourceSize.y);
   const float2 source = position.xy * sourceToTarget - 0.5;
   const int2 first = int2(floor(source));
   const float3 target = CompressColor(outputColorTexture.Load(int3(int2(position.xy), 0)).rgb);

   float bestDistance = FLT_MAX;
   float coc = 0.0;
   [unroll] for (int i = 0; i < 4; i++)
   {
      const int2 texel = clamp(first + int2(i & 1, i >> 1), 0, int2(sourceSize) - 1);
      const float3 difference = CompressColor(renderColorTexture.Load(int3(texel, 0)).rgb) - target;
      const float2 offset = float2(texel) - source;
      const float distance = dot(difference, difference) + 0.0001 * dot(offset, offset);
      if (distance < bestDistance)
      {
         bestDistance = distance;
         coc = cocTexture.Load(int3(texel, 0));
      }
   }
   return coc * cocScale;
}
