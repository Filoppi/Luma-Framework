#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#else
namespace TonemapInfo
{
   static int FlagDrawnTonemap =     0x40000000; //1<<30
   // static int FlagSprites =          0x20000000; //1<<29
   // static int FlagComplex =          0x10000000; //1<<28
   static int FlagDrawnFinal =       0x08000000; //1<<27
   static int FlagIsFMV =            0x04000000; //1<<26
   static int FlagDrawnHPBarDelta =  0x02000000; //1<<25
   static int IndexBitMask =         0x0000000F;
         
   int GetDefaultReset() { return 0; }
         
   int SetDrawnTonemapTrue(int v) { return v | FlagDrawnTonemap; }
   bool GetDrawnTonemap(int v) { return (v & FlagDrawnTonemap) > 0; }
         
   // int SetSpritesTrue(int v) { return v | FlagSprites; }
   // bool GetSprites(int v) { return (v & FlagSprites) > 0; }
         
   // int SetComplexTrue(int v) { return v | FlagComplex; }
   // bool GetComplex(int v) { return (v & FlagComplex) > 0; }

   int SetDrawnFinalTrue(int v) { return v | FlagDrawnFinal; }
   bool GetDrawnFinal(int v) { return (v & FlagDrawnFinal) > 0; }

   int SetIsFMVTrue(int v) { return v | FlagIsFMV; }
   bool GetIsFMV(int v) { return (v & FlagIsFMV) > 0; }

   int SetDrawnHPBarDeltaTrue(int v) { return v | FlagDrawnHPBarDelta; }
   bool GetDrawnHPBarDelta(int v) { return (v & FlagDrawnHPBarDelta) > 0; }
         
   int SetIndexAndDrawnTonemapTrue(int v, int i) { return v | FlagDrawnTonemap | (IndexBitMask & i); }
   int GetIndex(int v) { return v & IndexBitMask; }
   int GetIndexOnlyIfDrawn(int v) { return GetDrawnTonemap(v) ? v & IndexBitMask : -1; }
}

#endif


namespace CB
{
   struct LumaGameSettings
   {
      int TonemapInfo;
      
      float TonemapperRolloffStart;
      
      float Exposure;
      float BloomStrength;
      
      float LUT;
      float LUTBlowoutReduction;
      float LUTBlowoutReductionLookBack;
      
      float PCBlowoutStrength;
      float PCBlowoutStart;
      float PCBlowoutEnd;
      
      float FakeBT2020ChromaCorrect;
      float FakeBT2020LumaCorrect;
      
      float UpscaleMovPumboPow; 
      float UpscaleBGSpritesMax; 
      float UpscaleBGSpritesExp;
      float UpscaleToonMax; 
      float UpscaleToonExp;

      float HUDBrightnessHealthBar; 
      float HUDBrightnessHealthBarDelta; 
      float HUDBrightnessProgressBar; 
      float HUDBrightnessNoteResponse;
      float HUDBrightnessHoldComboBg; 
      float HUDBrightnessPJDLogo; 
      
      float CGContrast;
      float CGContrastMidGray;
      float CGSaturation;
      float CGHighlightsStrength;
      float CGHighlightsMidGray;
      float CGShadowsStrength;
      float CGShadowsMidGray;
   };
   
   struct LumaGameData
   {
      float Dummy;
   };
}



#endif // LUMA_GAME_CB_STRUCTS
