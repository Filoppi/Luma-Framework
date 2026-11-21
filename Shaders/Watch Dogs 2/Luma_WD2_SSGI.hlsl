#include "Includes/Common.hlsl"

// Reuse the exact Watch Dogs 2 viewport constant-buffer layout already used by
// Luma_DecodeMotionVector.hlsl. In particular, InvProjectionMatrix reconstructs
// view-space positions from the game's device-depth buffer.
cbuffer cbViewport : register(b0)
{
    float4x4 InvProjectionMatrix : packoffset(c9);
    float4x4 ProjectionMatrix : packoffset(c24);
    float4 ViewportSize : packoffset(c66);
};

cbuffer cbSSGI : register(b1)
{
    float Intensity;
    float MaxDistance;
    float Thickness;
    float NormalBias;
};

Texture2D<float4> SourceColor : register(t0);
Texture2D<float4> SceneDepth : register(t1);
RWTexture2D<float4> OutputColor : register(u0);

static const uint SSGI_RAY_COUNT = 4;
static const uint SSGI_STEP_COUNT = 8;
static const float SSGI_PI = 3.14159265359f;

float3 ReconstructViewPosition(uint2 pixel, out bool valid)
{
    float rawDepth = SceneDepth.Load(int3(pixel, 0)).x;
    valid = rawDepth > 0.000001f;
    if (!valid)
        return 0.0f;

    float2 uv = (float2(pixel) + 0.5f) * ViewportSize.zw;
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    float4 clipPosition = float4(ndc, rawDepth, 1.0f);
    float4 viewPosition = mul(clipPosition, InvProjectionMatrix);
    valid = abs(viewPosition.w) > 0.000001f;
    if (!valid)
        return 0.0f;

    return viewPosition.xyz / viewPosition.w;
}

bool ReconstructViewNormal(uint2 pixel, uint2 dimensions, float3 center, out float3 normal)
{
    float3 leftPosition = 0.0f;
    float3 rightPosition = 0.0f;
    float3 upPosition = 0.0f;
    float3 downPosition = 0.0f;
    bool hasLeft = false;
    bool hasRight = false;
    bool hasUp = false;
    bool hasDown = false;

    if (pixel.x > 0)
        leftPosition = ReconstructViewPosition(pixel - uint2(1, 0), hasLeft);
    if (pixel.x + 1 < dimensions.x)
        rightPosition = ReconstructViewPosition(pixel + uint2(1, 0), hasRight);
    if (pixel.y > 0)
        upPosition = ReconstructViewPosition(pixel - uint2(0, 1), hasUp);
    if (pixel.y + 1 < dimensions.y)
        downPosition = ReconstructViewPosition(pixel + uint2(0, 1), hasDown);

    float3 derivativeX;
    float3 derivativeY;

    if (hasLeft && hasRight)
        derivativeX = abs(rightPosition.z - center.z) < abs(center.z - leftPosition.z)
            ? rightPosition - center : center - leftPosition;
    else if (hasRight)
        derivativeX = rightPosition - center;
    else if (hasLeft)
        derivativeX = center - leftPosition;
    else
        return false;

    if (hasUp && hasDown)
        derivativeY = abs(downPosition.z - center.z) < abs(center.z - upPosition.z)
            ? downPosition - center : center - upPosition;
    else if (hasDown)
        derivativeY = downPosition - center;
    else if (hasUp)
        derivativeY = center - upPosition;
    else
        return false;

    float3 n = cross(derivativeX, derivativeY);
    float nLengthSquared = dot(n, n);
    if (nLengthSquared < 1.0e-10f)
        return false;

    normal = n * rsqrt(nLengthSquared);
    // Orient the normal toward the camera (the view-space camera is at the origin).
    if (dot(normal, -center) < 0.0f)
        normal = -normal;
    return true;
}

float HashPixel(uint2 pixel)
{
    uint h = pixel.x * 1664525u + pixel.y * 1013904223u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return (h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

bool TraceDiffuseRay(uint2 originPixel, uint2 dimensions, float3 origin, float3 direction, out float3 radiance)
{
    radiance = 0.0f;
    float maxDistance = max(MaxDistance, 0.1f);
    float previousDistance = 0.0f;
    float previousDepthDifference = -1.0f;
    bool hasPreviousSample = false;

    [unroll]
    for (uint step = 0; step < SSGI_STEP_COUNT; ++step)
    {
        float distanceAlongRay = maxDistance * ((float)(step + 1) / (float)SSGI_STEP_COUNT);
        float3 rayPosition = origin + direction * distanceAlongRay;
        float4 clipPosition = mul(float4(rayPosition, 1.0f), ProjectionMatrix);
        if (clipPosition.w <= 0.00001f)
            break;

        float2 uv = clipPosition.xy / clipPosition.w;
        uv = uv * float2(0.5f, -0.5f) + 0.5f;
        if (any(uv <= 0.001f) || any(uv >= 0.999f))
            break;

        uint2 hitPixel = min((uint2)(uv * ViewportSize.xy), dimensions - 1);
        if (all(hitPixel == originPixel))
            continue;

        bool validDepth;
        float3 scenePosition = ReconstructViewPosition(hitPixel, validDepth);
        if (!validDepth)
            continue;

        float rayDepth = -rayPosition.z;
        float sceneDepth = -scenePosition.z;
        float depthDifference = rayDepth - sceneDepth;

        // Detect the front-to-back crossing, then refine it. Without refinement, a coarse
        // ray step can jump completely through a thin surface and miss its radiance.
        if (depthDifference >= 0.0f)
        {
            if (hasPreviousSample && previousDepthDifference < 0.0f)
            {
                float lowT = previousDistance;
                float highT = distanceAlongRay;
                uint2 refinedPixel = hitPixel;
                float refinedDifference = depthDifference;
                float refinedSceneDepth = sceneDepth;

                [unroll]
                for (uint refine = 0; refine < 4; ++refine)
                {
                    float midT = (lowT + highT) * 0.5f;
                    float3 midPosition = origin + direction * midT;
                    float4 midClip = mul(float4(midPosition, 1.0f), ProjectionMatrix);
                    if (midClip.w <= 0.00001f)
                    {
                        lowT = midT;
                        continue;
                    }

                    float2 midUV = midClip.xy / midClip.w;
                    midUV = midUV * float2(0.5f, -0.5f) + 0.5f;
                    if (any(midUV <= 0.001f) || any(midUV >= 0.999f))
                    {
                        lowT = midT;
                        continue;
                    }

                    uint2 midPixel = min((uint2)(midUV * ViewportSize.xy), dimensions - 1);
                    if (all(midPixel == originPixel))
                    {
                        lowT = midT;
                        continue;
                    }
                    bool midValid;
                    float3 midScenePosition = ReconstructViewPosition(midPixel, midValid);
                    if (!midValid)
                    {
                        lowT = midT;
                        continue;
                    }

                    float midDifference = -midPosition.z - (-midScenePosition.z);
                    if (midDifference >= 0.0f)
                    {
                        highT = midT;
                        refinedPixel = midPixel;
                        refinedDifference = midDifference;
                        refinedSceneDepth = -midScenePosition.z;
                    }
                    else
                    {
                        lowT = midT;
                    }
                }

                float hitThickness = max(Thickness, refinedSceneDepth * 0.015f);
                if (refinedDifference < hitThickness)
                {
                    radiance = SourceColor.Load(int3(refinedPixel, 0)).rgb;
                    radiance *= 1.0f - 0.35f * (highT / maxDistance);
                    return true;
                }
            }

            // Once the ray is behind a visible surface, do not keep marching through it.
            break;
        }

        previousDistance = distanceAlongRay;
        previousDepthDifference = depthDifference;
        hasPreviousSample = true;
    }

    return false;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    uint2 dimensions = (uint2)ViewportSize.xy;
    if (any(pixel >= dimensions))
        return;

    float4 sceneColor = SourceColor.Load(int3(pixel, 0));
    bool validPosition;
    float3 viewPosition = ReconstructViewPosition(pixel, validPosition);
    if (!validPosition)
    {
        OutputColor[pixel] = sceneColor;
        return;
    }

    float3 normal;
    if (!ReconstructViewNormal(pixel, dimensions, viewPosition, normal))
    {
        OutputColor[pixel] = sceneColor;
        return;
    }

    float3 helperAxis = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 tangent = normalize(cross(helperAxis, normal));
    float3 bitangent = cross(normal, tangent);
    float rotation = HashPixel(pixel);

    float3 indirectRadiance = 0.0f;
    float3 rayOrigin = viewPosition + normal * max(NormalBias, 0.001f);

    [unroll]
    for (uint ray = 0; ray < SSGI_RAY_COUNT; ++ray)
    {
        // Cosine-weighted hemisphere samples. The per-pixel rotation is fixed in screen space
        // so it does not introduce a new random pattern every frame.
        float u = ((float)ray + 0.5f) / (float)SSGI_RAY_COUNT;
        float phi = 2.0f * SSGI_PI * frac(rotation + (float)ray * 0.61803398875f);
        float radius = sqrt(u);
        float3 localDirection = float3(cos(phi) * radius, sin(phi) * radius, sqrt(1.0f - u));
        float3 rayDirection = normalize(
            tangent * localDirection.x +
            bitangent * localDirection.y +
            normal * localDirection.z);

        float3 sampleRadiance;
        if (TraceDiffuseRay(pixel, dimensions, rayOrigin, rayDirection, sampleRadiance))
            indirectRadiance += sampleRadiance;
    }

    indirectRadiance *= 1.0f / (float)SSGI_RAY_COUNT;

    // Clamp unusually bright screen samples so a single emissive/white pixel cannot create a firefly.
    float indirectLuminance = dot(indirectRadiance, float3(0.2126f, 0.7152f, 0.0722f));
    if (indirectLuminance > 6.0f)
        indirectRadiance *= 6.0f / indirectLuminance;

    sceneColor.rgb += indirectRadiance * max(Intensity, 0.0f);
    OutputColor[pixel] = sceneColor;
}
