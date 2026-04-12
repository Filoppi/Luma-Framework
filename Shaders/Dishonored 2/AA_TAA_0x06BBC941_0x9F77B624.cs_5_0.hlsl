struct postfx_luminance_autoexposure_t
{
   float EngineLuminanceFactor;   // Offset:    0
   float LuminanceFactor;         // Offset:    4
   float MinLuminanceLDR;         // Offset:    8
   float MaxLuminanceLDR;         // Offset:   12
   float MiddleGreyLuminanceLDR;  // Offset:   16
   float EV;                      // Offset:   20
   float Fstop;                   // Offset:   24
   uint PeakHistogramValue;       // Offset:   28
};

cbuffer PerInstanceCB : register(b2)
{
   // xy ?, zw size
   float4 cb_positiontoviewtexture : packoffset(c0);
   // xy size, zw inverse size
   float4 cb_taatexsize : packoffset(c1);
   // xy dithering randomization value, zw size
   float4 cb_taaditherandviewportsize : packoffset(c2);
   float4 cb_postfx_tonemapping_tonemappingparms : packoffset(c3);
   float4 cb_postfx_tonemapping_tonemappingcoeffsinverse1 : packoffset(c4);
   float4 cb_postfx_tonemapping_tonemappingcoeffsinverse0 : packoffset(c5);
   float4 cb_postfx_tonemapping_tonemappingcoeffs1 : packoffset(c6);
   float4 cb_postfx_tonemapping_tonemappingcoeffs0 : packoffset(c7);
   uint2 cb_postfx_luminance_exposureindex : packoffset(c8);
   float2 cb_prevresolutionscale : packoffset(c8.z);
   float cb_env_autoexp_adapt_max_luminance : packoffset(c9);
   float cb_view_white_level : packoffset(c9.y);
   float cb_taaamount : packoffset(c9.z);
   float cb_postfx_luminance_customevbias : packoffset(c9.w);
}
  
// Dishonored 2 used this name for this variable, which was renamed to a better name for DOTO
static const float cb_env_tonemapping_white_level = cb_env_autoexp_adapt_max_luminance;

cbuffer PerViewCB : register(b1)
{
   float4 cb_alwaystweak : packoffset(c0);
   float4 cb_viewrandom : packoffset(c1);
   float4x4 cb_viewprojectionmatrix : packoffset(c2);
   float4x4 cb_viewmatrix : packoffset(c6);
   // xy apparently zero and zw appearently one? seemengly unrelated from jitters
   float4 cb_subpixeloffset : packoffset(c10);
   float4x4 cb_projectionmatrix : packoffset(c11);
   float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
   float4x4 cb_previousviewmatrix : packoffset(c19);
   float4x4 cb_previousprojectionmatrix : packoffset(c23);
   float4 cb_mousecursorposition : packoffset(c27);
   float4 cb_mousebuttonsdown : packoffset(c28);
   // Jitters in UV space. xy current frame, zw previous frame.
   float4 cb_jittervectors : packoffset(c29);
   float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
   float4x4 cb_inverseviewmatrix : packoffset(c34);
   float4x4 cb_inverseprojectionmatrix : packoffset(c38);
   // xy are the rendering resolution
   float4 cb_globalviewinfos : packoffset(c42);
   float3 cb_wscamforwarddir : packoffset(c43);
   uint cb_alwaysone : packoffset(c43.w);
   float3 cb_wscamupdir : packoffset(c44);
   // This seems to be true at all times for TAA
   uint cb_usecompressedhdrbuffers : packoffset(c44.w);
   float3 cb_wscampos : packoffset(c45);
   float cb_time : packoffset(c45.w);
   float3 cb_wscamleftdir : packoffset(c46);
   float cb_systime : packoffset(c46.w);
   float2 cb_jitterrelativetopreviousframe : packoffset(c47);
   float2 cb_worldtime : packoffset(c47.z);
   float2 cb_shadowmapatlasslicedimensions : packoffset(c48);
   float2 cb_resolutionscale : packoffset(c48.z);
   float2 cb_parallelshadowmapslicedimensions : packoffset(c49);
   float cb_framenumber : packoffset(c49.z);
   uint cb_alwayszero : packoffset(c49.w);
}

SamplerState smp_linearclamp_s : register(s0);
Texture2D ro_taahistory_read : register(t0);
Texture2D ro_motionvectors : register(t1);
Texture2D ro_viewcolormap : register(t2);
StructuredBuffer<postfx_luminance_autoexposure_t> ro_postfx_luminance_buffautoexposure : register(t3);
RWTexture2D<float4> rw_taahistory_write : register(u0);
RWTexture2D<float4> rw_taaresult : register(u1);

#ifndef VIEWPORT_SIZE
#define VIEWPORT_SIZE cb_taatexsize.xy
#endif

#ifndef INV_VIEWPORT_SIZE
#define INV_VIEWPORT_SIZE cb_taatexsize.zw
#endif

#ifndef MIN_ALPHA
#define MIN_ALPHA 0.04
#endif

// Replicate what the game's native TAA is doing
// pre and post tonemap.
//

float3 pre_tonemap(float3 color, const float exposure, const float factor)
{
    color = cb_usecompressedhdrbuffers ? color * factor : color;
    color = max(0.0, color);
    color = min(cb_env_tonemapping_white_level, color);
    return color * exposure;
}

float3 post_tonemap(float3 color, const float exposure, const float factor)
{
    color *= rcp(exposure);
    color = cb_usecompressedhdrbuffers ? color * rcp(factor) : color;
    return color;
}

//

float get_luma(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 tonemap(float3 color)
{
    return color * rcp(max(1e-6, 1.0 + get_luma(color)));
}

float3 inv_tonemap(float3 color)
{
    return color * rcp(max(1e-6, 1.0 - get_luma(color)));
}

float3 rgb_to_ycocg(float3 color)
{
    const float y = dot(color, float3(0.25, 0.5, 0.25));
    const float co = dot(color, float3(0.5, 0.0, -0.5));
    const float cg = dot(color, float3(-0.25, 0.5, -0.25));
    return float3(y, co, cg);
}

float3 ycocg_to_rgb(float3 color)
{
    const float r = dot(color, float3(1.0, 1.0, -1.0));
    const float g = dot(color, float3(1.0, 0.0, 1.0));
    const float b = dot(color, float3(1.0, -1.0, -1.0));
    return float3(r, g, b);
}

float3 sample_catmull_rom_aprox(Texture2D tex, SamplerState smp, float2 uv)
{
    const float2 f = frac(uv * VIEWPORT_SIZE - 0.5);
    const float2 tc = uv - f * INV_VIEWPORT_SIZE;
    const float2 f2 = f * f;
    const float2 f3 = f2 * f;

    // Catmull-Rom weights.
    const float2 w0 = f2 - 0.5 * (f3 + f);
    const float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    const float2 w3 = 0.5 * (f3 - f2);
    const float2 w2 = 1.0 - w0 - w1 - w3;
    const float2 w12 = w1 + w2;

    // Texel coords.
    const float2 tc0 = tc - 1.0 * INV_VIEWPORT_SIZE;
    const float2 tc3 = tc + 2.0 * INV_VIEWPORT_SIZE;
    const float2 tc12 = tc + w2 / w12 * INV_VIEWPORT_SIZE;

    // Combined weights.
    const float w12w0 = w12.x * w0.y;
    const float w0w12 = w0.x * w12.y;
    const float w12w12 = w12.x * w12.y;
    const float w3w12 = w3.x * w12.y;
    const float w12w3 = w12.x * w3.y;

    float3 c = tex.SampleLevel(smp, float2(tc12.x, tc0.y), 0.0).xyz * w12w0;
    c += tex.SampleLevel(smp, float2(tc0.x, tc12.y), 0.0).xyz * w0w12;
    c += tex.SampleLevel(smp, tc12, 0.0).xyz * w12w12;
    c += tex.SampleLevel(smp, float2(tc3.x, tc12.y), 0.0).xyz * w3w12;
    c += tex.SampleLevel(smp, float2(tc12.x, tc3.y), 0.0).xyz * w12w3;

    // Normalize.
    c *= rcp(w12w0 + w0w12 + w12w12 + w3w12 + w12w3);

    return c;
}

float3 clip_to_aabb(float3 color, float3 minc, float3 maxc)
{
    const float3 center = (minc + maxc) * 0.5;
    const float3 extent = (maxc - minc) * 0.5 + 1e-3;
    const float3 offset = color - center;
    const float3 units = abs(offset * rcp(extent));
    const float max_unit = max(max(units.x, units.y), max(units.z, 1.0));
    return center + offset * rcp(max_unit);
}

[numthreads(16, 16, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // Neighborhood offsets.
    const int2 offsets[8] = {
        int2(-1, -1),
        int2(0, -1),
        int2(1, -1),
        int2(-1, 0),
        int2(1, 0),
        int2(-1, 1),
        int2(0, 1),
        int2(1, 1)
    };

    const float2 uv = (dtid.xy + 0.5) * INV_VIEWPORT_SIZE;

    // From the game's native TAA.
    const float r0z = ro_postfx_luminance_buffautoexposure[cb_postfx_luminance_exposureindex.y].EngineLuminanceFactor;
    float r0w = exp2(-cb_postfx_luminance_customevbias);
    r0w = r0z * r0w; // Exposure?
    const float r1w = cb_view_white_level * r0z; // Compression factor?

    float4 color = ro_viewcolormap.Load(int3(dtid.xy, 0)).xyzw;
    color.xyz = pre_tonemap(color.xyz, r0w, r1w);
    color.xyz = tonemap(color.xyz);
    color.xyz = rgb_to_ycocg(color.xyz);

    // Find the longest motion vector and
    // calculate variance box in 3x3 neighborhood.
    //

    // The longest motion vector.
    float2 longest_mv = ro_motionvectors.Load(int3(dtid.xy, 0)).xy;

    // Variance box.
    float3 m1 = color.xyz;
    float3 m2 = color.xyz * color.xyz;
    float3 minn = color.xyz;
    float3 maxn = color.xyz;

    [unroll]
    for (int i = 0; i < 8; ++i) {
        // The longest motion vector.
        const float2 mv = ro_motionvectors.Load(int3(dtid.xy + offsets[i], 0)).xy;
        longest_mv = dot(mv, mv) > dot(longest_mv, longest_mv) ? mv : longest_mv;

        // Variance box.
        float3 c = ro_viewcolormap.Load(int3(dtid.xy + offsets[i], 0)).xyz;
        c = pre_tonemap(c, r0w, r1w);
        c = tonemap(c);
        c = rgb_to_ycocg(c);		
        m1 += c;
        m2 += c * c;
        minn = min(minn, c);
        maxn = max(maxn, c);
    }

    // Variance box.
    m1 /= 9.0;
    m2 /= 9.0;
    const float3 sigma = sqrt(max(0.0, m2 - m1 * m1));
    const float velocity = length(longest_mv * VIEWPORT_SIZE);
    const float gamma = lerp(1.5, 0.75, saturate(velocity / 20.0));
    const float3 minc = m1 - gamma * sigma;
    const float3 maxc = m1 + gamma * sigma;

    //

    // Sample history.
    const float2 prev_uv = uv - longest_mv;
    float3 history = sample_catmull_rom_aprox(ro_taahistory_read, smp_linearclamp_s, prev_uv);
    history = tonemap(history);
    history = rgb_to_ycocg(history);

    // Clip history.
    history = clamp(history, minn, maxn);
    history = clip_to_aabb(history, minc, maxc);

    // Calculate the alpha (final blend).
    float alpha = lerp(MIN_ALPHA, 0.2, saturate(velocity / 30.0));
    alpha = max(alpha, saturate(0.01 * history.x * rcp(abs(color.x - history.x))));

    // The final color.
    color.xyz = lerp(history, color.xyz, alpha);

    color.xyz = ycocg_to_rgb(color.xyz);
    color.xyz = inv_tonemap(color.xyz);
    rw_taahistory_write[dtid.xy] = float4(color.xyz, 1.0);
    color.xyz = post_tonemap(color.xyz, r0w, r1w);
    rw_taaresult[dtid.xy] = color;
}