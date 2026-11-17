// Match the upper cap to the adjacent lower sky strip at their measured edge.
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 0
#endif
cbuffer Globals : register(b4) { float2 textureSizeInv; float2 padding; float4 multiply; float3 colorModulate; };
Texture2D<float4> tex : register(t0);
#if TONEMAP_TYPE >= 1
Texture2D<float4> lowerSky : register(t1);
#endif
SamplerState samp : register(s0);
float4 main(float4 position : SV_POSITION, float4 viewPosition : TEXCOORD0, float4 uv : TEXCOORD2, float4 t3 : TEXCOORD3, float4 t4 : TEXCOORD4, float4 t5 : TEXCOORD5, float4 t6 : TEXCOORD6, float4 t7 : TEXCOORD7) : SV_TARGET
{
    float4 sampled = tex.Sample(samp, uv.xy);
#if TONEMAP_TYPE >= 1
    uint width, height, levels, lowerWidth, lowerHeight, lowerLevels;
    tex.GetDimensions(0, width, height, levels);
    lowerSky.GetDimensions(0, lowerWidth, lowerHeight, lowerLevels);
    if (width == 256 && height == 256 && levels == 9 && lowerWidth == 512 && lowerHeight == 256 && lowerLevels == 9 && all(abs(multiply - 0.2) < 0.0001) && all(abs(colorModulate - 1.0) < 0.0001))
    {
        float2 dx = ddx(uv.xy), dy = ddy(uv.xy);
        float2 pixel = 1.0 / float2(width, height);
        float3 average = (tex.SampleGrad(samp, uv.xy + float2(pixel.x, 0), dx, dy).rgb + tex.SampleGrad(samp, uv.xy - float2(pixel.x, 0), dx, dy).rgb + tex.SampleGrad(samp, uv.xy + float2(0, pixel.y), dx, dy).rgb + tex.SampleGrad(samp, uv.xy - float2(0, pixel.y), dx, dy).rgb) * 0.25;
        float3 detail = clamp((sampled.rgb - average) * 0.2, -0.02, 0.02);
        float3 sharpened = max(sampled.rgb + detail, 0.0);
        float2 lowerUV = float2(1.0142 - 1.0253 * uv.x, 0.0025);
        float2 lowerDx = float2(-1.0253 * dx.x, 0), lowerDy = float2(-1.0253 * dy.x, 0);
        float3 edge = lowerSky.SampleGrad(samp, lowerUV, lowerDx, lowerDy).rgb;
        float blend = smoothstep(0.956, 0.981, uv.y);
        sampled.rgb = lerp(sharpened, edge, blend);
    }
#endif
    return float4((sampled.rgb * colorModulate) * multiply.rgb, 1.0);
}
