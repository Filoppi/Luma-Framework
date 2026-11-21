#ifndef __SAMPLING_HLSLI__
#define __SAMPLING_HLSLI__

#include "Math.hlsl"

// Hermite sampling in 1 texture fetch.
// Sharper than linear.
//
// smp should be linear.
// Should not be used for downsampling.
//
// f(x) = 2 * |x|^3 - 3 * |x|^2 + 1 for 0 <= |x| <= 1
// f(x) = 0 otherwise
//
#define SAMPLING_TEMPLATE(T) \
T sample_hermite(Texture2D<T> tex, SamplerState smp, float2 uv, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                 \
    if (!specify_src_sizes)                                       \
    {                                                             \
        tex.GetDimensions(src_size.x, src_size.y);                \
        inv_src_size = 1.0 / src_size;                            \
    }                                                             \
                                                                  \
    const float2 f = frac(uv * src_size - 0.5);                   \
    const float2 offset = smoothstep(0.0, 1.0, f) - f;            \
    return tex.SampleLevel(smp, uv + offset * inv_src_size, 0.0); \
}                                                                 \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

// Bicubic sampling in 4 texture fetches.
// Smoother than linear.
//
// smp should be linear.
// Should only be used for upsampling.
//
// f(x) = (4 + 3 * |x|^3 – 6 * |x|^2) / 6 for 0 <= |x| <= 1
// f(x) = (2 – |x|)^3 / 6 for 1 < |x| <= 2
// f(x) = 0 otherwise
//
// Source: https://www.researchgate.net/publication/220494113_Efficient_GPU-Based_Texture_Interpolation_using_Uniform_B-Splines
//
#define SAMPLING_TEMPLATE(T) \
T sample_bicubic(Texture2D<T> tex, SamplerState smp, float2 uv, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                     \
    if (!specify_src_sizes)                                           \
    {                                                                 \
        tex.GetDimensions(src_size.x, src_size.y);                    \
        inv_src_size = 1.0 / src_size;                                \
    }                                                                 \
                                                                      \
    float2 coord_grid = uv * src_size - 0.5;                          \
    float2 index = floor(coord_grid);                                 \
    float2 fraction = coord_grid - index;                             \
    float2 one_frac = 1.0 - fraction;                                 \
    float2 one_frac2 = one_frac * one_frac;                           \
    float2 fraction2 = fraction * fraction;                           \
    float2 w0 = 1.0 / 6.0 * one_frac2 * one_frac;                     \
    float2 w1 = 2.0 / 3.0 - 0.5 * fraction2 * (2.0 - fraction);       \
    float2 w2 = 2.0 / 3.0 - 0.5 * one_frac2 * (2.0 - one_frac);       \
    float2 w3 = 1.0 / 6.0 * fraction2 * fraction;                     \
    float2 g0 = w0 + w1;                                              \
    float2 g1 = w2 + w3;                                              \
                                                                      \
    /* h0 = w1/g0 - 1, move from [-0.5, extent-0.5] to [0, extent] */ \
    float2 h0 = ((w1 / g0) - 0.5 + index) * inv_src_size;             \
    float2 h1 = ((w3 / g1) + 1.5 + index) * inv_src_size;             \
                                                                      \
    /* fetch the four linear interpolations */                        \
    T tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y), 0.0);          \
    T tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y), 0.0);          \
    T tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y), 0.0);          \
    T tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y), 0.0);          \
                                                                      \
    /* weigh along the y-direction */                                 \
    tex00 = lerp(tex01, tex00, g0.y);                                 \
    tex10 = lerp(tex11, tex10, g0.y);                                 \
                                                                      \
    /* weigh along the x-direction */                                 \
    return lerp(tex10, tex00, g0.x);                                  \
}                                                                     \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

// Box (average) sampling in 4 texture fetches.
//
// smp should be linear.
// Should only be used for 2x downsampling.
//
#define SAMPLING_TEMPLATE(T) \
T sample_box_2x(Texture2D<T> tex, SamplerState smp, float2 uv, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                                     \
    if (!specify_src_sizes)                                                           \
    {                                                                                 \
        tex.GetDimensions(src_size.x, src_size.y);                                    \
        inv_src_size = 1.0 / src_size;                                                \
    }                                                                                 \
                                                                                      \
    T csum = 0.0;                                                                     \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size.x, inv_src_size.y), 0.0);   \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size.x, inv_src_size.y), 0.0);  \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size.x, -inv_src_size.y), 0.0);  \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size.x, -inv_src_size.y), 0.0); \
                                                                                      \
    return csum / 4.0;                                                                \
}                                                                                     \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

// Box (average) sampling in 16 texture fetches.
//
// smp should be linear.
// Should only be used for 4x downsampling.
//
#define SAMPLING_TEMPLATE(T) \
T sample_box_4x(Texture2D<T> tex, SamplerState smp, float2 uv, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                                       \
    if (!specify_src_sizes)                                                             \
    {                                                                                   \
        tex.GetDimensions(src_size.x, src_size.y);                                      \
        inv_src_size = 1.0 / src_size;                                                  \
    }                                                                                   \
                                                                                        \
    const float2 inv_src_size3 = inv_src_size * 3.0;                                    \
                                                                                        \
    T csum = 0.0;                                                                       \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size3.x, -inv_src_size3.y), 0.0); \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size3.x, -inv_src_size.y), 0.0);  \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size3.x, inv_src_size.y), 0.0);   \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size3.x, inv_src_size3.y), 0.0);  \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size.x, -inv_src_size3.y), 0.0);  \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size.x, -inv_src_size.y), 0.0);   \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size.x, inv_src_size.y), 0.0);    \
    csum += tex.SampleLevel(smp, uv + float2(-inv_src_size.x, inv_src_size3.y), 0.0);   \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size.x, -inv_src_size3.y), 0.0);   \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size.x, -inv_src_size.y), 0.0);    \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size.x, inv_src_size.y), 0.0);     \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size.x, inv_src_size3.y), 0.0);    \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size3.x, -inv_src_size3.y), 0.0);  \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size3.x, -inv_src_size.y), 0.0);   \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size3.x, inv_src_size.y), 0.0);    \
    csum += tex.SampleLevel(smp, uv + float2(inv_src_size3.x, inv_src_size3.y), 0.0);   \
                                                                                        \
    return csum / 16.0;                                                                 \
}                                                                                       \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

// Fast Catmull-Rom approximation sampling in 5 texture fetches.
// Sharper than linear.
//
// smp should be linear.
// Should not be used for downsampling.
// Will produce ringing artifacts, but it may not be noticable at all.
//
#define SAMPLING_TEMPLATE(T) \
T sample_catmull_rom_approx(Texture2D<T> tex, SamplerState smp, float2 uv, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                  \
    if (!specify_src_sizes)                                        \
    {                                                              \
        tex.GetDimensions(src_size.x, src_size.y);                 \
        inv_src_size = 1.0 / src_size;                             \
    }                                                              \
                                                                   \
    const float2 f = frac(uv * src_size - 0.5);                    \
    const float2 tc = uv - f * inv_src_size;                       \
    const float2 f2 = f * f;                                       \
    const float2 f3 = f2 * f;                                      \
                                                                   \
    /* Catmull-Rom weights. */                                     \
    const float2 w0 = f2 - 0.5 * (f3 + f);                         \
    const float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;                   \
    const float2 w3 = 0.5 * (f3 - f2);                             \
    const float2 w2 = 1.0 - w0 - w1 - w3;                          \
    const float2 w12 = w1 + w2;                                    \
                                                                   \
    /* Texel coords. */                                            \
    const float2 tc0 = tc - inv_src_size;                          \
    const float2 tc3 = tc + inv_src_size * 2.0;                    \
    const float2 tc12 = tc + w2 / w12 * inv_src_size;              \
                                                                   \
    /* Combined weights. */                                        \
    const float w12w0 = w12.x * w0.y;                              \
    const float w0w12 = w0.x * w12.y;                              \
    const float w12w12 = w12.x * w12.y;                            \
    const float w3w12 = w3.x * w12.y;                              \
    const float w12w3 = w12.x * w3.y;                              \
                                                                   \
    T c = 0.0;                                                     \
    c += tex.SampleLevel(smp, float2(tc12.x, tc0.y), 0.0) * w12w0; \
    c += tex.SampleLevel(smp, float2(tc0.x, tc12.y), 0.0) * w0w12; \
    c += tex.SampleLevel(smp, tc12, 0.0) * w12w12;                 \
    c += tex.SampleLevel(smp, float2(tc3.x, tc12.y), 0.0) * w3w12; \
    c += tex.SampleLevel(smp, float2(tc12.x, tc3.y), 0.0) * w12w3; \
                                                                   \
    return c * rcp(w12w0 + w0w12 + w12w12 + w3w12 + w12w3);        \
}                                                                  \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

// Has to be satisfied: n > 0, m > 0.
// n = any, m = 1.0: Garamond window.
// n = 2.0, m = 1.0: Welch window.
// n = 1.0, m = 1.0: Linear window.
// n -> inf, m <= 1.0: Box window.
//
// f(x) = (1 - |x|^n)^m for |x| <= 1
// f(x) = 0 otherwise
//
float sampling_power_of_garamond_window(float x, float support, float n, float m)
{
    return pow(1.0 - pow(x * rcp(support), n), m);
}

// f(x) = sin(pi * x / blur) / (pi * x / blur) for x != 0
// f(x) = 1 for x = 0
//
float sampling_sinc(float x, float blur)
{
    return x < 1e-6 ? PI * rcp(blur) : sin(PI * rcp(blur) * x) * rcp(x);
}

float sampling_get_weight(float x, float blur, float support, float n, float m)
{
    x = abs(x);
    return x >= support ? 0.0 : sampling_sinc(x, blur) * sampling_power_of_garamond_window(x, support, n, m);
}

// Power of Garamond windowed Sinc (kernel radius 2) orthogonal sampling in 16 texture fetches.
// Highly adjustable.
//
// blur - for > 1 blurring and for < 1 sharpening, default 1
// n, m - Power of Garamond window parameters, see https://github.com/garamond13/power-of-garamond-window
//
// Should not be used for downsampling.
//
#define SAMPLING_TEMPLATE(T) \
T sample_ortho(Texture2D<T> tex, SamplerState smp, float2 uv, float blur, float n, float m, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                                        \
    if (!specify_src_sizes)                                                              \
    {                                                                                    \
        tex.GetDimensions(src_size.x, src_size.y);                                       \
        inv_src_size = 1.0 / src_size;                                                   \
    }                                                                                    \
                                                                                         \
    const float2 xy = uv * src_size;                                                     \
    const float2 tc = floor(xy - 0.5) + 0.5;                                             \
    const float2 f = xy - tc;                                                            \
                                                                                         \
    float wsum = 0.0;                                                                    \
    T csum = 0.0;                                                                        \
                                                                                         \
    /* Antiringing. */                                                                   \
    T lo = FLT_MAX;                                                                      \
    T hi = -FLT_MAX;                                                                     \
                                                                                         \
    [unroll]                                                                             \
    for (float y = -1.0; y <= 2.0; ++y)                                                  \
    {                                                                                    \
        const float wy = sampling_get_weight(y - f.y, blur, 2.0, n, m);                  \
                                                                                         \
        [unroll]                                                                         \
        for (float x = -1.0; x <= 2.0; ++x)                                              \
        {                                                                                \
            const T color = tex.SampleLevel(smp, tc + float2(x, y) * inv_src_size, 0.0); \
            const float w = sampling_get_weight(x - f.x, blur, 2.0, n, m) * wy;          \
            csum += color * w;                                                           \
            wsum += w;                                                                   \
                                                                                         \
            /* Antiringing. */                                                           \
            if (y >= 0.0 && y <= 1.0 && x >= 0.0 && x <= 1.0)                            \
            {                                                                            \
                lo = min(lo, color);                                                     \
                hi = max(hi, color);                                                     \
            }                                                                            \
        }                                                                                \
    }                                                                                    \
                                                                                         \
    return clamp(csum * rcp(wsum), lo, hi);                                              \
}                                                                                        \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

// Power of Garamond windowed Sinc (base kernel radius 2) orthogonal sampling in ceil(2 * dst_size / src_size) * 4 texture fetches.
// Highly adjustable.
//
// 2 pass!
// For the first pass render tartget size should be x = dst.x, y = src.y. The input (SRV) is src.
// For the second pass render target size should be x = dst.x, y = dst.y. The input (SRV) is the 1st pass.
//
// inv_dst_size - inverse destination size for the current render target.
// axis - float2(x_axis_status, y_axis_status) 1 for the axis we are currently sampling and 0 for the other.
// blur - for > 1 blurring and for < 1 sharpening, default 1
// n, m - Power of Garamond window parameters, see https://github.com/garamond13/power-of-garamond-window
//
// Example, downsampling 1920x1080 to 960x540.
// 1st pass: inv_dst_size = 1 / float2(960, 1080), axis = float2(1, 0).
// 2nd pass: inv_dst_size = 1 / float2(960, 540), axis = float2(0, 1).
//
// Should not be used for upsampling.
//
#define SAMPLING_TEMPLATE(T) \
T sample_ortho(Texture2D<T> tex, SamplerState smp, float2 uv, float2 inv_dst_size, float2 axis, float blur, float n, float m, bool specify_src_sizes = false, float2 src_size = 0, float2 inv_src_size = 0) \
{                                                                              \
    if (!specify_src_sizes)                                                    \
    {                                                                          \
        tex.GetDimensions(src_size.x, src_size.y);                             \
        inv_src_size = 1.0 / src_size;                                         \
    }                                                                          \
                                                                               \
    const float f = dot(frac(uv * src_size - 0.5), axis);                      \
    const float2 tc = uv - f * inv_src_size * axis;                            \
                                                                               \
    float wsum = 0.0;                                                          \
    T csum = 0.0;                                                              \
                                                                               \
    const float scale = dot(src_size * inv_dst_size, axis);                    \
    const float radius = ceil(2.0 * rcp(scale));                               \
                                                                               \
    for (float i = 1.0 - radius; i <= radius; ++i)                             \
    {                                                                          \
        const float w = sampling_get_weight((i - f) * scale, blur, 2.0, n, m); \
        csum += tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0) * w;   \
        wsum += w;                                                             \
    }                                                                          \
                                                                               \
    return csum * rcp(wsum);                                                   \
}                                                                              \

// Generate functions.
SAMPLING_TEMPLATE(float)
SAMPLING_TEMPLATE(float2)
SAMPLING_TEMPLATE(float3)
SAMPLING_TEMPLATE(float4)

#undef SAMPLING_TEMPLATE

#endif // __SAMPLING_HLSLI__