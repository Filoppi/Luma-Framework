Texture2D<float4> ColorTexture : register(t0);
Texture2D<float> DepthTexture : register(t1);

SamplerState ColorSampler : register(s0);
SamplerState DepthSampler : register(s1);

cbuffer cb0 : register(b0)
{
    float4 cb0[9];
}

void main(
    float4 position          : SV_POSITION,
    float4 vertexColor       : COLOR0,
    float2 colorUV           : TEXCOORD0,
    float4 secondaryTexcoord : TEXCOORD1,
    out float4 output        : SV_TARGET0)
{
    // Reconstruct the depth UV from the destination pixel position using the
    // original 2x scale, pixel offset, and depth-buffer dimensions.
    float2 depthUV = (position.xy * 2.0 + cb0[4].xy) / cb0[8].xy;
    float depth = DepthTexture.Sample(DepthSampler, depthUV).r;

    float circleOfConfusion = 0.0;
    float cocMode = cb0[2].y;

    if (cocMode == 1.0)
    {
        // Linear CoC mode. Negative values represent foreground blur and
        // positive values represent background blur.
        if (depth > cb0[0].z)
        {
            circleOfConfusion = min(cb0[0].w * (depth - cb0[0].z), cb0[1].y);
        }
        else if (depth < cb0[0].x)
        {
            circleOfConfusion = max(cb0[0].y * (depth - cb0[0].x), -cb0[1].x);
        }
    }
    else if (cocMode == 2.0)
    {
        // Perspective CoC mode. Preserve the original direct division by
        // depth; the game is responsible for supplying a valid depth value.
        if (depth > cb0[0].w)
        {
            circleOfConfusion = ((depth - cb0[0].w) / depth) * cb0[0].z;
        }
        else if (depth < cb0[0].y)
        {
            circleOfConfusion = ((depth - cb0[0].y) / depth) * cb0[0].x;
        }
    }

    circleOfConfusion = clamp(circleOfConfusion, -1.0, 1.0);

    output.rgb = ColorTexture.Sample(ColorSampler, colorUV).rgb;
    output.a = circleOfConfusion * cb0[2].x;
}
