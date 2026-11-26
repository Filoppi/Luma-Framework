#pragma once
// Method table for the D3D9 call-stream threading layer (Win32 dgVoodoo build).
// Vtable slot = index in the SDK vtable (IUnknown 0..2 included).
// Argument counts come from the member function types (CsArgc), so they follow d3d9.h.
//
// Row meaning:
//  mode       CsM_Async: queued, returns D3D_OK at once. CsM_Block: queued in order, caller waits, real return value.
//             CsM_Pass: never hooked. Device Present is async, but a non-null pDirtyRegion (arg 4) must block.
//  ptr[]      Async rows only: pointers whose data is copied into the ring. Kinds:
//             Fixed = fixed_bytes; Vec4Count = count_arg * 16; BoolCount = count_arg * 4; RectCount = count_arg * 16.
//             UPVerts with count_arg 0: vertex count derived from PrimitiveType + PrimitiveCount (DrawPrimitiveUP).
//             UPVerts with count_arg > 0: count_arg is the NumVertices argument (DrawIndexedPrimitiveUP).
//             UPIndices: count_arg is PrimitiveCount; index format is the argument after the pointer.
//             Stride for UPVerts = the argument after the pointer.
//             Nullable pointers stay null: copy only when the pointer is non-null.
//  out_arg    Argument index (0 = this) of the returned interface pointer to adopt. 0 = none.
//  out_iface  Used only when out_arg != 0. CsI_Count there = resolve at runtime via GetType (slot 10).
//  lock_flags_arg  Index of the Flags argument of Lock/LockRect/LockBox. 0 = none.

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <windows.h>
#include <d3d9.h>

static_assert(sizeof(void*) == 4, "D3D9 call-stream tables assume the x86 stdcall ABI (4-byte arguments)");

// Logging hook set by the mod (level 0 = info, 1 = warning). Null = silent.
inline void (*g_cs_log)(int level, const char* msg) = nullptr;

enum CsIface : uint8_t {
    CsI_Device, CsI_VertexBuffer, CsI_IndexBuffer, CsI_Texture, CsI_CubeTexture, CsI_VolumeTexture,
    CsI_Surface, CsI_Volume, CsI_SwapChain, CsI_VertexShader, CsI_PixelShader, CsI_VertexDecl, CsI_StateBlock, CsI_Query,
    CsI_Count
};
enum CsMode : uint8_t { CsM_Block, CsM_Async, CsM_Pass };
enum CsPtrKind : uint8_t { CsP_None, CsP_Fixed, CsP_Vec4Count, CsP_BoolCount, CsP_RectCount, CsP_UPVerts, CsP_UPIndices };

struct CsPtrArg {
    uint8_t arg;            // argument index, 0 = this
    CsPtrKind kind;
    uint16_t fixed_bytes;   // Fixed only
    uint8_t count_arg;      // count argument index (0 = none / derived)
};

struct CsMethod {
    const char* name;
    CsIface iface;
    uint8_t slot;           // vtable index
    uint8_t argc;           // arguments including this
    CsMode mode;
    CsPtrArg ptr[4];
    uint8_t out_arg;        // 0 = none
    CsIface out_iface;
    uint8_t lock_flags_arg; // 0 = none
};

// Argument count from the member function type: this + one 4-byte slot per argument (x86 stdcall).
template <class C, class R, class... A>
constexpr uint8_t CsArgc(R (__stdcall C::*)(A...)) {
    // References (REFIID/REFGUID) are passed as pointers: sizeof(T&) would give the referent's size.
    return static_cast<uint8_t>(1 + ((std::is_reference_v<A> ? 1 : (sizeof(A) + 3) / 4) + ... + 0));
}
static_assert(CsArgc(&IDirect3DDevice9::QueryInterface) == 3 && CsArgc(&IDirect3DTexture9::SetPrivateData) == 5);

template <size_t N>
constexpr bool CsSlotsInOrder(const CsMethod (&t)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (t[i].slot != i) return false;
    }
    return true;
}

// IDirect3DDevice9: 119 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsDevice[] = {
    {"QueryInterface", CsI_Device, 0, CsArgc(&IDirect3DDevice9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_Device, 1, CsArgc(&IDirect3DDevice9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_Device, 2, CsArgc(&IDirect3DDevice9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"TestCooperativeLevel", CsI_Device, 3, CsArgc(&IDirect3DDevice9::TestCooperativeLevel), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetAvailableTextureMem", CsI_Device, 4, CsArgc(&IDirect3DDevice9::GetAvailableTextureMem), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"EvictManagedResources", CsI_Device, 5, CsArgc(&IDirect3DDevice9::EvictManagedResources), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDirect3D", CsI_Device, 6, CsArgc(&IDirect3DDevice9::GetDirect3D), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDeviceCaps", CsI_Device, 7, CsArgc(&IDirect3DDevice9::GetDeviceCaps), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDisplayMode", CsI_Device, 8, CsArgc(&IDirect3DDevice9::GetDisplayMode), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetCreationParameters", CsI_Device, 9, CsArgc(&IDirect3DDevice9::GetCreationParameters), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetCursorProperties", CsI_Device, 10, CsArgc(&IDirect3DDevice9::SetCursorProperties), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetCursorPosition", CsI_Device, 11, CsArgc(&IDirect3DDevice9::SetCursorPosition), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"ShowCursor", CsI_Device, 12, CsArgc(&IDirect3DDevice9::ShowCursor), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateAdditionalSwapChain", CsI_Device, 13, CsArgc(&IDirect3DDevice9::CreateAdditionalSwapChain), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_SwapChain, 0},
    {"GetSwapChain", CsI_Device, 14, CsArgc(&IDirect3DDevice9::GetSwapChain), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_SwapChain, 0},
    {"GetNumberOfSwapChains", CsI_Device, 15, CsArgc(&IDirect3DDevice9::GetNumberOfSwapChains), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Reset", CsI_Device, 16, CsArgc(&IDirect3DDevice9::Reset), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Present", CsI_Device, 17, CsArgc(&IDirect3DDevice9::Present), CsM_Async, {{1, CsP_Fixed, 16, 0}, {2, CsP_Fixed, 16, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetBackBuffer", CsI_Device, 18, CsArgc(&IDirect3DDevice9::GetBackBuffer), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 4, CsI_Surface, 0},
    {"GetRasterStatus", CsI_Device, 19, CsArgc(&IDirect3DDevice9::GetRasterStatus), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetDialogBoxMode", CsI_Device, 20, CsArgc(&IDirect3DDevice9::SetDialogBoxMode), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetGammaRamp", CsI_Device, 21, CsArgc(&IDirect3DDevice9::SetGammaRamp), CsM_Async, {{3, CsP_Fixed, 1536, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetGammaRamp", CsI_Device, 22, CsArgc(&IDirect3DDevice9::GetGammaRamp), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateTexture", CsI_Device, 23, CsArgc(&IDirect3DDevice9::CreateTexture), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 7, CsI_Texture, 0},
    {"CreateVolumeTexture", CsI_Device, 24, CsArgc(&IDirect3DDevice9::CreateVolumeTexture), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 8, CsI_VolumeTexture, 0},
    {"CreateCubeTexture", CsI_Device, 25, CsArgc(&IDirect3DDevice9::CreateCubeTexture), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 6, CsI_CubeTexture, 0},
    {"CreateVertexBuffer", CsI_Device, 26, CsArgc(&IDirect3DDevice9::CreateVertexBuffer), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 5, CsI_VertexBuffer, 0},
    {"CreateIndexBuffer", CsI_Device, 27, CsArgc(&IDirect3DDevice9::CreateIndexBuffer), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 5, CsI_IndexBuffer, 0},
    {"CreateRenderTarget", CsI_Device, 28, CsArgc(&IDirect3DDevice9::CreateRenderTarget), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 7, CsI_Surface, 0},
    {"CreateDepthStencilSurface", CsI_Device, 29, CsArgc(&IDirect3DDevice9::CreateDepthStencilSurface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 7, CsI_Surface, 0},
    {"UpdateSurface", CsI_Device, 30, CsArgc(&IDirect3DDevice9::UpdateSurface), CsM_Async, {{2, CsP_Fixed, 16, 0}, {4, CsP_Fixed, 8, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"UpdateTexture", CsI_Device, 31, CsArgc(&IDirect3DDevice9::UpdateTexture), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetRenderTargetData", CsI_Device, 32, CsArgc(&IDirect3DDevice9::GetRenderTargetData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetFrontBufferData", CsI_Device, 33, CsArgc(&IDirect3DDevice9::GetFrontBufferData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"StretchRect", CsI_Device, 34, CsArgc(&IDirect3DDevice9::StretchRect), CsM_Async, {{2, CsP_Fixed, 16, 0}, {4, CsP_Fixed, 16, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"ColorFill", CsI_Device, 35, CsArgc(&IDirect3DDevice9::ColorFill), CsM_Async, {{2, CsP_Fixed, 16, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateOffscreenPlainSurface", CsI_Device, 36, CsArgc(&IDirect3DDevice9::CreateOffscreenPlainSurface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 5, CsI_Surface, 0},
    {"SetRenderTarget", CsI_Device, 37, CsArgc(&IDirect3DDevice9::SetRenderTarget), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetRenderTarget", CsI_Device, 38, CsArgc(&IDirect3DDevice9::GetRenderTarget), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_Surface, 0},
    {"SetDepthStencilSurface", CsI_Device, 39, CsArgc(&IDirect3DDevice9::SetDepthStencilSurface), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDepthStencilSurface", CsI_Device, 40, CsArgc(&IDirect3DDevice9::GetDepthStencilSurface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 1, CsI_Surface, 0},
    {"BeginScene", CsI_Device, 41, CsArgc(&IDirect3DDevice9::BeginScene), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"EndScene", CsI_Device, 42, CsArgc(&IDirect3DDevice9::EndScene), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Clear", CsI_Device, 43, CsArgc(&IDirect3DDevice9::Clear), CsM_Async, {{2, CsP_RectCount, 0, 1}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetTransform", CsI_Device, 44, CsArgc(&IDirect3DDevice9::SetTransform), CsM_Async, {{2, CsP_Fixed, 64, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetTransform", CsI_Device, 45, CsArgc(&IDirect3DDevice9::GetTransform), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"MultiplyTransform", CsI_Device, 46, CsArgc(&IDirect3DDevice9::MultiplyTransform), CsM_Async, {{2, CsP_Fixed, 64, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetViewport", CsI_Device, 47, CsArgc(&IDirect3DDevice9::SetViewport), CsM_Async, {{1, CsP_Fixed, 24, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetViewport", CsI_Device, 48, CsArgc(&IDirect3DDevice9::GetViewport), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetMaterial", CsI_Device, 49, CsArgc(&IDirect3DDevice9::SetMaterial), CsM_Async, {{1, CsP_Fixed, 68, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetMaterial", CsI_Device, 50, CsArgc(&IDirect3DDevice9::GetMaterial), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetLight", CsI_Device, 51, CsArgc(&IDirect3DDevice9::SetLight), CsM_Async, {{2, CsP_Fixed, 104, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLight", CsI_Device, 52, CsArgc(&IDirect3DDevice9::GetLight), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"LightEnable", CsI_Device, 53, CsArgc(&IDirect3DDevice9::LightEnable), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLightEnable", CsI_Device, 54, CsArgc(&IDirect3DDevice9::GetLightEnable), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetClipPlane", CsI_Device, 55, CsArgc(&IDirect3DDevice9::SetClipPlane), CsM_Async, {{2, CsP_Fixed, 16, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetClipPlane", CsI_Device, 56, CsArgc(&IDirect3DDevice9::GetClipPlane), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetRenderState", CsI_Device, 57, CsArgc(&IDirect3DDevice9::SetRenderState), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetRenderState", CsI_Device, 58, CsArgc(&IDirect3DDevice9::GetRenderState), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateStateBlock", CsI_Device, 59, CsArgc(&IDirect3DDevice9::CreateStateBlock), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_StateBlock, 0},
    {"BeginStateBlock", CsI_Device, 60, CsArgc(&IDirect3DDevice9::BeginStateBlock), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"EndStateBlock", CsI_Device, 61, CsArgc(&IDirect3DDevice9::EndStateBlock), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 1, CsI_StateBlock, 0},
    {"SetClipStatus", CsI_Device, 62, CsArgc(&IDirect3DDevice9::SetClipStatus), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetClipStatus", CsI_Device, 63, CsArgc(&IDirect3DDevice9::GetClipStatus), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetTexture", CsI_Device, 64, CsArgc(&IDirect3DDevice9::GetTexture), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_Count, 0},
    {"SetTexture", CsI_Device, 65, CsArgc(&IDirect3DDevice9::SetTexture), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetTextureStageState", CsI_Device, 66, CsArgc(&IDirect3DDevice9::GetTextureStageState), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetTextureStageState", CsI_Device, 67, CsArgc(&IDirect3DDevice9::SetTextureStageState), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetSamplerState", CsI_Device, 68, CsArgc(&IDirect3DDevice9::GetSamplerState), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetSamplerState", CsI_Device, 69, CsArgc(&IDirect3DDevice9::SetSamplerState), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"ValidateDevice", CsI_Device, 70, CsArgc(&IDirect3DDevice9::ValidateDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPaletteEntries", CsI_Device, 71, CsArgc(&IDirect3DDevice9::SetPaletteEntries), CsM_Async, {{2, CsP_Fixed, 1024, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPaletteEntries", CsI_Device, 72, CsArgc(&IDirect3DDevice9::GetPaletteEntries), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetCurrentTexturePalette", CsI_Device, 73, CsArgc(&IDirect3DDevice9::SetCurrentTexturePalette), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetCurrentTexturePalette", CsI_Device, 74, CsArgc(&IDirect3DDevice9::GetCurrentTexturePalette), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetScissorRect", CsI_Device, 75, CsArgc(&IDirect3DDevice9::SetScissorRect), CsM_Async, {{1, CsP_Fixed, 16, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetScissorRect", CsI_Device, 76, CsArgc(&IDirect3DDevice9::GetScissorRect), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetSoftwareVertexProcessing", CsI_Device, 77, CsArgc(&IDirect3DDevice9::SetSoftwareVertexProcessing), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetSoftwareVertexProcessing", CsI_Device, 78, CsArgc(&IDirect3DDevice9::GetSoftwareVertexProcessing), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetNPatchMode", CsI_Device, 79, CsArgc(&IDirect3DDevice9::SetNPatchMode), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    // GetNPatchMode is CsM_Pass: it is a getter the stream does not track, so it stays unhooked and the real device answers directly.
    {"GetNPatchMode", CsI_Device, 80, CsArgc(&IDirect3DDevice9::GetNPatchMode), CsM_Pass, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DrawPrimitive", CsI_Device, 81, CsArgc(&IDirect3DDevice9::DrawPrimitive), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DrawIndexedPrimitive", CsI_Device, 82, CsArgc(&IDirect3DDevice9::DrawIndexedPrimitive), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DrawPrimitiveUP", CsI_Device, 83, CsArgc(&IDirect3DDevice9::DrawPrimitiveUP), CsM_Async, {{3, CsP_UPVerts, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DrawIndexedPrimitiveUP", CsI_Device, 84, CsArgc(&IDirect3DDevice9::DrawIndexedPrimitiveUP), CsM_Async, {{5, CsP_UPIndices, 0, 4}, {7, CsP_UPVerts, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"ProcessVertices", CsI_Device, 85, CsArgc(&IDirect3DDevice9::ProcessVertices), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateVertexDeclaration", CsI_Device, 86, CsArgc(&IDirect3DDevice9::CreateVertexDeclaration), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_VertexDecl, 0},
    {"SetVertexDeclaration", CsI_Device, 87, CsArgc(&IDirect3DDevice9::SetVertexDeclaration), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetVertexDeclaration", CsI_Device, 88, CsArgc(&IDirect3DDevice9::GetVertexDeclaration), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 1, CsI_VertexDecl, 0},
    {"SetFVF", CsI_Device, 89, CsArgc(&IDirect3DDevice9::SetFVF), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetFVF", CsI_Device, 90, CsArgc(&IDirect3DDevice9::GetFVF), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateVertexShader", CsI_Device, 91, CsArgc(&IDirect3DDevice9::CreateVertexShader), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_VertexShader, 0},
    {"SetVertexShader", CsI_Device, 92, CsArgc(&IDirect3DDevice9::SetVertexShader), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetVertexShader", CsI_Device, 93, CsArgc(&IDirect3DDevice9::GetVertexShader), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 1, CsI_VertexShader, 0},
    {"SetVertexShaderConstantF", CsI_Device, 94, CsArgc(&IDirect3DDevice9::SetVertexShaderConstantF), CsM_Async, {{2, CsP_Vec4Count, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetVertexShaderConstantF", CsI_Device, 95, CsArgc(&IDirect3DDevice9::GetVertexShaderConstantF), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetVertexShaderConstantI", CsI_Device, 96, CsArgc(&IDirect3DDevice9::SetVertexShaderConstantI), CsM_Async, {{2, CsP_Vec4Count, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetVertexShaderConstantI", CsI_Device, 97, CsArgc(&IDirect3DDevice9::GetVertexShaderConstantI), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetVertexShaderConstantB", CsI_Device, 98, CsArgc(&IDirect3DDevice9::SetVertexShaderConstantB), CsM_Async, {{2, CsP_BoolCount, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetVertexShaderConstantB", CsI_Device, 99, CsArgc(&IDirect3DDevice9::GetVertexShaderConstantB), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetStreamSource", CsI_Device, 100, CsArgc(&IDirect3DDevice9::SetStreamSource), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetStreamSource", CsI_Device, 101, CsArgc(&IDirect3DDevice9::GetStreamSource), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_VertexBuffer, 0},
    {"SetStreamSourceFreq", CsI_Device, 102, CsArgc(&IDirect3DDevice9::SetStreamSourceFreq), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetStreamSourceFreq", CsI_Device, 103, CsArgc(&IDirect3DDevice9::GetStreamSourceFreq), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetIndices", CsI_Device, 104, CsArgc(&IDirect3DDevice9::SetIndices), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetIndices", CsI_Device, 105, CsArgc(&IDirect3DDevice9::GetIndices), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 1, CsI_IndexBuffer, 0},
    {"CreatePixelShader", CsI_Device, 106, CsArgc(&IDirect3DDevice9::CreatePixelShader), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_PixelShader, 0},
    {"SetPixelShader", CsI_Device, 107, CsArgc(&IDirect3DDevice9::SetPixelShader), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPixelShader", CsI_Device, 108, CsArgc(&IDirect3DDevice9::GetPixelShader), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 1, CsI_PixelShader, 0},
    {"SetPixelShaderConstantF", CsI_Device, 109, CsArgc(&IDirect3DDevice9::SetPixelShaderConstantF), CsM_Async, {{2, CsP_Vec4Count, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPixelShaderConstantF", CsI_Device, 110, CsArgc(&IDirect3DDevice9::GetPixelShaderConstantF), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPixelShaderConstantI", CsI_Device, 111, CsArgc(&IDirect3DDevice9::SetPixelShaderConstantI), CsM_Async, {{2, CsP_Vec4Count, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPixelShaderConstantI", CsI_Device, 112, CsArgc(&IDirect3DDevice9::GetPixelShaderConstantI), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPixelShaderConstantB", CsI_Device, 113, CsArgc(&IDirect3DDevice9::SetPixelShaderConstantB), CsM_Async, {{2, CsP_BoolCount, 0, 3}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPixelShaderConstantB", CsI_Device, 114, CsArgc(&IDirect3DDevice9::GetPixelShaderConstantB), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DrawRectPatch", CsI_Device, 115, CsArgc(&IDirect3DDevice9::DrawRectPatch), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DrawTriPatch", CsI_Device, 116, CsArgc(&IDirect3DDevice9::DrawTriPatch), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"DeletePatch", CsI_Device, 117, CsArgc(&IDirect3DDevice9::DeletePatch), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"CreateQuery", CsI_Device, 118, CsArgc(&IDirect3DDevice9::CreateQuery), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_Query, 0},
};
static_assert(sizeof(kCsDevice) / sizeof(kCsDevice[0]) == 119, "IDirect3DDevice9 slot count");
static_assert(CsSlotsInOrder(kCsDevice), "IDirect3DDevice9 slot order");

// IDirect3DVertexBuffer9: 14 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsVertexBuffer[] = {
    {"QueryInterface", CsI_VertexBuffer, 0, CsArgc(&IDirect3DVertexBuffer9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_VertexBuffer, 1, CsArgc(&IDirect3DVertexBuffer9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_VertexBuffer, 2, CsArgc(&IDirect3DVertexBuffer9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_VertexBuffer, 3, CsArgc(&IDirect3DVertexBuffer9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_VertexBuffer, 4, CsArgc(&IDirect3DVertexBuffer9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_VertexBuffer, 5, CsArgc(&IDirect3DVertexBuffer9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_VertexBuffer, 6, CsArgc(&IDirect3DVertexBuffer9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPriority", CsI_VertexBuffer, 7, CsArgc(&IDirect3DVertexBuffer9::SetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPriority", CsI_VertexBuffer, 8, CsArgc(&IDirect3DVertexBuffer9::GetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"PreLoad", CsI_VertexBuffer, 9, CsArgc(&IDirect3DVertexBuffer9::PreLoad), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_VertexBuffer, 10, CsArgc(&IDirect3DVertexBuffer9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Lock", CsI_VertexBuffer, 11, CsArgc(&IDirect3DVertexBuffer9::Lock), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 4},
    {"Unlock", CsI_VertexBuffer, 12, CsArgc(&IDirect3DVertexBuffer9::Unlock), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDesc", CsI_VertexBuffer, 13, CsArgc(&IDirect3DVertexBuffer9::GetDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsVertexBuffer) / sizeof(kCsVertexBuffer[0]) == 14, "IDirect3DVertexBuffer9 slot count");
static_assert(CsSlotsInOrder(kCsVertexBuffer), "IDirect3DVertexBuffer9 slot order");

// IDirect3DIndexBuffer9: 14 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsIndexBuffer[] = {
    {"QueryInterface", CsI_IndexBuffer, 0, CsArgc(&IDirect3DIndexBuffer9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_IndexBuffer, 1, CsArgc(&IDirect3DIndexBuffer9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_IndexBuffer, 2, CsArgc(&IDirect3DIndexBuffer9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_IndexBuffer, 3, CsArgc(&IDirect3DIndexBuffer9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_IndexBuffer, 4, CsArgc(&IDirect3DIndexBuffer9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_IndexBuffer, 5, CsArgc(&IDirect3DIndexBuffer9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_IndexBuffer, 6, CsArgc(&IDirect3DIndexBuffer9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPriority", CsI_IndexBuffer, 7, CsArgc(&IDirect3DIndexBuffer9::SetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPriority", CsI_IndexBuffer, 8, CsArgc(&IDirect3DIndexBuffer9::GetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"PreLoad", CsI_IndexBuffer, 9, CsArgc(&IDirect3DIndexBuffer9::PreLoad), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_IndexBuffer, 10, CsArgc(&IDirect3DIndexBuffer9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Lock", CsI_IndexBuffer, 11, CsArgc(&IDirect3DIndexBuffer9::Lock), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 4},
    {"Unlock", CsI_IndexBuffer, 12, CsArgc(&IDirect3DIndexBuffer9::Unlock), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDesc", CsI_IndexBuffer, 13, CsArgc(&IDirect3DIndexBuffer9::GetDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsIndexBuffer) / sizeof(kCsIndexBuffer[0]) == 14, "IDirect3DIndexBuffer9 slot count");
static_assert(CsSlotsInOrder(kCsIndexBuffer), "IDirect3DIndexBuffer9 slot order");

// IDirect3DTexture9: 22 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsTexture[] = {
    {"QueryInterface", CsI_Texture, 0, CsArgc(&IDirect3DTexture9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_Texture, 1, CsArgc(&IDirect3DTexture9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_Texture, 2, CsArgc(&IDirect3DTexture9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_Texture, 3, CsArgc(&IDirect3DTexture9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_Texture, 4, CsArgc(&IDirect3DTexture9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_Texture, 5, CsArgc(&IDirect3DTexture9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_Texture, 6, CsArgc(&IDirect3DTexture9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPriority", CsI_Texture, 7, CsArgc(&IDirect3DTexture9::SetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPriority", CsI_Texture, 8, CsArgc(&IDirect3DTexture9::GetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"PreLoad", CsI_Texture, 9, CsArgc(&IDirect3DTexture9::PreLoad), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_Texture, 10, CsArgc(&IDirect3DTexture9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetLOD", CsI_Texture, 11, CsArgc(&IDirect3DTexture9::SetLOD), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLOD", CsI_Texture, 12, CsArgc(&IDirect3DTexture9::GetLOD), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLevelCount", CsI_Texture, 13, CsArgc(&IDirect3DTexture9::GetLevelCount), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetAutoGenFilterType", CsI_Texture, 14, CsArgc(&IDirect3DTexture9::SetAutoGenFilterType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetAutoGenFilterType", CsI_Texture, 15, CsArgc(&IDirect3DTexture9::GetAutoGenFilterType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GenerateMipSubLevels", CsI_Texture, 16, CsArgc(&IDirect3DTexture9::GenerateMipSubLevels), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLevelDesc", CsI_Texture, 17, CsArgc(&IDirect3DTexture9::GetLevelDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetSurfaceLevel", CsI_Texture, 18, CsArgc(&IDirect3DTexture9::GetSurfaceLevel), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_Surface, 0},
    {"LockRect", CsI_Texture, 19, CsArgc(&IDirect3DTexture9::LockRect), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 4},
    {"UnlockRect", CsI_Texture, 20, CsArgc(&IDirect3DTexture9::UnlockRect), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddDirtyRect", CsI_Texture, 21, CsArgc(&IDirect3DTexture9::AddDirtyRect), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsTexture) / sizeof(kCsTexture[0]) == 22, "IDirect3DTexture9 slot count");
static_assert(CsSlotsInOrder(kCsTexture), "IDirect3DTexture9 slot order");

// IDirect3DCubeTexture9: 22 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsCubeTexture[] = {
    {"QueryInterface", CsI_CubeTexture, 0, CsArgc(&IDirect3DCubeTexture9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_CubeTexture, 1, CsArgc(&IDirect3DCubeTexture9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_CubeTexture, 2, CsArgc(&IDirect3DCubeTexture9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_CubeTexture, 3, CsArgc(&IDirect3DCubeTexture9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_CubeTexture, 4, CsArgc(&IDirect3DCubeTexture9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_CubeTexture, 5, CsArgc(&IDirect3DCubeTexture9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_CubeTexture, 6, CsArgc(&IDirect3DCubeTexture9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPriority", CsI_CubeTexture, 7, CsArgc(&IDirect3DCubeTexture9::SetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPriority", CsI_CubeTexture, 8, CsArgc(&IDirect3DCubeTexture9::GetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"PreLoad", CsI_CubeTexture, 9, CsArgc(&IDirect3DCubeTexture9::PreLoad), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_CubeTexture, 10, CsArgc(&IDirect3DCubeTexture9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetLOD", CsI_CubeTexture, 11, CsArgc(&IDirect3DCubeTexture9::SetLOD), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLOD", CsI_CubeTexture, 12, CsArgc(&IDirect3DCubeTexture9::GetLOD), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLevelCount", CsI_CubeTexture, 13, CsArgc(&IDirect3DCubeTexture9::GetLevelCount), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetAutoGenFilterType", CsI_CubeTexture, 14, CsArgc(&IDirect3DCubeTexture9::SetAutoGenFilterType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetAutoGenFilterType", CsI_CubeTexture, 15, CsArgc(&IDirect3DCubeTexture9::GetAutoGenFilterType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GenerateMipSubLevels", CsI_CubeTexture, 16, CsArgc(&IDirect3DCubeTexture9::GenerateMipSubLevels), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLevelDesc", CsI_CubeTexture, 17, CsArgc(&IDirect3DCubeTexture9::GetLevelDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetCubeMapSurface", CsI_CubeTexture, 18, CsArgc(&IDirect3DCubeTexture9::GetCubeMapSurface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 3, CsI_Surface, 0},
    {"LockRect", CsI_CubeTexture, 19, CsArgc(&IDirect3DCubeTexture9::LockRect), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 5},
    {"UnlockRect", CsI_CubeTexture, 20, CsArgc(&IDirect3DCubeTexture9::UnlockRect), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddDirtyRect", CsI_CubeTexture, 21, CsArgc(&IDirect3DCubeTexture9::AddDirtyRect), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsCubeTexture) / sizeof(kCsCubeTexture[0]) == 22, "IDirect3DCubeTexture9 slot count");
static_assert(CsSlotsInOrder(kCsCubeTexture), "IDirect3DCubeTexture9 slot order");

// IDirect3DVolumeTexture9: 22 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsVolumeTexture[] = {
    {"QueryInterface", CsI_VolumeTexture, 0, CsArgc(&IDirect3DVolumeTexture9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_VolumeTexture, 1, CsArgc(&IDirect3DVolumeTexture9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_VolumeTexture, 2, CsArgc(&IDirect3DVolumeTexture9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_VolumeTexture, 3, CsArgc(&IDirect3DVolumeTexture9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_VolumeTexture, 4, CsArgc(&IDirect3DVolumeTexture9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_VolumeTexture, 5, CsArgc(&IDirect3DVolumeTexture9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_VolumeTexture, 6, CsArgc(&IDirect3DVolumeTexture9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPriority", CsI_VolumeTexture, 7, CsArgc(&IDirect3DVolumeTexture9::SetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPriority", CsI_VolumeTexture, 8, CsArgc(&IDirect3DVolumeTexture9::GetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"PreLoad", CsI_VolumeTexture, 9, CsArgc(&IDirect3DVolumeTexture9::PreLoad), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_VolumeTexture, 10, CsArgc(&IDirect3DVolumeTexture9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetLOD", CsI_VolumeTexture, 11, CsArgc(&IDirect3DVolumeTexture9::SetLOD), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLOD", CsI_VolumeTexture, 12, CsArgc(&IDirect3DVolumeTexture9::GetLOD), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLevelCount", CsI_VolumeTexture, 13, CsArgc(&IDirect3DVolumeTexture9::GetLevelCount), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetAutoGenFilterType", CsI_VolumeTexture, 14, CsArgc(&IDirect3DVolumeTexture9::SetAutoGenFilterType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetAutoGenFilterType", CsI_VolumeTexture, 15, CsArgc(&IDirect3DVolumeTexture9::GetAutoGenFilterType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GenerateMipSubLevels", CsI_VolumeTexture, 16, CsArgc(&IDirect3DVolumeTexture9::GenerateMipSubLevels), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetLevelDesc", CsI_VolumeTexture, 17, CsArgc(&IDirect3DVolumeTexture9::GetLevelDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetVolumeLevel", CsI_VolumeTexture, 18, CsArgc(&IDirect3DVolumeTexture9::GetVolumeLevel), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 2, CsI_Volume, 0},
    {"LockBox", CsI_VolumeTexture, 19, CsArgc(&IDirect3DVolumeTexture9::LockBox), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 4},
    {"UnlockBox", CsI_VolumeTexture, 20, CsArgc(&IDirect3DVolumeTexture9::UnlockBox), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddDirtyBox", CsI_VolumeTexture, 21, CsArgc(&IDirect3DVolumeTexture9::AddDirtyBox), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsVolumeTexture) / sizeof(kCsVolumeTexture[0]) == 22, "IDirect3DVolumeTexture9 slot count");
static_assert(CsSlotsInOrder(kCsVolumeTexture), "IDirect3DVolumeTexture9 slot order");

// IDirect3DSurface9: 17 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsSurface[] = {
    {"QueryInterface", CsI_Surface, 0, CsArgc(&IDirect3DSurface9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_Surface, 1, CsArgc(&IDirect3DSurface9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_Surface, 2, CsArgc(&IDirect3DSurface9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_Surface, 3, CsArgc(&IDirect3DSurface9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_Surface, 4, CsArgc(&IDirect3DSurface9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_Surface, 5, CsArgc(&IDirect3DSurface9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_Surface, 6, CsArgc(&IDirect3DSurface9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPriority", CsI_Surface, 7, CsArgc(&IDirect3DSurface9::SetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPriority", CsI_Surface, 8, CsArgc(&IDirect3DSurface9::GetPriority), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"PreLoad", CsI_Surface, 9, CsArgc(&IDirect3DSurface9::PreLoad), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_Surface, 10, CsArgc(&IDirect3DSurface9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetContainer", CsI_Surface, 11, CsArgc(&IDirect3DSurface9::GetContainer), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDesc", CsI_Surface, 12, CsArgc(&IDirect3DSurface9::GetDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"LockRect", CsI_Surface, 13, CsArgc(&IDirect3DSurface9::LockRect), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 3},
    {"UnlockRect", CsI_Surface, 14, CsArgc(&IDirect3DSurface9::UnlockRect), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDC", CsI_Surface, 15, CsArgc(&IDirect3DSurface9::GetDC), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"ReleaseDC", CsI_Surface, 16, CsArgc(&IDirect3DSurface9::ReleaseDC), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsSurface) / sizeof(kCsSurface[0]) == 17, "IDirect3DSurface9 slot count");
static_assert(CsSlotsInOrder(kCsSurface), "IDirect3DSurface9 slot order");

// IDirect3DVolume9: 11 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsVolume[] = {
    {"QueryInterface", CsI_Volume, 0, CsArgc(&IDirect3DVolume9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_Volume, 1, CsArgc(&IDirect3DVolume9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_Volume, 2, CsArgc(&IDirect3DVolume9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_Volume, 3, CsArgc(&IDirect3DVolume9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"SetPrivateData", CsI_Volume, 4, CsArgc(&IDirect3DVolume9::SetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPrivateData", CsI_Volume, 5, CsArgc(&IDirect3DVolume9::GetPrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"FreePrivateData", CsI_Volume, 6, CsArgc(&IDirect3DVolume9::FreePrivateData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetContainer", CsI_Volume, 7, CsArgc(&IDirect3DVolume9::GetContainer), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDesc", CsI_Volume, 8, CsArgc(&IDirect3DVolume9::GetDesc), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"LockBox", CsI_Volume, 9, CsArgc(&IDirect3DVolume9::LockBox), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 3},
    {"UnlockBox", CsI_Volume, 10, CsArgc(&IDirect3DVolume9::UnlockBox), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsVolume) / sizeof(kCsVolume[0]) == 11, "IDirect3DVolume9 slot count");
static_assert(CsSlotsInOrder(kCsVolume), "IDirect3DVolume9 slot order");

// IDirect3DSwapChain9: 10 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsSwapChain[] = {
    {"QueryInterface", CsI_SwapChain, 0, CsArgc(&IDirect3DSwapChain9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_SwapChain, 1, CsArgc(&IDirect3DSwapChain9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_SwapChain, 2, CsArgc(&IDirect3DSwapChain9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Present", CsI_SwapChain, 3, CsArgc(&IDirect3DSwapChain9::Present), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetFrontBufferData", CsI_SwapChain, 4, CsArgc(&IDirect3DSwapChain9::GetFrontBufferData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetBackBuffer", CsI_SwapChain, 5, CsArgc(&IDirect3DSwapChain9::GetBackBuffer), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 3, CsI_Surface, 0},
    {"GetRasterStatus", CsI_SwapChain, 6, CsArgc(&IDirect3DSwapChain9::GetRasterStatus), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDisplayMode", CsI_SwapChain, 7, CsArgc(&IDirect3DSwapChain9::GetDisplayMode), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_SwapChain, 8, CsArgc(&IDirect3DSwapChain9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetPresentParameters", CsI_SwapChain, 9, CsArgc(&IDirect3DSwapChain9::GetPresentParameters), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsSwapChain) / sizeof(kCsSwapChain[0]) == 10, "IDirect3DSwapChain9 slot count");
static_assert(CsSlotsInOrder(kCsSwapChain), "IDirect3DSwapChain9 slot order");

// IDirect3DVertexShader9: 5 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsVertexShader[] = {
    {"QueryInterface", CsI_VertexShader, 0, CsArgc(&IDirect3DVertexShader9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_VertexShader, 1, CsArgc(&IDirect3DVertexShader9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_VertexShader, 2, CsArgc(&IDirect3DVertexShader9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_VertexShader, 3, CsArgc(&IDirect3DVertexShader9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetFunction", CsI_VertexShader, 4, CsArgc(&IDirect3DVertexShader9::GetFunction), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsVertexShader) / sizeof(kCsVertexShader[0]) == 5, "IDirect3DVertexShader9 slot count");
static_assert(CsSlotsInOrder(kCsVertexShader), "IDirect3DVertexShader9 slot order");

// IDirect3DPixelShader9: 5 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsPixelShader[] = {
    {"QueryInterface", CsI_PixelShader, 0, CsArgc(&IDirect3DPixelShader9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_PixelShader, 1, CsArgc(&IDirect3DPixelShader9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_PixelShader, 2, CsArgc(&IDirect3DPixelShader9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_PixelShader, 3, CsArgc(&IDirect3DPixelShader9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetFunction", CsI_PixelShader, 4, CsArgc(&IDirect3DPixelShader9::GetFunction), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsPixelShader) / sizeof(kCsPixelShader[0]) == 5, "IDirect3DPixelShader9 slot count");
static_assert(CsSlotsInOrder(kCsPixelShader), "IDirect3DPixelShader9 slot order");

// IDirect3DVertexDeclaration9: 5 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsVertexDecl[] = {
    {"QueryInterface", CsI_VertexDecl, 0, CsArgc(&IDirect3DVertexDeclaration9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_VertexDecl, 1, CsArgc(&IDirect3DVertexDeclaration9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_VertexDecl, 2, CsArgc(&IDirect3DVertexDeclaration9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_VertexDecl, 3, CsArgc(&IDirect3DVertexDeclaration9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDeclaration", CsI_VertexDecl, 4, CsArgc(&IDirect3DVertexDeclaration9::GetDeclaration), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsVertexDecl) / sizeof(kCsVertexDecl[0]) == 5, "IDirect3DVertexDeclaration9 slot count");
static_assert(CsSlotsInOrder(kCsVertexDecl), "IDirect3DVertexDeclaration9 slot order");

// IDirect3DStateBlock9: 6 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsStateBlock[] = {
    {"QueryInterface", CsI_StateBlock, 0, CsArgc(&IDirect3DStateBlock9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_StateBlock, 1, CsArgc(&IDirect3DStateBlock9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_StateBlock, 2, CsArgc(&IDirect3DStateBlock9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_StateBlock, 3, CsArgc(&IDirect3DStateBlock9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Capture", CsI_StateBlock, 4, CsArgc(&IDirect3DStateBlock9::Capture), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Apply", CsI_StateBlock, 5, CsArgc(&IDirect3DStateBlock9::Apply), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsStateBlock) / sizeof(kCsStateBlock[0]) == 6, "IDirect3DStateBlock9 slot count");
static_assert(CsSlotsInOrder(kCsStateBlock), "IDirect3DStateBlock9 slot order");

// IDirect3DQuery9: 8 slots (IUnknown 0..2 included), SDK declaration order.
inline constexpr CsMethod kCsQuery[] = {
    {"QueryInterface", CsI_Query, 0, CsArgc(&IDirect3DQuery9::QueryInterface), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"AddRef", CsI_Query, 1, CsArgc(&IDirect3DQuery9::AddRef), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Release", CsI_Query, 2, CsArgc(&IDirect3DQuery9::Release), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDevice", CsI_Query, 3, CsArgc(&IDirect3DQuery9::GetDevice), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetType", CsI_Query, 4, CsArgc(&IDirect3DQuery9::GetType), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetDataSize", CsI_Query, 5, CsArgc(&IDirect3DQuery9::GetDataSize), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"Issue", CsI_Query, 6, CsArgc(&IDirect3DQuery9::Issue), CsM_Async, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
    {"GetData", CsI_Query, 7, CsArgc(&IDirect3DQuery9::GetData), CsM_Block, {{0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}, {0, CsP_None, 0, 0}}, 0, CsI_Device, 0},
};
static_assert(sizeof(kCsQuery) / sizeof(kCsQuery[0]) == 8, "IDirect3DQuery9 slot count");
static_assert(CsSlotsInOrder(kCsQuery), "IDirect3DQuery9 slot order");

inline const CsMethod* CsMethodTable(CsIface iface, size_t& count) {
    switch (iface) {
    case CsI_Device: count = sizeof(kCsDevice) / sizeof(kCsDevice[0]); return kCsDevice;
    case CsI_VertexBuffer: count = sizeof(kCsVertexBuffer) / sizeof(kCsVertexBuffer[0]); return kCsVertexBuffer;
    case CsI_IndexBuffer: count = sizeof(kCsIndexBuffer) / sizeof(kCsIndexBuffer[0]); return kCsIndexBuffer;
    case CsI_Texture: count = sizeof(kCsTexture) / sizeof(kCsTexture[0]); return kCsTexture;
    case CsI_CubeTexture: count = sizeof(kCsCubeTexture) / sizeof(kCsCubeTexture[0]); return kCsCubeTexture;
    case CsI_VolumeTexture: count = sizeof(kCsVolumeTexture) / sizeof(kCsVolumeTexture[0]); return kCsVolumeTexture;
    case CsI_Surface: count = sizeof(kCsSurface) / sizeof(kCsSurface[0]); return kCsSurface;
    case CsI_Volume: count = sizeof(kCsVolume) / sizeof(kCsVolume[0]); return kCsVolume;
    case CsI_SwapChain: count = sizeof(kCsSwapChain) / sizeof(kCsSwapChain[0]); return kCsSwapChain;
    case CsI_VertexShader: count = sizeof(kCsVertexShader) / sizeof(kCsVertexShader[0]); return kCsVertexShader;
    case CsI_PixelShader: count = sizeof(kCsPixelShader) / sizeof(kCsPixelShader[0]); return kCsPixelShader;
    case CsI_VertexDecl: count = sizeof(kCsVertexDecl) / sizeof(kCsVertexDecl[0]); return kCsVertexDecl;
    case CsI_StateBlock: count = sizeof(kCsStateBlock) / sizeof(kCsStateBlock[0]); return kCsStateBlock;
    case CsI_Query: count = sizeof(kCsQuery) / sizeof(kCsQuery[0]); return kCsQuery;
    default: count = 0; return nullptr;
    }
}
struct CsSlot {
    const CsMethod* method;
    void* orig;
};
