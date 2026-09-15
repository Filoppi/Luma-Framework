#pragma once
#include <d3d11.h>
#include <array>
#include <atomic>
#include <mutex>
#include "../../External/reshade/deps/minhook/include/MinHook.h"

namespace FC5::NativeUploads
{
   using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
   using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
   using MapObserver = void(*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, const D3D11_MAPPED_SUBRESOURCE*);
   using UnmapObserver = void(*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
   struct Hook { void* target = nullptr; void* original = nullptr; };
   inline std::array<Hook, 8> maps{}, unmaps{};
   inline std::mutex install_mutex;
   inline bool initialized = false;
   inline std::atomic<MapObserver> on_map{nullptr};
   inline std::atomic<UnmapObserver> before_unmap{nullptr}, after_unmap{nullptr};
   inline thread_local unsigned nesting = 0;

   template<size_t I> HRESULT STDMETHODCALLTYPE Map(ID3D11DeviceContext* ctx, ID3D11Resource* resource,
      UINT subresource, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
   {
      const bool outer = nesting++ == 0;
      const HRESULT hr = reinterpret_cast<MapFn>(maps[I].original)(ctx, resource, subresource, type, flags, mapped);
      if (outer && SUCCEEDED(hr)) if (auto fn = on_map.load()) fn(ctx, resource, subresource, type, mapped);
      --nesting;
      return hr;
   }
   template<size_t I> void STDMETHODCALLTYPE Unmap(ID3D11DeviceContext* ctx, ID3D11Resource* resource, UINT subresource)
   {
      const bool outer = nesting++ == 0;
      if (outer) if (auto fn = before_unmap.load()) fn(ctx, resource, subresource);
      reinterpret_cast<UnmapFn>(unmaps[I].original)(ctx, resource, subresource);
      if (outer) if (auto fn = after_unmap.load()) fn(ctx, resource, subresource);
      --nesting;
   }
   inline const std::array<MapFn, 8> map_detours{Map<0>,Map<1>,Map<2>,Map<3>,Map<4>,Map<5>,Map<6>,Map<7>};
   inline const std::array<UnmapFn, 8> unmap_detours{Unmap<0>,Unmap<1>,Unmap<2>,Unmap<3>,Unmap<4>,Unmap<5>,Unmap<6>,Unmap<7>};

   template<class T> bool InstallOne(void* target, std::array<Hook, 8>& hooks, const std::array<T, 8>& detours)
   {
      for (const auto& h : hooks) if (h.target == target) return true;
      for (size_t i = 0; i < hooks.size(); ++i)
      {
         auto& h = hooks[i];
         if (h.target) continue;
         if (MH_CreateHook(target, reinterpret_cast<void*>(detours[i]), &h.original) != MH_OK) return false;
         h.target = target;
         if (MH_EnableHook(target) == MH_OK) return true;
         MH_RemoveHook(target); h = {}; return false;
      }
      return false;
   }
   inline bool Install(ID3D11DeviceContext* ctx)
   {
      std::lock_guard lock(install_mutex);
      if (!initialized)
      {
         if (MH_Initialize() != MH_OK) return false;
         initialized = true;
      }
      auto** table = *reinterpret_cast<void***>(ctx);
      // ID3D11DeviceContext ABI: IUnknown(3), DeviceChild(4), 7 methods, Map/Unmap.
      const bool a = InstallOne(table[14], maps, map_detours);
      const bool b = InstallOne(table[15], unmaps, unmap_detours);
      return a && b;
   }
   inline void Shutdown()
   {
      on_map = nullptr; before_unmap = nullptr; after_unmap = nullptr;
      std::lock_guard lock(install_mutex);
      if (!initialized) return;
      for (const auto& h : maps) if (h.target) MH_QueueDisableHook(h.target);
      for (const auto& h : unmaps) if (h.target) MH_QueueDisableHook(h.target);
      MH_ApplyQueued();
      for (auto& h : maps) { if (h.target) MH_RemoveHook(h.target); h = {}; }
      for (auto& h : unmaps) { if (h.target) MH_RemoveHook(h.target); h = {}; }
      MH_Uninitialize(); initialized = false;
   }
}
