#include "..\..\Core\core.hpp"
#include "hooks.hpp"

uintptr_t* AAOptionBase = nullptr;
uintptr_t CDeferredFxAntialiasRenderer = 0;
uintptr_t* m_deferredFXRendererContext = nullptr;
CDeferredFxRendererContextTextures m_deferredFXRendererContextTextures;
CSceneViewportPrivateData* m_viewportPrivateData = nullptr;
CViewportShaderParameterProvider* m_viewportParamProvider = nullptr;
CDeferredFxAntialiasRendererS* m_deferredFxAntialiasRenderer = nullptr;
CTexture* m_currDeferredFXAntialiasFrameTexture = nullptr;
uintptr_t JitterTableOffset = 0;

std::atomic<bool> bIsNetHackingRendering = false;

fnGetExistingSharedTexture GetExistingSharedTexture = nullptr;

AAOptions GetAAOption()
{
   return *(AAOptions*)(*(uintptr_t*)(*AAOptionBase) + 0x3A4);
}

__int64 __fastcall Hooked_CDeferredFxAntialiasRendererPrepare(__int64 a1, uintptr_t* a2)
{
   if (a1)
   {
      CDeferredFxAntialiasRenderer = a1;
      m_deferredFxAntialiasRenderer = reinterpret_cast<CDeferredFxAntialiasRendererS*>(a1);
   }
   
   if (a2 && a2[13])
   {
      m_deferredFXRendererContext = (uintptr_t*)a2;
      uintptr_t base = a2[13];
      m_viewportPrivateData = reinterpret_cast<CSceneViewportPrivateData*>(base);
      base = a2[8];
      m_viewportParamProvider = reinterpret_cast<CViewportShaderParameterProvider*>(base);
      
      {
         m_deferredFXRendererContextTextures.m_accumBuffer = reinterpret_cast<CIndirectTexture*>(a2[0]);
         m_deferredFXRendererContextTextures.m_linearDepthTexture = reinterpret_cast<CIndirectTexture*>(a2[1]);
         m_deferredFXRendererContextTextures.m_smallDepthColorTexture = reinterpret_cast<CIndirectTexture*>(a2[2]);
         m_deferredFXRendererContextTextures.m_depthStencilSurface = reinterpret_cast<CIndirectTexture*>(a2[3]);
         
         //uintptr_t* motion_vector_base = (uintptr_t*)((uint8_t*)m_viewportPrivateData->m_textures[1] - 0x900);
         
         m_deferredFXRendererContextTextures.m_motionVectors = reinterpret_cast<CIndirectTexture*>(GetExistingSharedTexture(*(__int64 *)(a1 + 48), 931095925));
         m_deferredFXRendererContextTextures.m_normalsTexture = reinterpret_cast<CIndirectTexture*>(a2[5]);
         m_deferredFXRendererContextTextures.m_gBufferAOTexture = reinterpret_cast<uintptr_t*>(a2[6]);
         m_deferredFXRendererContextTextures.m_fullAOTexture = reinterpret_cast<uintptr_t*>(a2[7]);
      }
   }

   auto original_result = g_deferred_fx_antialias_renderer_hook
       .unsafe_call<__int64>(a1, a2);
   
   if (a1)
   {
      m_currDeferredFXAntialiasFrameTexture = m_deferredFxAntialiasRenderer->m_currDeferredFXAntialiasFrameTexture;
   }
   
   return original_result;
}

__int64 __fastcall Hooked_CNetHackingRendererPrepare(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, __int64 a7, __int64 a8)
{
   auto original_result = g_net_hacking_renderer_hook
       .unsafe_call<__int64>(a1, a2, a3, a4, a5, a6, a7, a8);
   
   bIsNetHackingRendering = true;
   
   return original_result;
}