#pragma once

#include "shader_types.h"
#include "matrix.h"

namespace
{
   namespace
   {
      enum class DisplayModeType : uint;
   }
}

namespace CB
{
   using namespace Math;

   // In case the per game code had not defined a custom struct, define a generic empty one. This behaviour is matched in hlsl.
   // Purpusely leave "LUMA_GAME_CB_STRUCTS" undefined if we didn't already have a game definition, we need to be able to tell if we had one!
#ifndef LUMA_GAME_CB_STRUCTS
   // hlsl doesn't support empty structs, so add a dummy variable (ideally it'd be empty or optional but it won't realistically affect performance)
   struct LumaGameSettings
   {
      float Dummy;
   };
   struct LumaGameData
   {
      float Dummy;
   };
#endif

   struct LumaDevSettings
   {
      static constexpr size_t SettingsNum = 10;

      LumaDevSettings(float Value = 0.f)
      {
         for (size_t i = 0; i < SettingsNum; i++)
         {
            Settings[i] = Value;
         }
      }
      float& operator[](const size_t i)
      {
         return Settings[i];
      }
      float Settings[SettingsNum];
   };

   // Luma global settings, usually changed a max of once per frame.
   // This is mirrored in shaders (it's described there).
   struct LumaGlobalSettings
   {
      float2 SwapchainSize;
      float2 SwapchainInvSize;
      DisplayModeType DisplayMode;
      float ScenePeakWhite;
      float ScenePaperWhite;
      float UIPaperWhite;
      uint SRType;
      uint FrameIndex;

#if DEVELOPMENT // In case we disabled the "DEVELOPMENT" shader define while the code is compiled in "DEVELOPMENT" mode, we'll simply push values that aren't read by shaders (see "CPU_DEVELOPMENT")
      LumaDevSettings DevSettings;
#else
      float2 Padding1;
#endif // DEVELOPMENT

      LumaGameSettings GameSettings; // Custom games setting, with a per game struct
   };
   // Have a pre-padded version to satisfy DX buffer requirements (if we aligned the original struct, it'd pad in between structs and mess up the alignment to the GPU etc)
   struct alignas(16) LumaGlobalSettingsPadded : LumaGlobalSettings { };
   static_assert(sizeof(LumaGlobalSettingsPadded) % sizeof(uint32_t) == 0); // ReShade limitation, we probably don't depend on these anymore, still, it's not bad to have 4 bytes alignment, even if cbuffers are seemengly 8 byte aligned?
   static_assert(sizeof(LumaGlobalSettingsPadded) % (sizeof(uint32_t) * 4) == 0); // Apparently needed by DX
   static_assert(sizeof(LumaGlobalSettingsPadded) >= 16); // Needed by DX (there's a minimum size of 16 bytes)

   // See the hlsl declaration for more context
   struct LumaInstanceData
   {
      uint CustomData1; // Per call/instance data
      uint CustomData2; // Per call/instance data
      float CustomData3; // Per call/instance data
      float CustomData4; // Per call/instance data

      float2 RenderResolutionScale;
      float2 PreviousRenderResolutionScale;
      
      uint RenderScaleActive;
      float3 Padding1;

      LumaGameData GameData; // Custom games data, with a per game struct
   };
   struct alignas(16) LumaInstanceDataPadded : LumaInstanceData { };
   static_assert(sizeof(LumaInstanceDataPadded) % sizeof(uint32_t) == 0);
   static_assert(sizeof(LumaInstanceDataPadded) % (sizeof(uint32_t) * 4) == 0);
   static_assert(sizeof(LumaInstanceDataPadded) >= 16);

   struct LumaUIData
   {
      uint targeting_swapchain = 0;
      uint fullscreen_menu = 0;
      uint blend_mode = 0;
      float background_tonemapping_amount = 0.f;
   };
   struct alignas(16) LumaUIDataPadded : LumaUIData { };
   static_assert(sizeof(LumaUIDataPadded) % sizeof(uint32_t) == 0);
   static_assert(sizeof(LumaUIDataPadded) % (sizeof(uint32_t) * 4) == 0);
   static_assert(sizeof(LumaUIDataPadded) >= 16);
}