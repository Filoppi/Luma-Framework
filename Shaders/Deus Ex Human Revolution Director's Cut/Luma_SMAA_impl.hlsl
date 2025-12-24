// SMAA implementation
// See Demo for the reference https://github.com/iryoku/smaa

#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer SceneBuffer : register(b2)
{
    row_major float4x4 View : packoffset(c0);
    row_major float4x4 ScreenMatrix : packoffset(c4);
    float2 DepthExportScale : packoffset(c8);
    float2 FogScaleOffset : packoffset(c9);
    float3 CameraPosition : packoffset(c10);
    float3 CameraDirection : packoffset(c11);
    float3 DepthFactors : packoffset(c12);
    float2 ShadowDepthBias : packoffset(c13);
    float4 SubframeViewport : packoffset(c14);
    row_major float3x4 DepthToWorld : packoffset(c15);
    float4 DepthToView : packoffset(c18);
    float4 OneOverDepthToView : packoffset(c19);
    float4 DepthToW : packoffset(c20);
    float4 ClipPlane : packoffset(c21);
    float2 ViewportDepthScaleOffset : packoffset(c22);
    float2 ColorDOFDepthScaleOffset : packoffset(c23);
    float2 TimeVector : packoffset(c24);
    float3 HeightFogParams : packoffset(c25);
    float3 GlobalAmbient : packoffset(c26);
    float4 GlobalParams[16] : packoffset(c27);
    float DX3_SSAOScale : packoffset(c43);
    float4 ScreenExtents : packoffset(c44); // pixel size, offset
    float2 ScreenResolution : packoffset(c45);
    float4 PSSMToMap1Lin : packoffset(c46);
    float4 PSSMToMap1Const : packoffset(c47);
    float4 PSSMToMap2Lin : packoffset(c48);
    float4 PSSMToMap2Const : packoffset(c49);
    float4 PSSMToMap3Lin : packoffset(c50);
    float4 PSSMToMap3Const : packoffset(c51);
    float4 PSSMDistances : packoffset(c52);
    row_major float4x4 WorldToPSSM0 : packoffset(c53);
    float StereoOffset : packoffset(c25.w);
}

#define SMAA_RT_METRICS float4(1.0 / ScreenResolution, ScreenResolution) // Should use ScreenExtents here?
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION 1
#define SMAA_CUSTOM_SL
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAATexture2DMS2(tex) Texture2DMS<float4, 2> tex
#define SMAALoad(tex, pos, sample) tex.Load(pos, sample)
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
#include "../Includes/SMAA.hlsl"

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);

void fullscreen_triangle(uint id, out float4 position, out float2 texcoord)
{
    texcoord = float2((id << 1) & 2, id & 2);
    position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// SMAAEdgeDetection
//

void smaa_edge_detection_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float4 offset[3] : TEXCOORD1)
{
    fullscreen_triangle(id, position, texcoord);
    SMAAEdgeDetectionVS(texcoord, offset);
}

float2 smaa_edge_detection_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float4 offset[3] : TEXCOORD1) : SV_Target
{
    // tex0 = colorTexGamma
    // tex1 = predicationTex
    return SMAAColorEdgeDetectionPS(texcoord, offset, tex0, tex1);
}

//

// SMAABlendingWeightCalculation
//

void smaa_blending_weight_calculation_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float2 pixcoord : TEXCOORD1, out float4 offset[3] : TEXCOORD2)
{
    fullscreen_triangle(id, position, texcoord);
    SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
}

float4 smaa_blending_weight_calculation_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float2 pixcoord : TEXCOORD1, float4 offset[3] : TEXCOORD2) : SV_Target
{
    // tex0 = edgesTex
    // tex1 = areaTex
    // tex2 = searchTex
    return SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, tex0, tex1, tex2, 0);
}

//

// SMAANeighborhoodBlending
//

void smaa_neighborhood_blending_vs(uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD0, out float4 offset : TEXCOORD1)
{
    fullscreen_triangle(id, position, texcoord);
    SMAANeighborhoodBlendingVS(texcoord, offset);
}

float4 smaa_neighborhood_blending_ps(float4 position : SV_Position, float2 texcoord : TEXCOORD0, float4 offset : TEXCOORD1) : SV_Target
{
    // tex0 = colorTex
    // tex1 = blendTex
    float4 color = SMAANeighborhoodBlendingPS(texcoord, offset, tex0, tex1);

    float2 uv = position.xy * ScreenExtents.zw + ScreenExtents.xy;
    bool forceSDR = ShouldForceSDR(uv);
    if (!LumaSettings.GameSettings.HasColorGradingPass && !forceSDR) // Luma
    {
        const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
        const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
        bool tonemapPerChannel = LumaSettings.DisplayMode != 1;
#if ENABLE_HIGHLIGHTS_DESATURATION_TYPE == 1 || ENABLE_HIGHLIGHTS_DESATURATION_TYPE >= 3
        tonemapPerChannel = true;
#endif
        if (LumaSettings.DisplayMode == 1)
        {
            DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
            color.rgb = DICETonemap(color.rgb * paperWhite, peakWhite, settings) / paperWhite;
        }
        else
        {
            if (tonemapPerChannel)
            {
                color.rgb = Reinhard::ReinhardRange(color.rgb, MidGray, -1.0, peakWhite / paperWhite, false);
            }
            else
            {
                color.rgb = RestoreLuminance(color.rgb, Reinhard::ReinhardRange(GetLuminance(color.rgb), MidGray, -1.0, peakWhite / paperWhite, false).x, true);
                color.rgb = CorrectOutOfRangeColor(color.rgb, true, true, 0.5, 0.5, peakWhite / paperWhite);
            }
        }

#if UI_DRAW_TYPE == 2
        ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
        color.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
        ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif // UI_DRAW_TYPE == 2

        color.rgb = linear_to_gamma(color.rgb, GCT_MIRROR);
    }
    else
    {
        color.rgb = linear_to_sRGB_gamma(color.rgb);
    }

    return color;
}

//