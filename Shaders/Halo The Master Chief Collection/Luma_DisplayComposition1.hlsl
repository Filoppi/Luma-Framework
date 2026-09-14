#include "./Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl" // Use this as it has some gamma correction helpers
#include "../Includes/Reinhard.hlsl"

Texture2D<float4> sourceTexture : register(t0);
Texture2D<float4> uiTexture : register(t1); // Optional: Pre-multiplied UI
#if DEVELOPMENT
Texture2D<float4> debugTexture2D : register(t2);
Texture2DMS<float4> debugTexture2DMS : register(t3);
Texture2DArray<float4> debugTexture2DArray : register(t4);
Texture2DMSArray<float4> debugTexture2DMSArray : register(t5);
Texture3D<float4> debugTexture3D : register(t6);
Texture1D<float4> debugTexture1D : register(t7);
Texture1DArray<float4> debugTexture1DArray : register(t8);
// TODO: add TextureCube(s) support... Update "debug_draw_srv_slot_numbers" in c++ if you change this. And also for uint/sint textures.
#endif

// Note: this might not always be set, only use in known cases
SamplerState linearSampler : register(s0);

#if DEVELOPMENT
bool DrawDebugTexture(float3 pos, inout float4 outColor, float gamePaperWhite, float UIPaperWhite)
{
	bool _texture2D = (LumaData.CustomData2 & (1 << 13)) != 0; // "_" as the name is seemengly taken in hlsl
	bool _texture3D = (LumaData.CustomData2 & (1 << 14)) != 0;
	bool _texture1D = !_texture2D && !_texture3D; // We don't have a flag for "no texture", we simply unbind all of them
	bool _textureCube = _texture2D && _texture3D;
	bool _textureMS = (LumaData.CustomData2 & (1 << 11)) != 0;
	bool _textureArray = (LumaData.CustomData2 & (1 << 12)) != 0;
	float debugWidth = 0.0, debugHeightOrArraySize = 1.0, debugDepthOrArraySize = 1.0, sampleCount = 1.0; // Default to 0 to make sure they stay zero if the texture wasn't bound
	if (_texture1D && _textureArray)
	{
		debugTexture1DArray.GetDimensions(debugWidth, debugHeightOrArraySize);
	}
	else if (_texture1D)
	{
		debugTexture1D.GetDimensions(debugWidth);
	}
	else if (_texture3D)
	{
		debugTexture3D.GetDimensions(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize);
	}
	else if (_textureMS && _textureArray)
	{
		debugTexture2DMSArray.GetDimensions(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize, sampleCount);
	}
	else if (_textureMS)
	{
		debugTexture2DMS.GetDimensions(debugWidth, debugHeightOrArraySize, sampleCount);
	}
	else if (_textureArray)
	{
		debugTexture2DArray.GetDimensions(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize);
	}
	else // _texture2D
	{
		debugTexture2D.GetDimensions(debugWidth, debugHeightOrArraySize);
	}
	// TODO: add better support for depth stencil formats? uint?
	// Skip if there's no texture. It might be undefined behaviour, but it seems to work on Nvidia
	if (debugWidth != 0.0)
	{
		float2 resolutionScale = 1.0;
		bool fullscreen = (LumaData.CustomData2 & (1 << 0)) != 0;
		bool renderResolutionScale = (LumaData.CustomData2 & (1 << 1)) != 0;
		bool showAlpha = (LumaData.CustomData2 & (1 << 2)) != 0;
		bool premultiplyAlpha = (LumaData.CustomData2 & (1 << 3)) != 0;
		bool invertColors = (LumaData.CustomData2 & (1 << 4)) != 0;
		bool linearToGamma = (LumaData.CustomData2 & (1 << 5)) != 0;
		bool gammaToLinear = (LumaData.CustomData2 & (1 << 6)) != 0;
		bool flipY = (LumaData.CustomData2 & (1 << 7)) != 0;
		bool doAbs = (LumaData.CustomData2 & (1 << 15)) != 0;
		bool doSaturate = (LumaData.CustomData2 & (1 << 8)) != 0;
		bool redOnly = (LumaData.CustomData2 & (1 << 9)) != 0;
		bool backgroundPassthrough = (LumaData.CustomData2 & (1 << 10)) != 0;
		bool zoom4x = (LumaData.CustomData2 & (1 << 16)) != 0;
		bool bilinear = (LumaData.CustomData2 & (1 << 17)) != 0;
		bool tonemap = (LumaData.CustomData2 & (1 << 18)) != 0;
		bool sRGB = (LumaData.CustomData2 & (1 << 19)) != 0; // sRGB in/out (instead of gamma 2.2)
		bool UVToPixelSpace = (LumaData.CustomData2 & (1 << 20)) != 0;
		bool denormalize = (LumaData.CustomData2 & (1 << 21)) != 0;
		int mipLevel = LumaData.CustomData3 + 0.5; // 10 bits for the mip level
	
		float3 debugPos = float3(pos.xy, 0.5);
		int sliceWidth = debugWidth + 0.5;
		debugPos.z = (uint(debugPos.x) / sliceWidth) + 0.5; // Basically: "depthOrArrayIndex"

		float targetWidth;
		float targetHeight;
		sourceTexture.GetDimensions(targetWidth, targetHeight);

		bool size2DMatches = debugWidth == targetWidth && debugHeightOrArraySize == targetHeight;
		fullscreen = fullscreen || size2DMatches;
		
		if (fullscreen) // Stretch to fullscreen
		{
			resolutionScale = float2(debugWidth / targetWidth, debugHeightOrArraySize / targetHeight);
			if (flipY)
			{
				debugPos.y = targetHeight - debugPos.y;
			}
		}
		else
		{
			if (debugDepthOrArraySize != 1) // TODO: what is this for exactly? Some array textures?
			{
				debugPos.x = (uint(debugPos.x) % sliceWidth) + 0.5;
			}
			// TODO: handle if this works with "renderResolutionScale" (e.g. Prey)
			if (flipY)
			{
				debugPos.y = debugHeightOrArraySize - debugPos.y;
			}
		}
		if (renderResolutionScale) // Scale by rendering resolution (so to stretch the used part of the image to the full texture range) (note that this might not work so well if the game draws at a different aspect ratio and then adds black bars)
		{
			resolutionScale *= LumaData.RenderResolutionScale;
		}
		
		// Zoom around the center		
		float zoom = zoom4x ? 4.0 : 1.0;
		float3 debugSize = float3(debugWidth, debugHeightOrArraySize, debugDepthOrArraySize); // I don't think we need -1 on this
		float3 center = fullscreen ? 0 : (-debugSize * 0.5); // Zoom around the center if non fullscreen mode // TODO: polish... not right
		debugPos.xyz = ((debugPos.xyz - center) / zoom) + center;
		// TODO: add a brightness scale to convert from UV space to pixel space (e.g. motion vectors)

		// TODO: should we also apply the "resolutionScale" around the center?
		int3 debugPosInt;
		debugPosInt.xy = round((debugPos.xy - 0.5) * resolutionScale) + 0.5;
		debugPosInt.z = debugPos.z * resolutionScale.x;

		debugPos.xy *= resolutionScale;
		debugPos.z *= resolutionScale.x;
	
		bool validTexel = debugPos.x < debugSize.x && debugPos.y < debugSize.y && debugPos.z < debugSize.z && debugPos.x >= 0.0 && debugPos.y >= 0.0 && debugPos.z >= 0.0;
		float4 color = 0.0;
		int sampleIndex = 0; // Expose for manual analysis if necessary
		if (_texture1D && _textureArray)
		{
			color = debugTexture1DArray.Load(int3(debugPosInt.x, debugPosInt.y, mipLevel)); // The array elements are spread vertically
		}
		else if (_texture1D)
		{
			color = debugTexture1D.Load(int2(debugPosInt.x, mipLevel));
		}
		else if (_texture3D)
		{
			color = debugTexture3D.Load(int4(debugPosInt.x, debugPosInt.y, debugPosInt.z, mipLevel)); // The array elements are spread horizontally
		}
		// All "_texture2D" from here
		else if (_textureMS && _textureArray)
		{
			for (; sampleIndex < sampleCount; sampleIndex++)
			{
				color += debugTexture2DMSArray.Load(int3(debugPosInt.x, debugPosInt.y, debugPosInt.z), sampleIndex); // The array elements are spread horizontally
			}
		}
		else if (_textureMS)
		{
			for (; sampleIndex < sampleCount; sampleIndex++) // Take all MS samples, to get a better overview of the texture
			{
				color += debugTexture2DMS.Load(int2(debugPosInt.xy), sampleIndex); // Ideally we'd average them in linear space, but whatever
			}
		}
		else if (_textureArray)
		{
			color = debugTexture2DArray.Load(int4(debugPosInt.x, debugPosInt.y, debugPosInt.z, mipLevel)); // The array elements are spread horizontally
		}
		else
		{
			if (bilinear) // TODO: implement for other texture types
				color = debugTexture2D.SampleLevel(linearSampler, debugPos.xy / debugSize.xy, mipLevel);
			else
				color = debugTexture2D.Load(int3(debugPosInt.x, debugPosInt.y, mipLevel)); // Approximate to the closest texel (sharp!)
		}
		color /= sampleCount;

		// Samples on invalid coordinates should already return 0, but we force it anyway
		if (!validTexel)
		{
       		color.rgb = 0;
		}

		if (denormalize)
		{
			color.rgb = (color.rgb - 0.5) * 2.0; // Note: 0.5 wouldn't have been the exact center in UNORM textures, but it should do (otherwise we'd need to branch based on the format precision)
		}
		// Do it early so it also fixes nans
		if (doAbs)
		{
			color = abs(color);
		}
		if (doSaturate)
		{
			color = saturate(color);
		}
		if (redOnly)
		{
			color.rgb = color.r;
		}
		if (showAlpha)
		{
			color.rgb = color.a;
		}
		if (premultiplyAlpha)
		{
			color.rgb *= color.a;
		}
		if (UVToPixelSpace)
		{
			color.rgb *= debugSize;
		}
		if (invertColors) // Only works on in SDR range
		{
			color.rgb = 1.0 - color.rgb;
		}
		if (gammaToLinear) // Linearize (output expects linear) (use if image appeared too bright (gamma space viewed as linear))
		{
        	color.rgb = sRGB ? gamma_sRGB_to_linear(color.rgb, GCT_MIRROR) : (pow(abs(color.rgb), DefaultGamma) * Sign_Fast(color.rgb));
		}
		if (linearToGamma) // Gammify (usually not necessary) (use if image appeared too dark (linear space viewed as gamma))
		{
       		color.rgb = sRGB ? linear_to_sRGB_gamma(color.rgb, GCT_MIRROR) : (pow(abs(color.rgb), 1.f / DefaultGamma) * Sign_Fast(color.rgb));
		}
		if (tonemap && !showAlpha)
		{
    		const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
#if 1 // No hue shifts, better analysis
    		color.rgb = RestoreLuminance(color.rgb, Reinhard::ReinhardRange(GetLuminance(color.rgb), MidGray, -1.0, peakWhite / gamePaperWhite).x, true);
    		color.rgb = CorrectOutOfRangeColor(color.rgb, true, true, 0.5, peakWhite / gamePaperWhite);
#else
  			color.rgb = Reinhard::ReinhardRange(color.rgb, MidGray, -1.0, peakWhite / gamePaperWhite);
#endif
		}
		if (validTexel || !backgroundPassthrough)
		{
			outColor = color * gamePaperWhite; // Scale by user paper white brightness just to make it more visible
			return true;
		}
    }
	return false;
}
#endif

float3 ComposeUI(float3 pos, float3 linearSceneColor, float gamePaperWhite, float UIPaperWhite)
{
#if 0
	float3 sceneColorGamma = linear_to_game_gamma(linearSceneColor.rgb, GCT_MIRROR);
	float3 UIRelativeColor = linearSceneColor.rgb * (gamePaperWhite / UIPaperWhite);
    float3 sceneColorGammaTonemapped = linear_to_game_gamma((UIRelativeColor / (UIRelativeColor + 1.f)) / (gamePaperWhite / UIPaperWhite), GCT_MIRROR); // Tonemap the UI background based on the UI intensity to avoid bright backgrounds (e.g. sun) burning through the UI
	float3 UIInverseInfluence = 1.0;
	float4 UIColor = uiTexture.Load((int3)pos.xyz);
    float UIIntensity = saturate(UIColor.a);
	sceneColorGamma *= pow(gamePaperWhite, 1.0 / DefaultGamma);
	sceneColorGammaTonemapped *= pow(gamePaperWhite, 1.0 / DefaultGamma);
	UIColor.rgb *= pow(UIPaperWhite, 1.0 / DefaultGamma);
	// Darken the scene background based on the UI intensity
	float3 composedColor = lerp(sceneColorGamma, sceneColorGammaTonemapped, UIIntensity) * (1.0 - UIIntensity);
    // Calculate how much the additive UI influenced the darkened scene color, so we can determine the intensity to blend the composed color with the scene paper white (it's better to calculate this in gamma space)
	UIInverseInfluence = safeDivision(composedColor, composedColor + UIColor.rgb, 1); //TODO: handle negative colors?
	// Add pre-multiplied UI
	composedColor += UIColor.rgb;
	
	float3 compositionPaperWhite = lerp(pow(UIPaperWhite, 1.0 / DefaultGamma), pow(gamePaperWhite, 1.0 / DefaultGamma), UIInverseInfluence);
	composedColor /= compositionPaperWhite;

  	float3 linearComposedColor = game_gamma_to_linear(composedColor, GCT_MIRROR) * pow(compositionPaperWhite, DefaultGamma); // Note: this won't scale the paper white 100% correctly, slightly shifting colors
#else
	linearSceneColor.rgb /= UIPaperWhite / gamePaperWhite;
	//linearSceneColor.rgb /= UIPaperWhite;
	float3 sceneColorGamma = linear_to_game_gamma(linearSceneColor.rgb, GCT_MIRROR);
    float3 sceneColorGammaTonemapped = linear_to_game_gamma(linearSceneColor.rgb / (linearSceneColor.rgb + 1.f), GCT_MIRROR); // Tonemap the UI background based on the UI intensity to avoid bright backgrounds (e.g. sun) burning through the UI
	float3 UIInverseInfluence = 1.0;
	float4 UIColor = uiTexture.Load((int3)pos.xyz);
    float UIIntensity = saturate(UIColor.a);
	// Darken the scene background based on the UI intensity
	float3 composedColor = lerp(sceneColorGamma, sceneColorGammaTonemapped, UIIntensity) * (1.0 - UIIntensity);
	composedColor = sceneColorGamma * (1.0 - UIIntensity); // Disable TM for now
    // Calculate how much the additive UI influenced the darkened scene color, so we can determine the intensity to blend the composed color with the scene paper white (it's better to calculate this in gamma space)
	UIInverseInfluence = safeDivision(composedColor, composedColor + UIColor.rgb, 1);
	// Add pre-multiplied UI
	composedColor += UIColor.rgb;
	float3 linearComposedColor;
  	linearComposedColor.rgb = game_gamma_to_linear(composedColor, GCT_MIRROR);
	linearComposedColor.rgb *= UIPaperWhite / gamePaperWhite;
	//linearComposedColor.rgb *= UIPaperWhite;
	//linearComposedColor.rgb *= lerp(1.0, gamePaperWhite, saturate(UIInverseInfluence));
#endif
	return linearComposedColor;
}

// rect is top left (x,y), bottom right (x,y)
float3 DrawRect(float2 uv, float4 rect, float3 color, float3 rectColor) {
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
  return r == 0 ? color : rectColor;
}

// Custom Luma shader to apply the display (or output) transfer function from a linear input (or apply custom gamma correction)
float4 main(float4 pos : SV_Position) : SV_Target0
{
	// Game scene paper white and Generic paper white for when we can't account for the UI paper white.
	// If "POST_PROCESS_SPACE_TYPE" or "EARLY_DISPLAY_ENCODING" are 1, this might have already been applied in.
	// This essentially means that the SDR range we receive at this point is 0-1 in the buffers, with 1 matching "sRGB_WhiteLevelNits" as opposued to "ITU_WhiteLevelNits".
    float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    float UIPaperWhite = LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;

#if DEVELOPMENT
	float4 debugTextureColor = 0.0;
	if (DrawDebugTexture(pos.xyz, debugTextureColor, gamePaperWhite, UIPaperWhite))
	{
		return debugTextureColor;
	}
#endif

	float sourceWidth;
	float sourceHeight;
	sourceTexture.GetDimensions(sourceWidth, sourceHeight);

#if TEST_2X_ZOOM // TEST: zoom into the image to analyze it
	const float scale = 2.0;
	pos.xy = pos.xy / scale + float2(sourceWidth, sourceHeight) / (scale * 2.0);
#endif

	float2 uv = pos.xy / float2(sourceWidth, sourceHeight);

#if 0 // TEST: draw gradient
	return float4(uv.x, uv.x, uv.x, 1.0) * 2;
#endif

	float targetHeight1 = 1080.0 * DVS4;
	float targetHeight2 = targetHeight1 * DVS5 * 0.5;
	float mipLevel1 = log2(sourceHeight / targetHeight1);
	float mipLevel2 = log2(sourceHeight / targetHeight2);
	float4 color = sourceTexture.Load((int3)pos.xyz);
	float4 mipColor1 = 0.0;
	float4 mipColor2 = 0.0;
#if 0
	mipColor1 = sourceTexture.SampleLevel(linearSampler, uv, mipLevel1);
	mipColor2 = sourceTexture.SampleLevel(linearSampler, uv, mipLevel2);
	int offset = 5;
	mipColor1 = sourceTexture.Load((int3)pos.xyz + int3( offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset,  offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3( offset,  offset, 0)) +
				color;
	mipColor1 /= 5.0;
	offset = 30;
	mipColor2 = sourceTexture.Load((int3)pos.xyz + int3( offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset,  offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3(-offset, -offset, 0)) +
				sourceTexture.Load((int3)pos.xyz + int3( offset,  offset, 0)) +
				color;
	mipColor2 /= 5.0;
#endif
	//return mipColor2; //TODOFT

	// This case means the game currently doesn't have Luma custom shaders built in (fallback in case of problems), or has manually unloaded them, so the value of some macro defines do not matter
	const bool modActive = LumaData.CustomData1 == 0;
	if (!modActive)
	{
		// SDR was already linear, assuming we are outputting on scRGB HDR buffers (usually implies "POST_PROCESS_SPACE_TYPE" is 1)
		const bool vanillaSwapchainWasLinear = LumaData.CustomData1 >= 2;
		// "VANILLA_ENCODING_TYPE" is expected to be 0 for this branch
		if (vanillaSwapchainWasLinear)
		{
			// SDR (on SDR)
			if (LumaSettings.DisplayMode <= 0)
			{
				// Nothing to do, the game would have encoded with sRGB, the display will decode sRGB with gamma 2.2 as it would have in vanilla SDR, handling gamma correction for us.
				// "GAMMA_CORRECTION_TYPE" is not implemented here as it's not a common case that we'd wanna handle, and we want to keep SDR looking like SDR always did.
			}
			// HDR
			else
			{
				//TODOFT: wrap in a func, given it's duplicate below too? One of the ColorGradingLUTTransferFunctionIn...
				float3 colorGammaCorrectedByChannel = gamma_to_linear(linear_to_sRGB_gamma(color.rgb, GCT_MIRROR), GCT_MIRROR);
				float luminanceGammaCorrected = gamma_to_linear(linear_to_sRGB_gamma(GetLuminance(color.rgb), GCT_POSITIVE).x, GCT_POSITIVE).x;
				float3 colorGammaCorrectedByLuminance = RestoreLuminance(color.rgb, luminanceGammaCorrected);
#if GAMMA_CORRECTION_TYPE == 1
				color.rgb = colorGammaCorrectedByChannel;
#elif GAMMA_CORRECTION_TYPE == 2
  				color.rgb = RestoreLuminance(color.rgb, colorGammaCorrectedByChannel);
				float3 colorGammaCorrectedByChannelMip1 = gamma_to_linear(linear_to_sRGB_gamma(mipColor1.rgb, GCT_MIRROR), GCT_MIRROR);
				float3 colorGammaCorrectedByChannelMip2 = gamma_to_linear(linear_to_sRGB_gamma(mipColor2.rgb, GCT_MIRROR), GCT_MIRROR);
  				mipColor1.rgb = RestoreLuminance(mipColor1.rgb, colorGammaCorrectedByChannelMip1);
  				mipColor2.rgb = RestoreLuminance(mipColor2.rgb, colorGammaCorrectedByChannelMip2);
#elif GAMMA_CORRECTION_TYPE == 3 //TODOFT: probably doesn't look good? It'd treat green and blue massively different
  				color.rgb = colorGammaCorrectedByLuminance;
#elif GAMMA_CORRECTION_TYPE >= 4
  				color.rgb = RestoreHueAndChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel, 0.0, 1.0);
#endif // GAMMA_CORRECTION_TYPE == 1
			}
		}
		// SDR was gamma space, but now we are outputting on scRGB HDR buffers
		else
		{
			// SDR (on SDR)
			if (LumaSettings.DisplayMode <= 0)
			{
				// The SDR display will (usually) linearize with gamma 2.2, hence applying the usual gamma mismatch, so we don't correct gamma here
				color.rgb = gamma_sRGB_to_linear(color.rgb, GCT_NONE);
			}
			// HDR (we assume this is the default case for Luma users/devs, this isn't an officially supported case anyway) (we ignore "GAMMA_CORRECTION_RANGE_TYPE" and "VANILLA_ENCODING_TYPE" here, it doesn't matter)
			else
			{
				color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, GAMMA_CORRECTION_TYPE);

				mipColor1.rgb = ColorGradingLUTTransferFunctionOut(mipColor1.rgb, GAMMA_CORRECTION_TYPE);
				mipColor2.rgb = ColorGradingLUTTransferFunctionOut(mipColor2.rgb, GAMMA_CORRECTION_TYPE);
			}
		}
		
#if 0 // AntiBloom AutoHDR
		// TODO: add an "unclipping" mode that tries to detect the channel value before it clipped to 1 by querying smaller mips until one isn't 1, and then tries to increase the value in the center of the clipped highlight blob. Also try "CorrectPerChannelTonemapDesaturation" to resaturate per channel tonemaped highlights.
		float shoulderPow = 2.75f; // Default value, can be changed in the settings
		float maxShoulderPow = lerp(shoulderPow, 1.f, LumaSettings.DevSetting01); // Default value, can be changed in the settings
		float mipRatio1 = GetLuminance(mipColor1.rgb) / GetLuminance(color.rgb);
		float mipRatio2 = GetLuminance(mipColor2.rgb) / GetLuminance(color.rgb);
		float peak = 500.f;
		float maxPeak = lerp(peak, 800.f, LumaSettings.DevSetting02);
    	if (LumaSettings.DevSetting03 <= 0.0)
		{
			shoulderPow = lerp(shoulderPow, maxShoulderPow, saturate(mipRatio1 * mipRatio2 * GetLuminance(color.rgb)));
			peak = lerp(peak, maxPeak, saturate(mipRatio1 * mipRatio2 * GetLuminance(color.rgb)));
		}
		if (LumaSettings.DevSetting03 > 0.75)
		{
			color.rgb = mipColor2.rgb;
		}
		else if (LumaSettings.DevSetting03 > 0.5)
		{
			color.rgb = mipColor1.rgb;
		}
		else if (LumaSettings.DevSetting03 > 0.25)
		{
		}
		else
		{
			color.rgb = PumboAutoHDR(color.rgb, peak, LumaSettings.GamePaperWhiteNits, shoulderPow); // This won't multiply the paper white in, it just uses it as a modifier for the AutoHDR logic
		}
		if (LumaSettings.DevSetting03 <= 0.0)
		{
			color.rgb = Saturation(color.rgb, (saturate(mipRatio1 * mipRatio2 * GetLuminance(color.rgb))* 0.333 + 1.0));
		}
#endif

#if DEVELOPMENT // Optionally clamp SDR and SDR on HDR modes (dev only)
		if (LumaSettings.DisplayMode != 1)
			color.rgb = saturate(color.rgb);
#endif
		color.rgb *= gamePaperWhite;
		return float4(color.rgb, color.a);
	}

	// Halo3 Indirect Upgrade gamma mismatch
	if ((IsGame_Halo3() || IsGame_Halo3ODST()) && GS.UIBlurDown0Count == 0) color.rgb = linear_to_sRGB_gamma(color.rgb);

	float postLinearizationScale = 1.0;
	
//TODOFT: split this and other code behaviours into functions
#if UI_DRAW_TYPE == 2 // The scene color was scaled by "scene paper white / UI paper white" (in linear space) to make the UI blend in correctly in gamma space without modifying its shaders. sRGB/2.2 gamma mismatch correction would have already applied temporarily at native SDR range on the pre-UI scene color, before scaling it by the UI factors, to then be undone. So we need to re-apply the gamma mismatch fix on the UI SDR range color (and the rest of the scene, for which the fix was already undone at that scaling level, and thus will normalize itself out).

  	postLinearizationScale *= UIPaperWhite;
#if !EARLY_DISPLAY_ENCODING
  	postLinearizationScale /= gamePaperWhite;
#endif

	// if ((IsGame_Halo1Classic() || IsGame_Halo1Anniversary()) && GS.UIBlurDown0Count == 0) postLinearizationScale = 1;

#elif UI_DRAW_TYPE == 3 // Compose UI on top of "scene" and tonemap the scene background //TODOFT6: clean up all the defines that we don't need anymore

	// These should imply the scene is rendering (the UI might still be all zero)
	if (LumaData.CustomData4 != 0.f)
	{
		color.rgb = ComposeUI(pos.xyz, color.rgb, gamePaperWhite, UIPaperWhite);
		
		//color.rgb = game_gamma_to_linear(uiTexture.Load((int3)pos.xyz, GCT_MIRROR).rgb);
		//color.rgb = uiTexture.Load((int3)pos.xyz, GCT_MIRROR).a;
	}
	// There's no scene rendering, which imply the image is all UI, so scale the overall brightness with the UI brightness parameter instead of the game scene one
	else
	{
		//color.rgb = game_gamma_to_linear(color.rgb, GCT_MIRROR);
		gamePaperWhite = UIPaperWhite;
	}

#endif // UI_DRAW_TYPE != 0

	// SDR: In this case, paper white (game and UI) would have been 1 (neutral for SDR), so we can ignore it if we want to
	if (LumaSettings.DisplayMode <= 0)
	{
		color.rgb = saturate(color.rgb); // Optional, but saves performance on the gamma pows below (the vanilla SDR tonemapper might have retained some values beyond 1 so we want to clip them anyway, for a "reliable" SDR look)

#if POST_PROCESS_SPACE_TYPE == 1
		// Revert whatever gamma adjustment "GAMMA_CORRECTION_TYPE" would have made, and get the color is sRGB gamma encoding (which would have been meant for 2.2 displays)
		// This function does more stuff than we need (like handling colors beyond the 0-1 range, which we've clamped out above), but we use it anyway for simplicity
		ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, (EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE < 2) ? GAMMA_CORRECTION_TYPE : VANILLA_ENCODING_TYPE, LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB, true);
		// No need for "postLinearizationScale", it should always be 1 in SDR.
#else // POST_PROCESS_SPACE_TYPE != 1
		// In SDR, we ignore "GAMMA_CORRECTION_TYPE" as they are not that relevant
		// We are target the gamma 2.2 look here, which would likely match the average SDR screen, so
		// we linearize with sRGB because scRGB HDR buffers (Luma) in SDR are re-encoded with sRGB and then (likely) linearized by the display with 2.2, which would then apply the gamma correction.
		// For any user that wanted to play in sRGB, they'd need to have an sRGB monitor.
		// We could theoretically add a mode that fakes sRGB output on scRGB->2.2 but it wouldn't really be useful as the game was likely designed for 2.2 displays (unconsciously).
		color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB, false);
#endif // POST_PROCESS_SPACE_TYPE == 1

#if 0 // For linux support (somehow scRGB is not interpreted as linear when in SDR) //TODOFT4: expose?
		color.rgb = linear_to_sRGB_gamma(color.rgb, GCT_NONE);
#endif
	}
	// HDR and SDR in HDR: in this case the UI paper white would have already been mutliplied in, relatively to the game paper white, so we only apply the game paper white.
	else if (LumaSettings.DisplayMode >= 1)
	{
#if POST_PROCESS_SPACE_TYPE != 1 // Gamma->Linear space

		// At this point, in this case, the color would have been gamma space (sRGB or 2.2, depending on the game), normalized around SDR range (80 nits paper white).
		// The gamma correction both acts as correction but also as "absolute" gamma curve selection (emulating either an sRGB or Gamma 2.2 display).

#if GAMMA_CORRECTION_RANGE_TYPE == 1 // Apply gamma correction only in the 0-1 range

		color.rgb = ColorGradingLUTTransferFunctionOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE);
		
#else // GAMMA_CORRECTION_RANGE_TYPE != 0 // Apply gamma correction around the whole range (alternative branch) (this doesn't acknowledge "VANILLA_ENCODING_TYPE", it doesn't need to)

		color.rgb = ColorGradingLUTTransferFunctionOut(color.rgb, GAMMA_CORRECTION_TYPE);

#endif // GAMMA_CORRECTION_RANGE_TYPE == 1

#else // POST_PROCESS_SPACE_TYPE == 1 // Linear->Linear space

#if EARLY_DISPLAY_ENCODING
		// At this point, for this case, we expect the paper white to already have been multiplied in the color (earlier in the linear post processing pipeline)
		color.rgb /= gamePaperWhite;
#endif

#if !EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE <= 1

		ColorGradingLUTTransferFunctionInOutCorrected(color.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true); // We enforce "GAMMA_CORRECTION_RANGE_TYPE" 1 as the other case it too complicated and unnecessary to implement

// "GAMMA_CORRECTION_TYPE >= 2" is always delayed until the end and treated as sRGB gamma before (independently of "EARLY_DISPLAY_ENCODING").
// We originally applied this gamma correction directly during tonemapping/grading and other later passes,
// but given that the formula is slow to execute and isn't easily revertible
// (mirroring back and forth is lossy, at least in the current lightweight implementation),
// we moved it to a single application here (it might not look as good but it's certainly good enough).
// Any linear->gamma->linear encoding (e.g. "PostAAComposites") or linear->gamma->luminance encoding (e.g. Anti Aliasing)
// should fall back on gamma 2.2 instead of sRGB for this gamma correction type, but we haven't bothered implementing that (it's not worth it).
#elif GAMMA_CORRECTION_TYPE >= 2 // Note: this might not work with "UI_DRAW_TYPE == 2"

   		float3 colorInExcess = color.rgb - saturate(color.rgb); // Only correct in the 0-1 range
		color.rgb = saturate(color.rgb);

		float3 colorGammaCorrectedByChannel = gamma_to_linear(linear_to_sRGB_gamma(color.rgb));
		float luminanceGammaCorrected = gamma_to_linear1(linear_to_sRGB_gamma1(GetLuminance(color.rgb)));
		float3 colorGammaCorrectedByLuminance = RestoreLuminance(color.rgb, luminanceGammaCorrected);
#if GAMMA_CORRECTION_TYPE == 2
		color.rgb = RestoreLuminance(color.rgb, colorGammaCorrectedByChannel);
#elif GAMMA_CORRECTION_TYPE == 3
  		color.rgb = colorGammaCorrectedByLuminance;
#elif GAMMA_CORRECTION_TYPE >= 4
  		color.rgb = RestoreHueAndChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel, 0.0, 1.0);
#endif // GAMMA_CORRECTION_TYPE == 2

		color.rgb += colorInExcess;

#endif // !EARLY_DISPLAY_ENCODING && GAMMA_CORRECTION_TYPE <= 1

#endif // POST_PROCESS_SPACE_TYPE != 1

		color.rgb *= postLinearizationScale;

		bool gamutMap = true;
#if DEVELOPMENT || TEST // Optionally clip in SDR to properly emulate SDR (dev only)
		if (LumaSettings.DisplayMode >= 2)
		{
			color.rgb = saturate(color.rgb);
			gamutMap = false;
		}
#endif

		float aspectRatio = sourceWidth / (float)sourceHeight;
		bool blackBar = false;
		if (LumaSettings.DisplayMode == 1 && ShouldForceSDR(uv, false, blackBar, aspectRatio))
		{
			if (blackBar)
			{
				color.rgb = 0.0;
			}
			else
			{
#if defined(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL) && TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL
				color.rgb = saturate(color.rgb);
#else // Tonemap instead of raw clipping if the game didn't natively implement our split view mode in the tonemapper
    			const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  				color.rgb = Reinhard::ReinhardRange(color.rgb, MidGray, peakWhite, 1.0, true); // Acknowledge the previous peak tonemapping to better compress from the HDR display to the SDR range
#endif
				color.rgb = gamma_to_linear(round(linear_to_gamma(color.rgb) * 255.0) / 255.0); // Quantize to 8 bit
			}
		}

		if (gamutMap)
		{
			// Applying gamma correction could both generate negative (invalid) luminances and colors beyond the human visible range,
			// so here we try and fix them up.
			// Depending on the HDR tonemapper, film grain and sharpening math we used, prior passes might have also generated invalid colors.

#if GAMUT_MAPPING_TYPE > 0
			float3 preColor = color.rgb;

			FixColorGradingLUTNegativeLuminance(color.rgb);

			bool sdr = LumaSettings.DisplayMode != 1; // "GAMUT_MAPPING_TYPE == 1" is "auto"
#if GAMUT_MAPPING_TYPE == 2
			sdr = true;
#else // GAMUT_MAPPING_TYPE >= 3
			sdr = false;
#endif // GAMUT_MAPPING_TYPE == 2
			if (sdr)
			{
				color.rgb = SimpleGamutClip(color.rgb, false);
			}
			else
			{
				color.rgb = BT2020_To_BT709(SimpleGamutClip(BT709_To_BT2020(color.rgb), true)); // For scRGB HDR we could go even wider than BT.2020 (e.g. AP0) but it should overall do fine.
			}
#if DEVELOPMENT && 0 // Display gamut mapped colors
			if (any(abs(color.rgb - preColor.rgb) > 0.00001))
			{
				color.rgb = 100;
			}
#endif
#endif // GAMUT_MAPPING_TYPE > 0
		}
		
		color.rgb *= gamePaperWhite;
	}

#if 0 // Test
	color.rgb = float3(1, 0, 0);
#endif

	// clamp scRGB
	color.rgb = min(color.rgb, max(PeakWhiteNits, GamePaperWhiteNits) / 80.f);

#if SWAPCHAIN_TEST_PEAK
	color.rgb = 0;
	color.rgb = DrawRect(uv, float4(0.35,  0.47,  0.65,  0.53),  color.rgb, 10000.f          );
	color.rgb = DrawRect(uv, float4(0.365, 0.483, 0.448, 0.517), color.rgb, PeakWhiteNits * 2);
	color.rgb = DrawRect(uv, float4(0.458, 0.483, 0.542, 0.517), color.rgb, PeakWhiteNits    );
	color.rgb = DrawRect(uv, float4(0.552, 0.483, 0.635, 0.517), color.rgb, PeakWhiteNits / 2);
	color.rgb /= 80.f;
#endif

	return float4(color.rgb, color.a);
}