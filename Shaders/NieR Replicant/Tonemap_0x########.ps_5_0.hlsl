#include "Includes/Common.hlsl"
#pragma warning( disable : 3571)

// There are 62 permutations
// I think the only permutations actually used are the ones with everything disabled and the one with only APPLY_POST_PROCESS

#if _1B8367C6 || _2685FF45 || _31FF4D91 || _59302561 || _5DE08981 || _6DF41D20 || _88A1CCBA || _91993DC4 || _939D4667 || _A1A5BEF7 || _A6B8160E || _ADDE23B0 || _B104F0E0 || _B42C78C9 || _F24DAEA3 || _FC031FC2
#define TONEMAP_FOUR_POINT
#endif

#if _1CD01365 || _2A516D34 || _39D658BC || _3E142416 || _85778F80 || _8F609210 || _96F7E635 || _A59F6D98 || _BECCA52E || _BFF7D818 || _CD7AA2E4 || _D270A735 || _D4B5F3B6 || _F10BD1EB || _FE6C4D15
#define TONEMAP_SIMPLE
#endif

#if _12F91938 || _134D5127 || _1D46FC37 || _325E18C9 || _44285E78 || _45BBA609 || _5A0EA592 || _7C9826D7 || _80EF63B9 || _8BE0264D || _90EE1419 || _9422AA33 || _BD39BA78 || _C93A88D1 || _D74BFC47 || _E6563C2D || _F5C3DAD8
#define TONEMAP_INUI
#endif

#if _12F91938 || _134D5127 || _1CD01365 || _1D46FC37 || _2685FF45 || _2A516D34 || _31FF4D91 || _325E18C9 || _39D658BC || _3E142416 || _44285E78 || _45BBA609 || _59302561 || _5A0EA592 || _5DE08981 || _7C9826D7 || _88A1CCBA || _8BE0264D || _90EE1419 || _91993DC4 || _939D4667 || _96F7E635 || _A59F6D98 || _A6B8160E || _ADDE23B0 || _B104F0E0 || _B42C78C9 || _BECCA52E || _CD7AA2E4 || _D4B5F3B6 || _D74BFC47 || _F10BD1EB || _F5C3DAD8 || _FC031FC2 || _FE6C4D15
#define HIGHLIGHT_THRESHOLD
#endif

#if _0701FA1A || _0AF8FBCC || _12F91938 || _134D5127 || _1CD01365 || _2685FF45 || _2A516D34 || _31FF4D91 || _39D658BC || _3E142416 || _44285E78 || _58F52206 || _59302561 || _5B18AA0C || _5DE08981 || _675FFB89 || _7C9826D7 || _88A1CCBA || _90EE1419 || _939D4667 || _B59F3693 || _D4B5F3B6 || _F5C3DAD8
#define WORKING_COLOR_BT2020
#endif

#if _12F91938 || _1CD01365 || _31FF4D91 || _39D658BC || _3E142416 || _44285E78 || _45BBA609 || _5B18AA0C || _5DE08981 || _656E3714 || _675FFB89 || _6DF41D20 || _7EEEE3BF || _85778F80 || _8BE0264D || _8F609210 || _90EE1419 || _939D4667 || _9422AA33 || _9C292250 || _A1A5BEF7 || _A6B8160E || _ADDE23B0 || _B104F0E0 || _B59F3693 || _BD39BA78 || _BECCA52E || _CD7AA2E4 || _D9ACC9FF || _E6563C2D || _F10BD1EB
#define APPLY_POST_PROCESS
#endif

#if _029A4C03 || _12F91938 || _134D5127 || _31FF4D91 || _325E18C9 || _39D658BC || _58F52206 || _59302561 || _656E3714 || _8BE0264D || _91993DC4 || _ADDE23B0 || _B59F3693 || _CD7AA2E4 || _D4B5F3B6 || _FE6C4D15
#define OUTPUT_PQ
#endif

#if _0701FA1A || _1D46FC37 || _2685FF45 || _2A516D34 || _3E142416 || _44285E78 || _5DE08981 || _5E926B65 || _9422AA33 || _96F7E635 || _9C292250 || _B104F0E0 || _BECCA52E || _F5C3DAD8 || _FC031FC2
#define OUTPUT_PIECEWISE
#endif

#if _675FFB89 || _6DF41D20 || _85778F80 || _BBC9C6BB || _C93A88D1 || _D270A735 || _D9ACC9FF || _E6563C2D || _F24DAEA3
#define OUTPUT_SRGB
#endif

#if defined(TONEMAP_FOUR_POINT) || defined(TONEMAP_SIMPLE) || defined(TONEMAP_INUI)
#define TONEMAPPING
#endif

cbuffer CbTonemap : register(b0)
{
	float max_luminance : packoffset(c0);
	float base_luminance : packoffset(c0.y);
	float exposure : packoffset(c0.z);
	float contrast : packoffset(c0.w);
	float mid_start : packoffset(c1);
	float mid_length : packoffset(c1.y);
	float toe : packoffset(c1.z);
	float toe_offset : packoffset(c1.w);
	float2 mask : packoffset(c2);
	float inui_branch : packoffset(c2.z);
	float inui_slope : packoffset(c2.w);
	float inui_a : packoffset(c3);
	float inui_m : packoffset(c3.y);
	float inui_s : packoffset(c3.z);
	float min_luminance : packoffset(c3.w);
	float user_gamma : packoffset(c4);
}

#ifdef TONEMAP_FOUR_POINT
cbuffer CbFourPointToneCurve : register(b1)
{
	float cb_toe_offset_x : packoffset(c0);
	float cb_toe_offset_y : packoffset(c0.y);
	float cb_toe_mul : packoffset(c0.z);
	float cb_mid_start_x : packoffset(c0.w);
	float cb_mid_start_y : packoffset(c1);
	float cb_mid_end_x : packoffset(c1.y);
	float cb_mid_end_y : packoffset(c1.z);
	float cb_mid_grad : packoffset(c1.w);
	float cb_shoulder_offset_x : packoffset(c2);
	float cb_shoulder_offset_y : packoffset(c2.y);
	float cb_shoulder_mul : packoffset(c2.z);
	float cb_top_start_x : packoffset(c2.w);
	float cb_top_start_y : packoffset(c3);
	float cb_top_grad : packoffset(c3.y);
	bool cb_apply_rgb_independent : packoffset(c3.z);
}
#endif

#ifdef TONEMAP_FOUR_POINT
cbuffer CbTonemapPostProcess : register(b2)
#else
cbuffer CbTonemapPostProcess : register(b1)
#endif
{
	float2 cb_postgrad_dir : packoffset(c0) = {0,1};
	float cb_postgrad_offset : packoffset(c0.z) = {0};
	float cb_postgrad_bandrange_inv : packoffset(c0.w) = {1};
	float3 cb_postgrad_color : packoffset(c1) = {0,0,0};
	float cb_postgrad_rate_exp : packoffset(c1.w) = {1};
	int cb_postgrad_para_flare_switch : packoffset(c2) = {0};
}

#ifdef TONEMAP_FOUR_POINT
float ToneCurve(float x)
{
    float toe_value =
        cb_toe_offset_y
        - 1.0 / (cb_toe_mul * (x + cb_toe_offset_x));

    float mid_value =
        cb_mid_grad * (x - cb_mid_start_x)
        + cb_mid_start_y;

    float shoulder_value =
        cb_shoulder_offset_y
        - 1.0 / (cb_shoulder_mul * (x + cb_shoulder_offset_x));

    float top_value =
        cb_top_grad * (x - cb_top_start_x)
        + cb_top_start_y;

    float result = toe_value;

    if (x >= cb_mid_start_x)
        result = mid_value;

    if (x >= cb_mid_end_x)
        result = shoulder_value;

    if (x >= cb_top_start_x)
        result = top_value;

    return result;
}

float3 ToneCurve(float3 x)
{
    return float3(
        ToneCurve(x.r),
        ToneCurve(x.g),
        ToneCurve(x.b)
    );
}
#endif

SamplerState tex_Color_s_s : register(s0);
Texture2D<float4> tex_Color : register(t0);

void main(
	float4 v0 : SV_POSITION0,
	linear noperspective float2 uv : TEXCOORD0,
	out float4 o0 : SV_TARGET0)
{
	float3 srcColor = tex_Color.Sample(tex_Color_s_s, uv).xyz;
	float srcLuminance = dot(srcColor, float3(0.212599993f,0.715200007f,0.0722000003f));
	
	bool isInsideMask = all(uv < mask);
	float luminanceScale = isInsideMask ? 1.0f : base_luminance;
#ifdef TONEMAPPING
	float scaledLuminance = srcLuminance * luminanceScale;
	float clampedLuminance = (scaledLuminance >= max_luminance)? 100.0f : scaledLuminance;
#ifdef HIGHLIGHT_THRESHOLD
	float curveLuminance = clampedLuminance;
#else
	float curveLuminance = scaledLuminance;
#endif
#ifdef TONEMAP_FOUR_POINT
#ifdef HIGHLIGHT_THRESHOLD
	float highlightScale = clampedLuminance / scaledLuminance;
	float mappedLuminance = ToneCurve(curveLuminance);
#else
	float mappedLuminance = ToneCurve(curveLuminance);
#endif
#elif defined(TONEMAP_SIMPLE)
	float4 r0,r1,r2;
	r0.x = -min_luminance + max_luminance;
	r0.x = max(0, r0.x);
	r0.x = r0.x / base_luminance;
	r0.y = -mid_start + r0.x;
	r0.z = r0.y * mid_length + mid_start;
	r0.y = mid_length * r0.y;
	r0.y = r0.y / contrast;
	r0.y = mid_start + r0.y;
	r0.z = r0.x + -r0.z;
	r0.w = contrast * r0.x;
	r0.w = r0.w / r0.z;
	r0.w = -r0.w / r0.x;
	r2.x = curveLuminance + -r0.y;
	r0.y = curveLuminance >= r0.y;
	r0.w = r2.x * r0.w;
	r0.w = 1.44269502 * r0.w;
	r0.w = exp2(r0.w);
	r0.x = -r0.z * r0.w + r0.x;
	r0.z = 1 / mid_start;
	r0.z = saturate(curveLuminance * r0.z);
	r0.w = r0.z * -2 + 3;
	r0.z = r0.z * r0.z;
	r0.z = -r0.w * r0.z + 1;
	r0.w = 1 + -r0.z;
	r2.x = r0.y ? -1 : -0;
	r0.y = r0.y ? 1.000000 : 0;
	r0.w = r2.x + r0.w;
	r2.x = -mid_start + curveLuminance;
	r1.x = curveLuminance / mid_start;
	r1.x = log2(r1.x);
	r1.x = toe * r1.x;
	r1.x = exp2(r1.x);
	r1.x = mid_start * r1.x + toe_offset;
	r2.x = contrast * r2.x + mid_start;
	r0.w = r2.x * r0.w;
	r0.z = r1.x * r0.z + r0.w;
	float mappedLuminance = r0.x * r0.y + r0.z;
#elif defined(TONEMAP_INUI)
	float4 r0,r1;
	r0.x = -min_luminance + max_luminance;
	r0.x = max(0.0f, r0.x);
	r0.x = r0.x / base_luminance;
	r0.y = r0.x + r0.x;
	r1.w = -inui_branch + curveLuminance;
	r0.y = r1.w * r0.y;
	r0.x = r0.x * 2.0f + r1.w;
	r0.x = r0.y / r0.x;
	r0.x = inui_slope * r0.x + 0.5f;
	r0.y = inui_s * curveLuminance;
	r0.z = inui_branch >= curveLuminance;
	r0.y = log2(r0.y);
	r0.y = -inui_a * r0.y;
	r0.y = exp2(r0.y);
	r0.y = 1.0f + r0.y;
	r0.y = inui_m / r0.y;
	float mappedLuminance = r0.z ? r0.y : r0.x;
#endif
#endif
#ifdef WORKING_COLOR_BT2020
	float3 tmpColor;
	tmpColor.x = dot(float3(0.627403975f,0.329282999f,0.0433130004f), srcColor);
	tmpColor.y = dot(float3(0.0690969974f,0.919539988f,0.0113620004), srcColor);
	tmpColor.z = dot(float3(0.0163909998f,0.0880130008f,0.895595014f), srcColor);
	srcColor = tmpColor;
#endif
#ifdef TONEMAPPING
#ifdef TONEMAP_FOUR_POINT
#ifdef HIGHLIGHT_THRESHOLD
	float3 mappedColor = ToneCurve(srcColor * luminanceScale * highlightScale);
#else
	float3 mappedColor = ToneCurve(srcColor);
#endif
	float3 normalizedColor = srcColor / srcLuminance;
	float3 dstColor = cb_apply_rgb_independent ? (mappedColor + min_luminance) : (normalizedColor * mappedLuminance + min_luminance);
#else
	float3 normalizedColor = srcColor / srcLuminance;
	float3 dstColor = normalizedColor * mappedLuminance + min_luminance;
#endif
#else
	float3 dstColor = srcColor * luminanceScale;
#endif
	if (LumaSettings.DisplayMode == 0)
	{
		dstColor = pow(dstColor, user_gamma);
	}
	else
	{
		const float relativePeakWhite = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
		float highlightsStrength = 2.0;
		dstColor = Reinhard::ReinhardAdvanced(dstColor, MidGray, FLT_MAX, relativePeakWhite, highlightsStrength);
	}
	
#ifdef APPLY_POST_PROCESS
	float3 invColor = max(1.0f - dstColor, 0.0f);
	float3 gradedColor = cb_postgrad_para_flare_switch ? (dstColor + invColor * cb_postgrad_color) : (dstColor * cb_postgrad_color);
	gradedColor = gradedColor - dstColor;
	float postGradient = dot(uv + float2(-0.5f,-0.5f), cb_postgrad_dir) - cb_postgrad_offset;
	postGradient = saturate(dot(postGradient.xx, cb_postgrad_bandrange_inv.xx));
	postGradient = 0 < postGradient ? pow(postGradient, max(0.0f, cb_postgrad_rate_exp)) : 0;
	dstColor += postGradient * gradedColor;
#endif
#ifdef OUTPUT_PQ
	float3 pqInput = pow(dstColor, 1.20000005) * 0.00999999978f;
	pqInput = pow(pqInput, 0.159301758f);
	float3 pqNumerator = pqInput * 18.8515625f + 0.8359375f;
    float3 pqDenominator = pqInput * 18.6875f + 1.0f;
	float3 pqRatio = pqNumerator / pqDenominator;
	dstColor = pow(pqRatio, 78.84375);
#elif defined(OUTPUT_SRGB)
	dstColor = linear_to_sRGB_gamma(dstColor);
#elif defined(OUTPUT_PIECEWISE)
	dstColor = dstColor <= 1.0f ? (float3(0.5,0.5,0.5) * sqrt(dstColor)): (log2(float3(-0.284668922,-0.284668922,-0.284668922) + dstColor) * float3(0.123957433,0.123957433,0.123957433) + float3(0.559910715,0.559910715,0.559910715));
#else
	dstColor = dstColor;
#endif
	if (LumaSettings.DisplayMode == 1)
	{
		o0.xyz = dstColor;
	}
	else
	{
		o0.xyz = saturate(dstColor);
	}
	o0.w = 1;
	return;
}