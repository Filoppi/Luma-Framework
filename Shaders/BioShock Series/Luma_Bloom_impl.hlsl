#if GAME_BIOSHOCK == 1 // BioShock Remastered
    cbuffer _Globals : register(b0)
    {
        float ImageSpace_hlsl_BlurPixelShader048_6bits : packoffset(c0) = {0};
        float4 fogColor : packoffset(c1);
        float3 fogTransform : packoffset(c2);
        float4x3 screenDataToCamera : packoffset(c3);
        float globalScale : packoffset(c6);
        float sceneDepthAlphaMask : packoffset(c6.y);
        float globalOpacity : packoffset(c6.z);
        float distortionBufferScale : packoffset(c6.w);
        float2 wToZScaleAndBias : packoffset(c7);
        float4 screenTransform[2] : packoffset(c8);
        float4 textureToPixel : packoffset(c10);
        float4 pixelToTexture : packoffset(c11);
        float maxScale : packoffset(c12) = {0};
        float bloomAlpha : packoffset(c12.y) = {0};
        float sceneBias : packoffset(c12.z) = {1};
        float exposure : packoffset(c12.w) = {0};
        float deltaExposure : packoffset(c13) = {0};
        float4 SampleOffsets[8] : packoffset(c14);
        float4 SampleWeights[16] : packoffset(c22);
        float4 PWLConstants : packoffset(c38);
        float PWLThreshold : packoffset(c39);
        float ShadowEdgeDetectThreshold : packoffset(c39.y);
        float4 ColorFill : packoffset(c40);
    }
    #define LUMA_BLOOM_THRESHOLD PWLThreshold
    #define LUMA_BLOOM_SOFT_KNEE LUMA_BLOOM_THRESHOLD
    #define LUMA_BLOOM_TINT float3(1.0, 1.3, 1.0)
#elif GAME_BIOSHOCK == 2 // BioShock 2 Remastered
    cbuffer _Globals : register(b0)
    {
        float ImageSpace_hlsl_BlurPixelShader00000000000000000000000000000124_9bits : packoffset(c0) = {0};
        float4 fogColor : packoffset(c1);
        float3 fogTransform : packoffset(c2);
        float2 fogLuminance : packoffset(c3);
        row_major float3x4 screenDataToCamera : packoffset(c4);
        float globalScale : packoffset(c7);
        float sceneDepthAlphaMask : packoffset(c7.y);
        float globalOpacity : packoffset(c7.z);
        float distortionBufferScale : packoffset(c7.w);
        float3 wToZScaleAndBias : packoffset(c8);
        float4 screenTransform[2] : packoffset(c9);
        float4 textureToPixel : packoffset(c11);
        float4 pixelToTexture : packoffset(c12);
        float maxScale : packoffset(c13) = {0};
        float bloomAlpha : packoffset(c13.y) = {0};
        float sceneBias : packoffset(c13.z) = {1};
        float3 gammaSettings : packoffset(c14);
        float exposure : packoffset(c14.w) = {0};
        float deltaExposure : packoffset(c15) = {0};
        float4 SampleOffsets[2] : packoffset(c16);
        float4 SampleWeights[4] : packoffset(c18);
        float4 PWLConstants : packoffset(c22);
        float PWLThreshold : packoffset(c23);
        float ShadowEdgeDetectThreshold : packoffset(c23.y);
        float4 ColorFill : packoffset(c24);
        float2 LowResTextureDimensions : packoffset(c25);
        float2 DownsizeTextureDimensions : packoffset(c25.z);
    }
    #define LUMA_BLOOM_THRESHOLD PWLThreshold
    #define LUMA_BLOOM_SOFT_KNEE LUMA_BLOOM_THRESHOLD
#elif GAME_BIOSHOCK == 3 // BioShock Infinite
    cbuffer _Globals : register(b0)
    {
        float4 SceneShadowsAndDesaturation : packoffset(c0);
        float4 SceneInverseHighLights : packoffset(c1);
        float4 SceneMidTones : packoffset(c2);
        float4 SceneScaledLuminanceWeights : packoffset(c3);
        float4 GammaColorScaleAndInverse : packoffset(c4);
        float4 GammaOverlayColor : packoffset(c5);
        float4 RenderTargetExtent : packoffset(c6);
        float2 DownsampledDepthScale : packoffset(c7);
        float2 BloomScaleAndThreshold : packoffset(c7.z);
        float4 PackedParameters : packoffset(c8);
        float4 PackedParameters2 : packoffset(c9);
        float4 MinMaxBlurClamp : packoffset(c10);
        float4x4 CameraDelta : packoffset(c11);
        float4 MotionBlurSettings : packoffset(c15);
    }
    #define LUMA_BLOOM_THRESHOLD (BloomScaleAndThreshold.y * rcp(max(BloomScaleAndThreshold.x, 1e-6)))
    #define LUMA_BLOOM_SOFT_KNEE LUMA_BLOOM_THRESHOLD
#endif
#include "../Includes/Bloom.hlsl"