#pragma once

// Brütal Legend: filters the redundant D3D9 state calls the game makes into dgVoodoo's d3d9.dll (all builds).
// Each filtered state keeps a shadow of its last value; a call that would set the value the device already has is dropped.
// The device vtable is found through the patched dgVoodoo GetTexture site (main.cpp: TryPatchDgVoodooGetTextureBounds).

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "d3d9_cs_dispatch.h"

// Set by TryPatchDgVoodooGetTextureBounds() on success: the patched GetTexture site inside dgVoodoo's d3d9.dll.
static uintptr_t g_dgv_gettexture_site = 0;

// Drop calls that would set the value the device already has (shadow valid and equal). Mcp toggle "d3d9.filter".
static bool g_d3d9_filter = true;

// Shadow of the last value passed for a constant register range. valid = false means unknown.
struct D3DConsts
{
   float v[256][4];
   bool valid[256];
};

// Shadow of the last value passed per state. valid = false means unknown (never set since the last invalidation).
struct D3DShadow
{
   uintptr_t rs[256];
   bool rs_valid[256];
   uintptr_t samp[20][14];
   bool samp_valid[20][14];
   uintptr_t tex[260];
   bool tex_valid[260];
   uintptr_t tss[8][33];
   bool tss_valid[8][33];
   D3DConsts vs;
   D3DConsts ps;
   uintptr_t stream_vb[16];
   uintptr_t stream_off[16];
   uintptr_t stream_stride[16];
   bool stream_valid[16];
   uintptr_t indices;
   bool indices_valid;
   uintptr_t vs_shader;
   bool vs_valid;
   uintptr_t ps_shader;
   bool ps_valid;
   uintptr_t decl;
   bool decl_valid;
   uintptr_t fvf;
   bool fvf_valid;
};

enum class D3DCheck { Send, Drop, Foreign };

static D3DShadow g_d3d_shadow = {};
static bool g_d3d_in_block = false;       // between BeginStateBlock and EndStateBlock: calls pass through untouched
static DWORD g_d3d_owner_thread = 0;      // thread of the last draw call; state calls from other threads pass through
static bool g_d3d_toggle_pending = false; // filter toggled on: invalidate at the next draw
static bool g_d3d_sb_unhooked = false;    // a second state block vtable appeared: filtering stays off
static void* g_d3d_orig[128] = {};
static bool g_d3d_installed = false;
static bool g_d3d_attempted = false;
static void* g_d3d_sb_vtable = nullptr;
static void* g_d3d_sb_apply_orig = nullptr;

static void D3DInvalidateAll()
{
   g_d3d_shadow = D3DShadow{};
}

static bool D3DForeignThread()
{
   return g_d3d_owner_thread != 0 && GetCurrentThreadId() != g_d3d_owner_thread;
}

static bool D3DFilterAllowed()
{
   return g_d3d9_filter && !g_d3d_sb_unhooked && !g_d3d_in_block && g_d3d_owner_thread != 0 && GetCurrentThreadId() == g_d3d_owner_thread;
}

// Draw calls define the owner (render) thread. A draw from another thread takes ownership and invalidates everything.
static void D3DClaimDrawThread()
{
   const DWORD current = GetCurrentThreadId();
   if (g_d3d_owner_thread != current)
   {
      D3DInvalidateAll();
      g_d3d_owner_thread = current;
   }
   if (g_d3d_toggle_pending)
   {
      D3DInvalidateAll();
      g_d3d_toggle_pending = false;
   }
}

// Decides for one scalar-style state. same = shadow valid and equal to the new value.
// Foreign-thread calls only invalidate their shadow entry.
static D3DCheck D3DCheckShadow(bool& valid, bool same)
{
   if (D3DForeignThread())
   {
      valid = false;
      return D3DCheck::Foreign;
   }
   if (same && D3DFilterAllowed())
      return D3DCheck::Drop;
   return D3DCheck::Send;
}

// Shared logic for SetVertexShaderConstantF / SetPixelShaderConstantF. call(start, data, count4) forwards to the original.
template <typename Call>
static HRESULT D3DConstF(D3DConsts& c, UINT start, const float* data, UINT count4, Call&& call)
{
   if (g_d3d_in_block)
      return call(start, data, count4);

   const bool in_range = data != nullptr && count4 > 0 && count4 <= 256 && start <= 256 - count4;
   if (!in_range || D3DForeignThread())
   {
      for (UINT i = 0; i < count4 && start + i < 256; ++i)
         c.valid[start + i] = false;
      return call(start, data, count4);
   }

   UINT first = 0;
   UINT last = 0;
   bool any = false;
   for (UINT i = 0; i < count4; ++i)
   {
      if (c.valid[start + i] && std::memcmp(c.v[start + i], data + 4 * i, sizeof(float) * 4) == 0)
         continue;
      if (!any)
      {
         first = i;
         any = true;
      }
      last = i;
   }

   const bool filter = D3DFilterAllowed();
   if (!any && filter)
      return S_OK;

   UINT send_start = start;
   UINT send_count = count4;
   const float* send_data = data;
   if (any && filter)
   {
      send_start = start + first;
      send_count = last - first + 1;
      send_data = data + 4 * first;
   }

   const HRESULT hr = call(send_start, send_data, send_count);
   if (SUCCEEDED(hr))
   {
      for (UINT i = 0; i < count4; ++i)
      {
         std::memcpy(c.v[start + i], data + 4 * i, sizeof(float) * 4);
         c.valid[start + i] = true;
      }
   }
   return hr;
}

// ---- __stdcall wrappers: filter, update shadows after success, call the original vtable entry ----

static HRESULT __stdcall D3DWrap_Present(void* self, const RECT* src, const RECT* dst, HWND hwnd, const void* dirty)
{
   static bool last_filter = true;
   if (g_d3d9_filter && !last_filter)
      g_d3d_toggle_pending = true; // filter turned on: invalidated at the next draw on the owner thread
   last_filter = g_d3d9_filter;

   return reinterpret_cast<HRESULT(__stdcall*)(void*, const RECT*, const RECT*, HWND, const void*)>(g_d3d_orig[17])(self, src, dst, hwnd, dirty);
}

static HRESULT __stdcall D3DWrap_Reset(void* self, void* params)
{
   D3DInvalidateAll();
   const HRESULT hr = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(g_d3d_orig[16])(self, params);
   D3DInvalidateAll();
   return hr;
}

static HRESULT __stdcall D3DWrap_SetRenderTarget(void* self, DWORD index, void* surface)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD, void*)>(g_d3d_orig[37])(self, index, surface);
}

static HRESULT __stdcall D3DWrap_SetDepthStencilSurface(void* self, void* surface)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(g_d3d_orig[39])(self, surface);
}

static HRESULT __stdcall D3DWrap_Clear(void* self, DWORD count, const void* rects, DWORD flags, DWORD color, float z, DWORD stencil)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD, const void*, DWORD, DWORD, float, DWORD)>(g_d3d_orig[43])(self, count, rects, flags, color, z, stencil);
}

static HRESULT __stdcall D3DWrap_SetViewport(void* self, const void* viewport)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, const void*)>(g_d3d_orig[47])(self, viewport);
}

static HRESULT __stdcall D3DWrap_SetRenderState(void* self, DWORD state, DWORD value)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD, DWORD)>(g_d3d_orig[57]);
   if (g_d3d_in_block || state >= 256)
      return fn(self, state, value);
   auto& s = g_d3d_shadow;
   const D3DCheck check = D3DCheckShadow(s.rs_valid[state], s.rs_valid[state] && s.rs[state] == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, state, value);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.rs[state] = value;
      s.rs_valid[state] = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_BeginStateBlock(void* self)
{
   g_d3d_in_block = true;
   return reinterpret_cast<HRESULT(__stdcall*)(void*)>(g_d3d_orig[60])(self);
}

static void D3DHookStateBlock(void* block);

static HRESULT __stdcall D3DWrap_EndStateBlock(void* self, void** block)
{
   g_d3d_in_block = false;
   D3DInvalidateAll();
   const HRESULT hr = reinterpret_cast<HRESULT(__stdcall*)(void*, void**)>(g_d3d_orig[61])(self, block);
   if (SUCCEEDED(hr) && block != nullptr && *block != nullptr)
      D3DHookStateBlock(*block);
   return hr;
}

static HRESULT __stdcall D3DWrap_CreateStateBlock(void* self, UINT type, void** block)
{
   const HRESULT hr = reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, void**)>(g_d3d_orig[59])(self, type, block);
   if (SUCCEEDED(hr) && block != nullptr && *block != nullptr && !CsSbIsProxy(*block))
      D3DHookStateBlock(*block);
   return hr;
}

// IDirect3DStateBlock9 slot 5 is Apply: applying a block changes device state behind the shadows.
static HRESULT __stdcall D3DWrap_SBApply(void* self)
{
   const HRESULT hr = reinterpret_cast<HRESULT(__stdcall*)(void*)>(g_d3d_sb_apply_orig)(self);
   D3DInvalidateAll();
   return hr;
}

// Patches the Apply slot of the first state block vtable seen (idempotent). A different vtable disables filtering.
static void D3DHookStateBlock(void* block)
{
   void** vt = *reinterpret_cast<void***>(block);
   if (vt == g_d3d_sb_vtable)
      return;
   if (g_d3d_sb_vtable != nullptr)
   {
      if (!g_d3d_sb_unhooked)
         reshade::log::message(reshade::log::level::warning, "[BL D3D9] filter: second state block vtable, Apply not hooked, filtering disabled");
      g_d3d_sb_unhooked = true;
      return;
   }
   DWORD old_protection = 0;
   if (!VirtualProtect(&vt[5], sizeof(void*), PAGE_READWRITE, &old_protection))
   {
      g_d3d_sb_unhooked = true;
      return;
   }
   g_d3d_sb_apply_orig = vt[5];
   vt[5] = reinterpret_cast<void*>(&D3DWrap_SBApply);
   DWORD ignored = 0;
   VirtualProtect(&vt[5], sizeof(void*), old_protection, &ignored);
   g_d3d_sb_vtable = vt;
}

static HRESULT __stdcall D3DWrap_SetTexture(void* self, DWORD stage, void* texture)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD, void*)>(g_d3d_orig[65]);
   if (g_d3d_in_block || stage >= 260)
      return fn(self, stage, texture);
   auto& s = g_d3d_shadow;
   const uintptr_t value = reinterpret_cast<uintptr_t>(texture);
   const D3DCheck check = D3DCheckShadow(s.tex_valid[stage], s.tex_valid[stage] && s.tex[stage] == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, stage, texture);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.tex[stage] = value;
      s.tex_valid[stage] = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetTextureStageState(void* self, DWORD stage, DWORD type, DWORD value)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD)>(g_d3d_orig[67]);
   if (g_d3d_in_block || stage >= 8 || type >= 33)
      return fn(self, stage, type, value);
   auto& s = g_d3d_shadow;
   const D3DCheck check = D3DCheckShadow(s.tss_valid[stage][type], s.tss_valid[stage][type] && s.tss[stage][type] == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, stage, type, value);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.tss[stage][type] = value;
      s.tss_valid[stage][type] = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetSamplerState(void* self, DWORD sampler, DWORD type, DWORD value)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD, DWORD, DWORD)>(g_d3d_orig[69]);
   if (g_d3d_in_block || sampler >= 20 || type >= 14)
      return fn(self, sampler, type, value);
   auto& s = g_d3d_shadow;
   const D3DCheck check = D3DCheckShadow(s.samp_valid[sampler][type], s.samp_valid[sampler][type] && s.samp[sampler][type] == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, sampler, type, value);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.samp[sampler][type] = value;
      s.samp_valid[sampler][type] = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetScissorRect(void* self, const RECT* rect)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, const RECT*)>(g_d3d_orig[75])(self, rect);
}

static HRESULT __stdcall D3DWrap_DrawPrimitive(void* self, UINT type, UINT start_vertex, UINT primitive_count)
{
   D3DClaimDrawThread();
   return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, UINT, UINT)>(g_d3d_orig[81])(self, type, start_vertex, primitive_count);
}

static HRESULT __stdcall D3DWrap_DrawIndexedPrimitive(void* self, UINT type, INT base_vertex, UINT min_index, UINT num_vertices, UINT start_index, UINT primitive_count)
{
   D3DClaimDrawThread();
   return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, INT, UINT, UINT, UINT, UINT)>(g_d3d_orig[82])(self, type, base_vertex, min_index, num_vertices, start_index, primitive_count);
}

static HRESULT __stdcall D3DWrap_DrawPrimitiveUP(void* self, UINT type, UINT primitive_count, const void* data, UINT stride)
{
   D3DClaimDrawThread();
   return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, UINT, const void*, UINT)>(g_d3d_orig[83])(self, type, primitive_count, data, stride);
}

static HRESULT __stdcall D3DWrap_DrawIndexedPrimitiveUP(void* self, UINT type, UINT min_vertex, UINT num_vertices, UINT primitive_count, const void* index_data, UINT index_format, const void* vertex_data, UINT stride)
{
   D3DClaimDrawThread();
   return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, UINT, UINT, UINT, const void*, UINT, const void*, UINT)>(g_d3d_orig[84])(self, type, min_vertex, num_vertices, primitive_count, index_data, index_format, vertex_data, stride);
}

static HRESULT __stdcall D3DWrap_SetVertexDeclaration(void* self, void* decl)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(g_d3d_orig[87]);
   if (g_d3d_in_block)
      return fn(self, decl);
   auto& s = g_d3d_shadow;
   const uintptr_t value = reinterpret_cast<uintptr_t>(decl);
   const D3DCheck check = D3DCheckShadow(s.decl_valid, s.decl_valid && s.decl == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, decl);
   // A declaration replaces the FVF in D3D9, so the FVF shadow is stale after a sent call.
   if (check == D3DCheck::Foreign)
      s.fvf_valid = false;
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.decl = value;
      s.decl_valid = true;
      s.fvf_valid = false;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetFVF(void* self, DWORD fvf)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD)>(g_d3d_orig[89]);
   if (g_d3d_in_block)
      return fn(self, fvf);
   auto& s = g_d3d_shadow;
   const D3DCheck check = D3DCheckShadow(s.fvf_valid, s.fvf_valid && s.fvf == fvf);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, fvf);
   // An FVF replaces the vertex declaration in D3D9, so the declaration shadow is stale after a sent call.
   if (check == D3DCheck::Foreign)
      s.decl_valid = false;
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.fvf = fvf;
      s.fvf_valid = true;
      s.decl_valid = false;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetVertexShader(void* self, void* shader)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(g_d3d_orig[92]);
   if (g_d3d_in_block)
      return fn(self, shader);
   auto& s = g_d3d_shadow;
   const uintptr_t value = reinterpret_cast<uintptr_t>(shader);
   const D3DCheck check = D3DCheckShadow(s.vs_valid, s.vs_valid && s.vs_shader == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, shader);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.vs_shader = value;
      s.vs_valid = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetVertexShaderConstantF(void* self, UINT start, const float* data, UINT count4)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, const float*, UINT)>(g_d3d_orig[94]);
   return D3DConstF(g_d3d_shadow.vs, start, data, count4,
      [&](UINT st, const float* d, UINT n) { return fn(self, st, d, n); });
}

static HRESULT __stdcall D3DWrap_SetVertexShaderConstantI(void* self, UINT start, const int* data, UINT count4)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, const int*, UINT)>(g_d3d_orig[96])(self, start, data, count4);
}

static HRESULT __stdcall D3DWrap_SetVertexShaderConstantB(void* self, UINT start, const BOOL* data, UINT count)
{
   return reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, const BOOL*, UINT)>(g_d3d_orig[98])(self, start, data, count);
}

static HRESULT __stdcall D3DWrap_SetStreamSource(void* self, UINT stream, void* vb, UINT offset, UINT stride)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, void*, UINT, UINT)>(g_d3d_orig[100]);
   if (g_d3d_in_block || stream >= 16)
      return fn(self, stream, vb, offset, stride);
   auto& s = g_d3d_shadow;
   const uintptr_t value = reinterpret_cast<uintptr_t>(vb);
   const bool same = s.stream_valid[stream] && s.stream_vb[stream] == value && s.stream_off[stream] == offset && s.stream_stride[stream] == stride;
   const D3DCheck check = D3DCheckShadow(s.stream_valid[stream], same);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, stream, vb, offset, stride);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.stream_vb[stream] = value;
      s.stream_off[stream] = offset;
      s.stream_stride[stream] = stride;
      s.stream_valid[stream] = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetIndices(void* self, void* ib)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(g_d3d_orig[104]);
   if (g_d3d_in_block)
      return fn(self, ib);
   auto& s = g_d3d_shadow;
   const uintptr_t value = reinterpret_cast<uintptr_t>(ib);
   const D3DCheck check = D3DCheckShadow(s.indices_valid, s.indices_valid && s.indices == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, ib);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.indices = value;
      s.indices_valid = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetPixelShader(void* self, void* shader)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, void*)>(g_d3d_orig[107]);
   if (g_d3d_in_block)
      return fn(self, shader);
   auto& s = g_d3d_shadow;
   const uintptr_t value = reinterpret_cast<uintptr_t>(shader);
   const D3DCheck check = D3DCheckShadow(s.ps_valid, s.ps_valid && s.ps_shader == value);
   if (check == D3DCheck::Drop)
      return S_OK;
   const HRESULT hr = fn(self, shader);
   if (check == D3DCheck::Send && SUCCEEDED(hr))
   {
      s.ps_shader = value;
      s.ps_valid = true;
   }
   return hr;
}

static HRESULT __stdcall D3DWrap_SetPixelShaderConstantF(void* self, UINT start, const float* data, UINT count4)
{
   auto fn = reinterpret_cast<HRESULT(__stdcall*)(void*, UINT, const float*, UINT)>(g_d3d_orig[109]);
   return D3DConstF(g_d3d_shadow.ps, start, data, count4,
      [&](UINT st, const float* d, UINT n) { return fn(self, st, d, n); });
}

// Accepts the device vtable whose slot 64 (GetTexture) points at the dgVoodoo site patched by TryPatchDgVoodooGetTextureBounds().
static bool BLDgvGetTextureSlotAccept(void** vtable)
{
   const uintptr_t target = reinterpret_cast<uintptr_t>(vtable[64]);
   return target <= g_dgv_gettexture_site && g_dgv_gettexture_site < target + 0x400;
}

// Installs the filter wrappers on top of the device vtable `vtable` (the command-stream thunks, see InstallD3D9Cs).
// Idempotent. A null vtable (locator failed) patches nothing.
static void InstallD3D9CallFilter(void** vtable)
{
   if (g_d3d_installed || g_d3d_attempted)
      return;
   g_d3d_attempted = true;
   if (vtable == nullptr)
      return;
   g_cs_sb_apply_cb = &D3DInvalidateAll;   // proxy state-block Apply invalidates like the hooked real-block Apply

   struct D3DHook
   {
      int slot;
      void* fn;
   };
   const D3DHook hooks[] = {
      { 16, reinterpret_cast<void*>(&D3DWrap_Reset) },
      { 17, reinterpret_cast<void*>(&D3DWrap_Present) },
      { 37, reinterpret_cast<void*>(&D3DWrap_SetRenderTarget) },
      { 39, reinterpret_cast<void*>(&D3DWrap_SetDepthStencilSurface) },
      { 43, reinterpret_cast<void*>(&D3DWrap_Clear) },
      { 47, reinterpret_cast<void*>(&D3DWrap_SetViewport) },
      { 57, reinterpret_cast<void*>(&D3DWrap_SetRenderState) },
      { 59, reinterpret_cast<void*>(&D3DWrap_CreateStateBlock) },
      { 60, reinterpret_cast<void*>(&D3DWrap_BeginStateBlock) },
      { 61, reinterpret_cast<void*>(&D3DWrap_EndStateBlock) },
      { 65, reinterpret_cast<void*>(&D3DWrap_SetTexture) },
      { 67, reinterpret_cast<void*>(&D3DWrap_SetTextureStageState) },
      { 69, reinterpret_cast<void*>(&D3DWrap_SetSamplerState) },
      { 75, reinterpret_cast<void*>(&D3DWrap_SetScissorRect) },
      { 81, reinterpret_cast<void*>(&D3DWrap_DrawPrimitive) },
      { 82, reinterpret_cast<void*>(&D3DWrap_DrawIndexedPrimitive) },
      { 83, reinterpret_cast<void*>(&D3DWrap_DrawPrimitiveUP) },
      { 84, reinterpret_cast<void*>(&D3DWrap_DrawIndexedPrimitiveUP) },
      { 87, reinterpret_cast<void*>(&D3DWrap_SetVertexDeclaration) },
      { 89, reinterpret_cast<void*>(&D3DWrap_SetFVF) },
      { 92, reinterpret_cast<void*>(&D3DWrap_SetVertexShader) },
      { 94, reinterpret_cast<void*>(&D3DWrap_SetVertexShaderConstantF) },
      { 96, reinterpret_cast<void*>(&D3DWrap_SetVertexShaderConstantI) },
      { 98, reinterpret_cast<void*>(&D3DWrap_SetVertexShaderConstantB) },
      { 100, reinterpret_cast<void*>(&D3DWrap_SetStreamSource) },
      { 104, reinterpret_cast<void*>(&D3DWrap_SetIndices) },
      { 107, reinterpret_cast<void*>(&D3DWrap_SetPixelShader) },
      { 109, reinterpret_cast<void*>(&D3DWrap_SetPixelShaderConstantF) },
   };

   for (const D3DHook& h : hooks)
   {
      DWORD old_protection = 0;
      if (!VirtualProtect(&vtable[h.slot], sizeof(void*), PAGE_READWRITE, &old_protection))
         continue;
      g_d3d_orig[h.slot] = vtable[h.slot];
      vtable[h.slot] = h.fn;
      DWORD ignored = 0;
      VirtualProtect(&vtable[h.slot], sizeof(void*), old_protection, &ignored);
   }

   g_d3d_installed = true;
}
