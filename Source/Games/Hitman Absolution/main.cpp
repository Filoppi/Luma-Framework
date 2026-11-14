///////////////////////////////////////////////////////////////////////
// Of course, before any of this, create a new project for your game!
// - Copy "Templates\VisualStudio\Luma-Template.zip" into "%USERPROFILE%\Documents\Visual Studio [current version]\Templates\ProjectTemplates" (and restart Visual Studio).
// - Right click "Games" in the solution explorer, Add -> New Project... -> Template (Luma Template Project for new games)
// - Make sure the location is "Source\Games"!
///////////////////////////////////////////////////////////////////////

// Specify a define with the name of the game here (GAME_*).
// This is optional but can be used to hardcode custom (per game) behaviours in the core library,
// or other reasons. We can't automate it through the project define system (based on the project name) because
// we need these defines to be upper case and space free.
#define GAME_HITMAN_ABSOLUTION 1
#define ENABLE_BLOOM 1 // core registers the Bloom VS/Prefilter/Downsample/Upsample passes used by DrawBloom (Luma Bloom setting)
#define ENABLE_SMAA 1 // core registers the SMAA passes used by DrawSMAA (AntiAliasingMode setting, replaces the game's FXAA)
#if defined(_M_IX86) && !defined(LUMA_SEGMENTED_TRACE)
#define LUMA_SEGMENTED_TRACE 1
#endif

//// Define all the global "core" defines before including its files: ////

// Enable support for Geometry Shaders (GS), rather rare.
#define GEOMETRY_SHADER_SUPPORT 0

// Enable to stop "You can now attach the debugger" popup.
#define DISABLE_AUTO_DEBUGGER 0

// DLSS/FSR are toggled via the "Luma Properties" page in the project properties (UseLumaNGX/UseLumaFSR).

// Instead of "manually" including the "core" library, we simply include its main code file (which is a header).
// The library in itself is standalone, as in, it compiles fine and could directly be used as a template addon etc if built as dll but,
// there's a major limitation in how libraries dependencies work by nature, and that is that you can only make
// one version of them for all other projects to use. For performance and tidiness reasons, we are interested in
// having global defines that can be turned on and off per game, as opposed to runtime (static) parameters.
// Hence why we specify the global defines before including the core Luma file (where near all of the generic Luma implementation is).
// If we wanted to use a library, we'd also need to add a core "main" definition in a cpp file, to link it properly.
// All externs that are currently defined in core would also need to be manually defined in each game's implementation (e.g. see "RESHADE_EXTERNS" above).
// The only disadvantage of not actually including the core as a library, is that we'll have to add the same include/library dependencies in our
// game project (e.g. add ReShade, DLSS, etc), and manually add all cpp files too.
//
// To compile in different modes (e.g. "DEVELOPMENT", "TEST" etc see "global_defines.h").
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system("start ...") hangs the render thread in exclusive fullscreen)
#include "HMAFlare.hpp"
#include "HardPause.hpp"
#include "BackgroundInput.hpp"
#include "gtao.h"

static bool g_pause_hotkey = true;
#if DEVELOPMENT
static bool g_force_borderless = true;
static bool g_always_on_top = false;
static bool g_pause_on_focus_loss = false;
static bool g_allow_background_controls = true;
static HWND g_saved_window = nullptr;
static LONG_PTR g_saved_style = 0, g_saved_ex_style = 0;
static WINDOWPLACEMENT g_saved_placement = { sizeof(WINDOWPLACEMENT) };
static bool g_borderless_applied = false;
static bool g_topmost_applied = false;

static void UpdateWindowControls()
{
   if (!game_window || !IsWindow(game_window)) return;
   if (g_saved_window != game_window)
   {
      g_saved_window = game_window;
      g_borderless_applied = false;
      g_topmost_applied = false;
   }
   if (g_force_borderless && !IsIconic(game_window))
   {
      if (!g_borderless_applied)
      {
         g_saved_style = GetWindowLongPtr(game_window, GWL_STYLE);
         g_saved_ex_style = GetWindowLongPtr(game_window, GWL_EXSTYLE);
         g_saved_placement.length = sizeof(g_saved_placement);
         if (!GetWindowPlacement(game_window, &g_saved_placement)) return;
         g_borderless_applied = true;
      }
      MONITORINFO mi = { sizeof(mi) };
      RECT rect = {};
      if (GetMonitorInfo(MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST), &mi) && GetWindowRect(game_window, &rect))
      {
         LONG_PTR style = GetWindowLongPtr(game_window, GWL_STYLE);
         LONG_PTR ex_style = GetWindowLongPtr(game_window, GWL_EXSTYLE);
         const LONG_PTR borders = WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME;
         const LONG_PTR edges = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
         if ((style & borders) || (ex_style & edges) || !EqualRect(&rect, &mi.rcMonitor))
         {
            SetWindowLongPtr(game_window, GWL_STYLE, (style & ~borders) | WS_POPUP);
            SetWindowLongPtr(game_window, GWL_EXSTYLE, ex_style & ~edges);
            SetWindowPos(game_window, nullptr, mi.rcMonitor.left, mi.rcMonitor.top,
               mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
         }
      }
   }
   else if (!g_force_borderless && g_borderless_applied && !IsIconic(game_window))
   {
      const LONG_PTR frame = WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME | WS_POPUP;
      const LONG_PTR edges = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
      LONG_PTR style = GetWindowLongPtr(game_window, GWL_STYLE);
      LONG_PTR ex_style = GetWindowLongPtr(game_window, GWL_EXSTYLE);
      SetWindowLongPtr(game_window, GWL_STYLE, (style & ~frame) | (g_saved_style & frame));
      SetWindowLongPtr(game_window, GWL_EXSTYLE, (ex_style & ~edges) | (g_saved_ex_style & edges));
      g_saved_placement.flags |= WPF_ASYNCWINDOWPLACEMENT;
      SetWindowPlacement(game_window, &g_saved_placement);
      SetWindowPos(game_window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
      g_borderless_applied = false;
   }
   if (g_always_on_top != g_topmost_applied)
   {
      if (SetWindowPos(game_window, g_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS))
         g_topmost_applied = g_always_on_top;
   }
}
#endif

// Forces the game window to a borderless popup covering the whole monitor.
static void ForceBorderlessFullscreen()
{
   HMONITOR h_monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
   MONITORINFO mi = {};
   mi.cbSize = sizeof(mi);
   if (!GetMonitorInfo(h_monitor, &mi))
      return;
   const RECT& rc_monitor = mi.rcMonitor;

   LONG_PTR style = GetWindowLongPtr(game_window, GWL_STYLE);
   style &= ~(WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME);
   style |= WS_POPUP | WS_VISIBLE;
   SetWindowLongPtr(game_window, GWL_STYLE, style);

   LONG_PTR ex_style = GetWindowLongPtr(game_window, GWL_EXSTYLE);
   ex_style &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
   SetWindowLongPtr(game_window, GWL_EXSTYLE, ex_style);

   SetWindowPos(game_window, nullptr,
      rc_monitor.left, rc_monitor.top,
      rc_monitor.right - rc_monitor.left, rc_monitor.bottom - rc_monitor.top,
      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
}
// Luma bloom: pyramid mip 0 is bound at kLumaBloomSlot for the tonemap draw. One sigma per mip, in mip texels (resolution-independent). Copied from Mass Effect.
static constexpr uint32_t kLumaBloomSlot = 4;
// GPU auto-exposure: the exposure texture is bound at t7 for the draws in kExposureBindHashes. The histogram runs on the scene of kExposureSourceHash(2).
static constexpr uint32_t kExposureSlot = 7;
static constexpr uint32_t kExposureSourceHash = 0x8C21ED83;
static constexpr uint32_t kExposureSourceHash2 = 0xA439D28A;
static constexpr uint32_t kExposureBindHashes[] = { 0x8C21ED83, 0xA6E2CE1F, 0x2B2B1728, 0x46C50FC2, 0xEED9AC77 };
static constexpr uint32_t kTonemapHash = 0x46C50FC2;
static constexpr uint32_t kTonemapHash2 = 0xEED9AC77; // Chinatown variant (skips LUT when depth coord > 1)
static constexpr uint32_t kFogHash = 0x49154D1F; // Chinatown fullscreen fog, blended onto the scene RT
static constexpr uint32_t kFogDensityHashes[] = { 0xC62DB387, 0x8B9419D7 }; // Half-res fog density RT, copied for the AO consumers
static constexpr uint32_t kFinalPostHash = 0x53909A5F; // Final post pass: reads the modern lens flare result at t3
static constexpr uint32_t kHurtCompositeHash = 0xAB9D887F; // Hurt composite: reads the modern lens flare result at t5
// Settings "FlareIntensity", "FlareThreshold", "FlareKnee", "ModernGlare", "GlareIntensity" (used with "ModernLensFlares").
static const HMAFlareSettings g_flare_defaults = [] {
   HMAFlareSettings settings = {};
#if !DEVELOPMENT && !TEST
   settings.threshold = 0.372f;
   settings.glare = true;
   settings.glare_intensity = 0.055f;
#endif
   return settings;
}();
static HMAFlareSettings g_flare_settings = g_flare_defaults;
static constexpr float g_bloom_sigmas[] = {1.5f, 2.f, 2.f, 2.f, 1.f, 0.5f};
constexpr uint32_t TONEMAP_TYPE_HASH = char_ptr_crc32("TONEMAP_TYPE");

// Setting "TonemappedResolve": replaces the hardware MSAA resolve of the 4x fp16 scene targets with a tonemapped (weighted) resolve.
// 0 = off (HW resolve), 1 = standard sample layout, 2 = 2x2 grid layout (some drivers expose the 4x MS surface as a 2x2 grid in sample 0).
static uint32_t g_tonemapped_resolve = 2u;

// Setting "GBufferClamp": vanilla's deferred G-buffers were 8-bit UNORM, so the game never wrote values outside 0..1. Luma's fp16 upgrade keeps them, so the G-buffers are clamped once after the fill.
static uint32_t g_gbuffer_clamp = 1u;
// MCP counter of G-buffer clamps
static uint32_t g_gbuffer_clamps = 0u;
// Set when the G-buffer fill (RT0..2 fp16, RT3 R32F) was drawn, cleared once the clamp ran
static bool g_gbuffer_dirty = false;
static ComPtr<ID3D11Resource> g_gbuffer_resources[3];
static ComPtr<ID3D11Texture2D> g_gbuffer_textures[3];

// Setting "AntiAliasingMode": 0 = vanilla FXAA, 1 = Luma SMAA (replaces the game's FXAA pixel shader draw).
static uint32_t g_anti_aliasing_mode = 1u;
static constexpr uint32_t kFXAAHash = 0x1B00C911;

// Settings "EnableXeGTAO", "XeGTAORadius" (meters), "XeGTAOIntensity" (final visibility exponent): XeGTAO replaces the SSAO draw kSSAOHash (its result goes into RT0).
// Quality is the advanced "XE_GTAO_QUALITY" define.
static bool g_enable_xegtao = true;
static float g_xegtao_radius = 10.f;
static float g_xegtao_intensity = 2.2f;
// "XeGTAOMode" (0 GTAO, 1 GT-VBAO, the visibility bitmask main pass) and "XeGTAOThickness" (GT-VBAO only, meters: the depth surfaces are this thick)
static bool g_xegtao_vbao = false;
static float g_xegtao_thickness = 5.f;
// XeGTAO path counters (MCP only, incremented on the render thread)
static uint32_t g_xegtao_fullres_runs = 0;
static uint32_t g_xegtao_halfres_runs = 0;
static uint32_t g_xegtao_failures = 0;
static uint32_t g_xegtao_t5_binds = 0;
static constexpr uint32_t kSSAOHash = 0xF6310D28;
// XeGTAO full res: the depth captured from PS 0x7BB58B6E (the draw just before the SSAO), and the AO consumer PS that reads the AO at t5.
static constexpr uint32_t kFullResDepthHash = 0x7BB58B6E;
static constexpr uint32_t kDepthDownsampleHash = 0x16B01762;
static constexpr uint32_t kAOConsumerHash = 0x3994DDCA;

static ID3D11ShaderResourceView* HMA_GetExposureSRV(DeviceData& device_data); // Defined after TemplateGameDeviceData
static bool HMA_ExposureReady(DeviceData& device_data, uint32_t mode); // Defined after TemplateGameDeviceData

static bool HMA_OnResolveTextureRegion(
   reshade::api::command_list* cmd_list,
   reshade::api::resource source,
   uint32_t source_subresource,
   const reshade::api::subresource_box* source_box,
   reshade::api::resource dest,
   uint32_t dest_subresource,
   uint32_t dest_x,
   uint32_t dest_y,
   uint32_t dest_z,
   reshade::api::format format)
{
   if (g_tonemapped_resolve == 0u || source.handle == 0 || dest.handle == 0)
      return false;
   if (source_box != nullptr || source_subresource != 0 || dest_subresource != 0 || dest_x != 0 || dest_y != 0 || dest_z != 0)
      return false;

   ComPtr<ID3D11Texture2D> src_tex;
   ComPtr<ID3D11Texture2D> dst_tex;
   if (FAILED(reinterpret_cast<ID3D11Resource*>(source.handle)->QueryInterface(src_tex.put())))
      return false;
   if (FAILED(reinterpret_cast<ID3D11Resource*>(dest.handle)->QueryInterface(dst_tex.put())))
      return false;

   D3D11_TEXTURE2D_DESC src_desc = {};
   D3D11_TEXTURE2D_DESC dst_desc = {};
   src_tex->GetDesc(&src_desc);
   dst_tex->GetDesc(&dst_desc);
   if (src_desc.SampleDesc.Count != 4 || dst_desc.SampleDesc.Count != 1)
      return false;
   if (src_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || dst_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return false;
   if (src_desc.Width != dst_desc.Width || src_desc.Height != dst_desc.Height)
      return false;
   if (!(src_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) || !(dst_desc.BindFlags & D3D11_BIND_RENDER_TARGET))
      return false;

   if (cmd_list == nullptr)
      return false;
   auto* device = cmd_list->get_device();
   if (device == nullptr)
      return false;
   auto* device_data_ptr = device->get_private_data<DeviceData>();
   if (device_data_ptr == nullptr)
      return false;
   auto& device_data = *device_data_ptr;

   ID3D11Device* native_device = reinterpret_cast<ID3D11Device*>(device->get_native());
   if (native_device == nullptr)
      return false;

   ComPtr<ID3D11DeviceContext> native_device_context;
   if (FAILED(reinterpret_cast<ID3D11DeviceChild*>(cmd_list->get_native())->QueryInterface(native_device_context.put())))
      return false;

   auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const bool use_2x2 = g_tonemapped_resolve == 2u;
   auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash(use_2x2 ? "HMA Tonemapped Resolve 2x2" : "HMA Tonemapped Resolve"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second.get()
      || ps_it == device_data.native_pixel_shaders.end() || !ps_it->second.get())
      return false;

#if DEVELOPMENT
   // Logged once per source texture
   static std::unordered_set<ID3D11Texture2D*> logged_resolve_sources;
   if (logged_resolve_sources.insert(src_tex.get()).second)
   {
      char msg[256];
      snprintf(msg, sizeof(msg), "HMA tonemapped resolve: source SampleDesc.Quality=%u, mode=%u (%s)", src_desc.SampleDesc.Quality, g_tonemapped_resolve, use_2x2 ? "2x2 grid" : "standard");
      reshade::log::message(reshade::log::level::info, msg);
   }
#endif

   ComPtr<ID3D11ShaderResourceView> srv_ms;
   if (FAILED(native_device->CreateShaderResourceView(src_tex.get(), nullptr, srv_ms.put())))
      return false;
   ComPtr<ID3D11RenderTargetView> rtv;
   if (FAILED(native_device->CreateRenderTargetView(dst_tex.get(), nullptr, rtv.put())))
      return false;

   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   draw_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);

   // Luma auto-exposure meter at t1 (only used by the tonemaps when auto exposure is on, otherwise the shader falls back to E = 1)
   const uint32_t tonemap_type = GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH);
   const bool exposure_ready = cb_luma_global_settings.GameSettings.AutoExposureMode != 0u
      && HMA_ExposureReady(device_data, cb_luma_global_settings.GameSettings.AutoExposureMode);
   ID3D11ShaderResourceView* srv_exposure = tonemap_type >= 1 && exposure_ready ? HMA_GetExposureSRV(device_data) : nullptr;
   native_device_context->PSSetShaderResources(1, 1, &srv_exposure);

   // Full-screen draw into the (non-MS) destination, reading the MS source with Load()
   DrawCustomPixelShader(native_device_context.get(), device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
      vs_it->second.get(), ps_it->second.get(), srv_ms.get(), rtv.get(), dst_desc.Width, dst_desc.Height, false, "HMA Tonemapped Resolve");

   draw_state_stack.Restore(native_device_context.get());
   return true;
}

// Gets the resource and 2D texture of a RTV (the caller keeps the RTV alive). Returns false if it's null or not a 2D texture.
static bool HMA_GetRTVTexture(ID3D11RenderTargetView* rtv, ComPtr<ID3D11Resource>& resource, ComPtr<ID3D11Texture2D>& texture, D3D11_TEXTURE2D_DESC& desc)
{
   if (rtv == nullptr)
      return false;
   rtv->GetResource(resource.put());
   if (!resource)
      return false;
   if (FAILED(resource->QueryInterface(texture.put())))
      return false;
   texture->GetDesc(&desc);
   return true;
}

// Clamps the 3 stored G-buffer textures to 0..1 with two full-screen passes: MAX blend with 0 (negatives to 0), then MIN blend with 1 (values above 1 to 1).
// Policy: this global pass is the authoritative G-buffer clamp. It runs once after the deferred fill, before the first draw that reads it, restoring the
// vanilla 8-bit UNORM range that Luma's fp16 upgrade removed. The per-shader GBUFFER_CLAMP/saturate clamps in the DeferredLight variants (and their
// SAFE_MIN/SAFE_W divide guards) are intentionally redundant belt-and-suspenders: they are no-ops once this pass has run,
// but they enforce the 0..1 range locally in each faithful translation, where it also documents the deviation from vanilla
// math. Do not remove either side without checking both.
static void HMA_ClampGBuffers(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
{
   auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   auto zero_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("HMA GBuffer Clamp Zero"));
   auto one_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("HMA GBuffer Clamp One"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second.get()
      || zero_it == device_data.native_pixel_shaders.end() || !zero_it->second.get()
      || one_it == device_data.native_pixel_shaders.end() || !one_it->second.get())
      return;

   D3D11_TEXTURE2D_DESC desc = {};
   g_gbuffer_textures[0]->GetDesc(&desc);

   ComPtr<ID3D11RenderTargetView> rtvs[3];
   ID3D11RenderTargetView* rtv_ptrs[3] = {};
   for (UINT i = 0; i < 3; i++)
   {
      if (FAILED(native_device->CreateRenderTargetView(g_gbuffer_resources[i].get(), nullptr, rtvs[i].put())))
         return;
      rtv_ptrs[i] = rtvs[i].get();
   }

   // Blend states are created once per device
   static ComPtr<ID3D11BlendState> blend_max;
   static ComPtr<ID3D11BlendState> blend_min;
   static ID3D11Device* blend_device = nullptr;
   if (blend_device != native_device)
   {
      D3D11_BLEND_DESC blend_desc = {};
      blend_desc.IndependentBlendEnable = FALSE;
      D3D11_RENDER_TARGET_BLEND_DESC& rt_desc = blend_desc.RenderTarget[0];
      rt_desc.BlendEnable = TRUE;
      rt_desc.SrcBlend = D3D11_BLEND_ONE;
      rt_desc.DestBlend = D3D11_BLEND_ONE;
      rt_desc.SrcBlendAlpha = D3D11_BLEND_ONE;
      rt_desc.DestBlendAlpha = D3D11_BLEND_ONE;
      rt_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      rt_desc.BlendOp = D3D11_BLEND_OP_MAX;
      rt_desc.BlendOpAlpha = D3D11_BLEND_OP_MAX;
      if (FAILED(native_device->CreateBlendState(&blend_desc, blend_max.put())))
         return;
      rt_desc.BlendOp = D3D11_BLEND_OP_MIN;
      rt_desc.BlendOpAlpha = D3D11_BLEND_OP_MIN;
      if (FAILED(native_device->CreateBlendState(&blend_desc, blend_min.put())))
         return;
      blend_device = native_device;
   }

   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

   constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
   D3D11_VIEWPORT viewport = {};
   viewport.Width = static_cast<FLOAT>(desc.Width);
   viewport.Height = static_cast<FLOAT>(desc.Height);
   viewport.MinDepth = 0.f;
   viewport.MaxDepth = 1.f;

   native_device_context->OMSetRenderTargets(3, rtv_ptrs, nullptr);
   native_device_context->RSSetViewports(1, &viewport);
   native_device_context->RSSetScissorRects(0, nullptr);
   native_device_context->RSSetState(nullptr);
   native_device_context->OMSetDepthStencilState(device_data.default_depth_stencil_state.get(), 0);
   native_device_context->IASetInputLayout(nullptr);
   native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   native_device_context->VSSetShader(vs_it->second.get(), nullptr, 0);

   // Pass A: MAX with a zero source clamps negatives to 0
   native_device_context->OMSetBlendState(blend_max.get(), blend_factor, 0xFFFFFFFF);
   native_device_context->PSSetShader(zero_it->second.get(), nullptr, 0);
   native_device_context->Draw(4, 0);
   Trace::CustomDraw(native_device_context, "HMA GBuffer Clamp", 4);

   // Pass B: MIN with a one source clamps values above 1 to 1
   native_device_context->OMSetBlendState(blend_min.get(), blend_factor, 0xFFFFFFFF);
   native_device_context->PSSetShader(one_it->second.get(), nullptr, 0);
   native_device_context->Draw(4, 0);
   Trace::CustomDraw(native_device_context, "HMA GBuffer Clamp", 4);

   draw_state_stack.Restore(native_device_context);
   g_gbuffer_clamps++;
}

// Replaces the game's FXAA draw (the scene in SRV0, output in RTV0) with Luma SMAA. Returns false on any failure, so the original draw runs.
static bool HMA_DrawSMAAForFXAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
{
   // SRV0 is the scene in linear HDR.
   ComPtr<ID3D11ShaderResourceView> srv_scene;
   native_device_context->PSGetShaderResources(0, 1, srv_scene.put());
   if (!srv_scene)
      return false;

   // RTV0 is the FXAA output.
   ComPtr<ID3D11RenderTargetView> rtv_output;
   native_device_context->OMGetRenderTargets(1, rtv_output.put(), nullptr);
   if (!rtv_output)
      return false;

   auto cs_it = device_data.native_compute_shaders.find(CompileTimeStringHash("SMAA Linear To sRGB CS"));
   if (cs_it == device_data.native_compute_shaders.end() || !cs_it->second.get())
      return false;

   ComPtr<ID3D11Resource> scene_resource;
   srv_scene->GetResource(scene_resource.put());
   if (!scene_resource)
      return false;
   ComPtr<ID3D11Texture2D> scene_tex;
   if (FAILED(scene_resource->QueryInterface(scene_tex.put())))
      return false;
   D3D11_TEXTURE2D_DESC tex_desc = {};
   scene_tex->GetDesc(&tex_desc);
   if (tex_desc.Width == 0 || tex_desc.Height == 0 || tex_desc.SampleDesc.Count != 1)
      return false;

   // Create all resources before binding anything, so early failures leave the state untouched.
   tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
   ComPtr<ID3D11Texture2D> tex_copy;
   ComPtr<ID3D11Texture2D> tex_gamma;
   if (FAILED(native_device->CreateTexture2D(&tex_desc, nullptr, tex_copy.put())) || FAILED(native_device->CreateTexture2D(&tex_desc, nullptr, tex_gamma.put())))
      return false;
   ComPtr<ID3D11UnorderedAccessView> uav_copy;
   ComPtr<ID3D11UnorderedAccessView> uav_gamma;
   ComPtr<ID3D11ShaderResourceView> srv_copy;
   ComPtr<ID3D11ShaderResourceView> srv_gamma;
   if (FAILED(native_device->CreateUnorderedAccessView(tex_copy.get(), nullptr, uav_copy.put()))
      || FAILED(native_device->CreateUnorderedAccessView(tex_gamma.get(), nullptr, uav_gamma.put()))
      || FAILED(native_device->CreateShaderResourceView(tex_copy.get(), nullptr, srv_copy.put()))
      || FAILED(native_device->CreateShaderResourceView(tex_gamma.get(), nullptr, srv_gamma.put())))
      return false;

   // SMAA metrics (1/size, size) for DrawSMAA's b1 cbuffer.
   const float smaa_metrics[4] = { 1.f / (float)tex_desc.Width, 1.f / (float)tex_desc.Height, (float)tex_desc.Width, (float)tex_desc.Height };
   D3D11_BUFFER_DESC cb_desc = {};
   cb_desc.ByteWidth = sizeof(smaa_metrics);
   cb_desc.Usage = D3D11_USAGE_IMMUTABLE;
   cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
   D3D11_SUBRESOURCE_DATA cb_data = {};
   cb_data.pSysMem = smaa_metrics;
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   if (FAILED(native_device->CreateBuffer(&cb_desc, &cb_data, cb_smaa_metrics.put())))
      return false;

   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

   // Linear to sRGB pass: reads the scene, writes a linear copy (uav0) and an sRGB copy (uav1).
   ID3D11ShaderResourceView* srv_null = nullptr;
   native_device_context->PSSetShaderResources(0, 1, &srv_null); // The scene can't stay bound as SRV while it's read by the CS.
   ID3D11ShaderResourceView* cs_srv = srv_scene.get();
   native_device_context->CSSetShaderResources(0, 1, &cs_srv);
   const std::array<ID3D11UnorderedAccessView*, 2> cs_uavs = { uav_copy.get(), uav_gamma.get() };
   native_device_context->CSSetUnorderedAccessViews(0, cs_uavs.size(), cs_uavs.data(), nullptr);
   native_device_context->CSSetShader(cs_it->second.get(), nullptr, 0);
   native_device_context->Dispatch((tex_desc.Width + 8 - 1) / 8, (tex_desc.Height + 8 - 1) / 8, 1);

   // Unbind UAVs and SRVs.
   const std::array<ID3D11UnorderedAccessView*, 2> uav_nulls = {};
   native_device_context->CSSetUnorderedAccessViews(0, uav_nulls.size(), uav_nulls.data(), nullptr);
   native_device_context->CSSetShaderResources(0, 1, &srv_null);
   native_device_context->CSSetShader(nullptr, nullptr, 0);

   DrawSMAA(native_device, native_device_context, device_data, rtv_output.get(), srv_copy.get(), srv_gamma.get(), nullptr, cb_smaa_metrics.get());

   draw_state_stack.Restore(native_device_context);
   return true;
}

// Matches the "AutoExposure" cbuffer in the exposure CS files (48 bytes).
struct AutoExposureCBData
{
   float delta_time;
   float reset;
   float adapt_speed;
   float range_stops;
   uint32_t width;
   uint32_t height;
   float key;
   uint32_t mode;
   uint32_t depth_valid;
   float depth_numerator;
   float depth_offset;
   float pad;
};
static_assert(sizeof(AutoExposureCBData) == 48);

struct TemplateGameDeviceData final : public GameDeviceData
{
   bool has_drawn_tonemap = false;
   ComPtr<ID3D11BlendState> ambient_original_blend;
   ComPtr<ID3D11BlendState> ambient_multiply_blend;
   // Non-owning view onto core's DrawBloom mip 0 (core-managed, released with the swapchain). Rebuilt every tonemap draw.
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom;
   // Pre-fog copy of the scene RT, bound at t2 for the fog draw. Recreated when the RT size or format changes.
   ComPtr<ID3D11Texture2D> fog_scene_copy;
   ComPtr<ID3D11ShaderResourceView> fog_scene_copy_srv;
   // Copy of the half-res fog density RT, bound at t6 for the AO consumers. fog_density_frame is the frame it was captured in.
   ComPtr<ID3D11Texture2D> fog_density_copy;
   ComPtr<ID3D11ShaderResourceView> fog_density_copy_srv;
   uint64_t fog_density_frame = 0;
   // GPU auto-exposure state
   ComPtr<ID3D11Texture2D> exposure_tex; // 2x1 R32_FLOAT
   ComPtr<ID3D11ShaderResourceView> exposure_srv;
   ComPtr<ID3D11UnorderedAccessView> exposure_uav;
   ComPtr<ID3D11Buffer> histogram_buf; // 512 bytes raw
   ComPtr<ID3D11UnorderedAccessView> histogram_uav;
   ComPtr<ID3D11Buffer> exposure_cb; // AutoExposureCBData
   LARGE_INTEGER last_time = {};
   bool need_reset = true;
   bool exposure_dispatched_this_frame = false;
   uint32_t exposure_dispatched_mode = 0u;
   uint32_t exposure_width = 0;
   uint32_t exposure_height = 0;
   uint32_t last_auto_exposure_mode = 0u;
   ComPtr<ID3D11Buffer> last_game_cb4; // Game's PS cb4, reused by the adapt CS when cb4 is not bound on the source draw
   // XeGTAO (replaces the SSAO draw), owns its intermediates
   HMAXeGTAO::Data gtao_data;
   // Full res depth of this frame (PS t0 of kFullResDepthHash, released in OnPresent), the frame index it was captured in, and the frame index the full res AO was written in
   ComPtr<ID3D11ShaderResourceView> fullres_depth_srv;
   ComPtr<ID3D11ShaderResourceView> downsample_source_srv;
   ComPtr<ID3D11Resource> downsample_target;
   uint64_t downsample_frame = ~0ull;
   uint64_t frame_index = 0;
   uint64_t fullres_depth_frame = ~0ull;
   uint64_t fullres_ao_valid_frame = ~0ull;
   ComPtr<ID3D11ShaderResourceView> gi_ao_srv;
   ComPtr<ID3D11Resource> gi_normal_resource;
   uint64_t gi_ao_frame = ~0ull;
   ComPtr<ID3D11ShaderResourceView> sky_lower_srv;
   ComPtr<ID3D11RenderTargetView> sky_target;
   uint64_t sky_frame = ~0ull;
   // Modern lens flares: intermediates and the final flare target (owned here), and the result of this frame's tonemap draw (non-owning, valid only until OnPresent).
   HMAFlareResources flare_res;
   ID3D11ShaderResourceView* flare_srv_this_frame = nullptr;
   bool flare_valid_this_frame = false;
};

static ID3D11ShaderResourceView* HMA_GetExposureSRV(DeviceData& device_data)
{
   return static_cast<TemplateGameDeviceData*>(device_data.game)->exposure_srv.get();
}

static bool HMA_ExposureReady(DeviceData& device_data, uint32_t mode)
{
   const auto& gd = *static_cast<TemplateGameDeviceData*>(device_data.game);
   return mode != 2u || (gd.exposure_dispatched_this_frame && gd.exposure_dispatched_mode == mode);
}

class TemplateGame final : public Game // Go view "Source/Core/includes/game.h" for all the overridable virtual functions
{
   // Optional helper to hide ugly casts
   static TemplateGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<TemplateGameDeviceData*>(device_data.game);
   }

   // Karis average, then the bloom pyramid, off the HDR scene. The result is bound at kLumaBloomSlot for the draw.
   static void BindLumaBloom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, ID3D11ShaderResourceView* srv_scene)
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.srv_luma_bloom.reset(); // DrawBloom AddRef's its mip 0 into this
      if (srv_scene)
      {
         DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
         bloom_state.Cache(native_device_context, device_data.uav_max_count);

         ComPtr<ID3D11ShaderResourceView> srv_karis;
         DrawKarisAverage(native_device, native_device_context, device_data, srv_scene, srv_karis.put());
         if (srv_karis)
            DrawBloom(native_device, native_device_context, device_data, srv_karis.get(), (int)std::size(g_bloom_sigmas), g_bloom_sigmas, gd.srv_luma_bloom.put());

         bloom_state.Restore(native_device_context);
      }
      ID3D11ShaderResourceView* bloom_srv = gd.srv_luma_bloom.get();
      native_device_context->PSSetShaderResources(kLumaBloomSlot, 1, &bloom_srv);
   }

   // Creates the auto-exposure resources on first use. Returns false if any of them is missing (nothing is kept on failure).
   static bool EnsureExposureResources(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, TemplateGameDeviceData& gd)
   {
      if (gd.exposure_srv && gd.exposure_uav && gd.histogram_uav && gd.exposure_cb)
         return true;

      D3D11_TEXTURE2D_DESC tex_desc = {};
      tex_desc.Width = 2;
      tex_desc.Height = 1;
      tex_desc.MipLevels = 1;
      tex_desc.ArraySize = 1;
      tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
      tex_desc.SampleDesc.Count = 1;
      tex_desc.Usage = D3D11_USAGE_DEFAULT;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      ComPtr<ID3D11Texture2D> tex;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11UnorderedAccessView> uav;
      if (FAILED(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()))
         || FAILED(native_device->CreateShaderResourceView(tex.get(), nullptr, srv.put()))
         || FAILED(native_device->CreateUnorderedAccessView(tex.get(), nullptr, uav.put())))
         return false;

      D3D11_BUFFER_DESC hist_desc = {};
      hist_desc.ByteWidth = 1280; // 128 histogram bins plus 48 sixteen-byte tile records
      hist_desc.Usage = D3D11_USAGE_DEFAULT;
      hist_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
      hist_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
      ComPtr<ID3D11Buffer> hist_buf;
      if (FAILED(native_device->CreateBuffer(&hist_desc, nullptr, hist_buf.put())))
         return false;
      D3D11_UNORDERED_ACCESS_VIEW_DESC hist_uav_desc = {};
      hist_uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
      hist_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      hist_uav_desc.Buffer.FirstElement = 0;
      hist_uav_desc.Buffer.NumElements = hist_desc.ByteWidth / sizeof(uint32_t);
      hist_uav_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
      ComPtr<ID3D11UnorderedAccessView> hist_uav;
      if (FAILED(native_device->CreateUnorderedAccessView(hist_buf.get(), &hist_uav_desc, hist_uav.put())))
         return false;

      D3D11_BUFFER_DESC cb_desc = {};
      cb_desc.ByteWidth = sizeof(AutoExposureCBData);
      cb_desc.Usage = D3D11_USAGE_DEFAULT; // Updated with UpdateSubresource every dispatch
      cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      ComPtr<ID3D11Buffer> cb;
      if (FAILED(native_device->CreateBuffer(&cb_desc, nullptr, cb.put())))
         return false;

      gd.exposure_tex = tex;
      gd.exposure_srv = srv;
      gd.exposure_uav = uav;
      gd.histogram_buf = hist_buf;
      gd.histogram_uav = hist_uav;
      gd.exposure_cb = cb;

      // Start from zero, and make the adapt pass reset its state on the next dispatch.
      const UINT clear_uint[4] = { 0u, 0u, 0u, 0u };
      native_device_context->ClearUnorderedAccessViewUint(gd.histogram_uav.get(), clear_uint);
      const FLOAT clear_float[4] = { 0.f, 0.f, 0.f, 0.f };
      native_device_context->ClearUnorderedAccessViewFloat(gd.exposure_uav.get(), clear_float);
      gd.need_reset = true;
      return true;
   }

   // Histogram + adapt compute passes on the HDR scene (srv_scene at t0). Silently does nothing if anything is missing.
   static void DispatchAutoExposure(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, ID3D11ShaderResourceView* srv_scene)
   {
      auto& gd = GetGameDeviceData(device_data);

      auto histogram_it = device_data.native_compute_shaders.find(CompileTimeStringHash("HMA Exposure Histogram CS"));
      auto adapt_it = device_data.native_compute_shaders.find(CompileTimeStringHash("HMA Exposure Adapt CS"));
      if (histogram_it == device_data.native_compute_shaders.end() || !histogram_it->second.get()
         || adapt_it == device_data.native_compute_shaders.end() || !adapt_it->second.get())
         return;
      if (srv_scene == nullptr)
         return;

      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_scene->GetDesc(&srv_desc);
      if (srv_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
         return;
      ComPtr<ID3D11Resource> scene_resource;
      srv_scene->GetResource(scene_resource.put());
      ComPtr<ID3D11Texture2D> scene_tex;
      if (!scene_resource || FAILED(scene_resource->QueryInterface(scene_tex.put())))
         return;
      D3D11_TEXTURE2D_DESC scene_desc = {};
      scene_tex->GetDesc(&scene_desc);
      if (scene_desc.Width == 0 || scene_desc.Height == 0 || scene_desc.SampleDesc.Count != 1)
         return;
      if (cb_luma_global_settings.GameSettings.AutoExposureMode == 2u
         && (srv_desc.Texture2D.MostDetailedMip != 0
            || (srv_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && srv_desc.Format != DXGI_FORMAT_R11G11B10_FLOAT)))
         return;

      if (!EnsureExposureResources(native_device, native_device_context, gd))
         return;
      if (gd.exposure_width != scene_desc.Width || gd.exposure_height != scene_desc.Height)
         gd.need_reset = true;
      if (gd.need_reset)
      {
         const UINT clear_uint[4] = { 0u, 0u, 0u, 0u };
         native_device_context->ClearUnorderedAccessViewUint(gd.histogram_uav.get(), clear_uint);
      }

      // Delta time from QPC, clamped.
      static LARGE_INTEGER qpc_frequency = {};
      if (qpc_frequency.QuadPart == 0)
         QueryPerformanceFrequency(&qpc_frequency);
      LARGE_INTEGER now = {};
      QueryPerformanceCounter(&now);
      float delta_time = 0.f;
      if (gd.last_time.QuadPart != 0 && qpc_frequency.QuadPart != 0)
         delta_time = (float)((double)(now.QuadPart - gd.last_time.QuadPart) / (double)qpc_frequency.QuadPart);
      gd.last_time = now;
      delta_time = delta_time < 0.f ? 0.f : (delta_time > 0.25f ? 0.25f : delta_time);

      // The game's cb4 is needed by the adapt pass (bound now on the source draw, or the last one seen on a tonemap draw).
      ComPtr<ID3D11Buffer> game_cb4;
      native_device_context->PSGetConstantBuffers(4, 1, game_cb4.put());
      ID3D11Buffer* cs_cb4 = game_cb4 ? game_cb4.get() : gd.last_game_cb4.get();
      if (cs_cb4 == nullptr)
         return;
      if (cb_luma_global_settings.GameSettings.AutoExposureMode == 2u)
      {
         D3D11_BUFFER_DESC cb4_desc = {};
         cs_cb4->GetDesc(&cb4_desc);
         if (cb4_desc.ByteWidth < 42u * 16u)
            return;
      }

      AutoExposureCBData cb_data = {};
      cb_data.delta_time = delta_time;
      cb_data.reset = gd.need_reset ? 1.f : 0.f;
      cb_data.adapt_speed = cb_luma_global_settings.GameSettings.AutoExposureSpeed;
      cb_data.range_stops = cb_luma_global_settings.GameSettings.AutoExposureRange;
      cb_data.key = cb_luma_global_settings.GameSettings.AutoExposureKey;
      cb_data.mode = cb_luma_global_settings.GameSettings.AutoExposureMode;
      cb_data.width = scene_desc.Width;
      cb_data.height = scene_desc.Height;
      // Live trace validation is pending; mode 2 currently uses color-only metering.
      cb_data.depth_valid = 0u;
      cb_data.depth_numerator = 0.f;
      cb_data.depth_offset = 0.f;
      cb_data.pad = 0.f;
      native_device_context->UpdateSubresource(gd.exposure_cb.get(), 0, nullptr, &cb_data, 0, 0);

      DrawStateStack<DrawStateStackType::Compute> cs_state;
      cs_state.Cache(native_device_context, device_data.uav_max_count);

      ID3D11Buffer* cs_cb0 = gd.exposure_cb.get();
      native_device_context->CSSetConstantBuffers(0, 1, &cs_cb0);

      // Mode 2 gathers 48 color-only tiles; modes 0 and 1 keep the original full-frame histogram dispatch.
      const std::array<ID3D11ShaderResourceView*, 2> cs_srvs = { srv_scene, nullptr };
      native_device_context->CSSetShaderResources(0, cs_srvs.size(), cs_srvs.data());
      ID3D11UnorderedAccessView* cs_hist_uav = gd.histogram_uav.get();
      native_device_context->CSSetUnorderedAccessViews(0, 1, &cs_hist_uav, nullptr);
      native_device_context->CSSetShader(histogram_it->second.get(), nullptr, 0);
      if (cb_data.mode == 2u)
         native_device_context->Dispatch(8, 6, 1);
      else
         native_device_context->Dispatch((scene_desc.Width + 63u) / 64u, (scene_desc.Height + 63u) / 64u, 1);

      // Adapt: one group, reads the histogram and writes the exposure texture.
      const std::array<ID3D11ShaderResourceView*, 2> cs_srv_nulls = {};
      native_device_context->CSSetShaderResources(0, cs_srv_nulls.size(), cs_srv_nulls.data());
      const std::array<ID3D11UnorderedAccessView*, 2> cs_adapt_uavs = { gd.exposure_uav.get(), gd.histogram_uav.get() };
      native_device_context->CSSetUnorderedAccessViews(0, cs_adapt_uavs.size(), cs_adapt_uavs.data(), nullptr);
      native_device_context->CSSetConstantBuffers(4, 1, &cs_cb4);
      native_device_context->CSSetShader(adapt_it->second.get(), nullptr, 0);
      native_device_context->Dispatch(1, 1, 1);

      // Unbind UAVs and SRVs before the exposure is read as an SRV.
      const std::array<ID3D11UnorderedAccessView*, 2> cs_uav_nulls = {};
      native_device_context->CSSetUnorderedAccessViews(0, cs_uav_nulls.size(), cs_uav_nulls.data(), nullptr);
      native_device_context->CSSetShaderResources(0, cs_srv_nulls.size(), cs_srv_nulls.data());
      native_device_context->CSSetShader(nullptr, nullptr, 0);

      cs_state.Restore(native_device_context);

      gd.need_reset = false;
      gd.exposure_dispatched_this_frame = true;
      gd.exposure_dispatched_mode = cb_data.mode;
      gd.exposure_width = scene_desc.Width;
      gd.exposure_height = scene_desc.Height;
   }

public:
   // You can define any data to initialize once here
   void OnInit(bool async) override
   {
      // The scRGB fp16 swapchain upgrade is the only thing making the presented backbuffer report as HDR to
      // external tools (lilium's HDR analysis reads the format/colorspace, not the content). When the display is
      // not in HDR mode, present the game's own UNORM swapchain instead: the SDR image is identical (the final
      // pass already gamma-encodes into 0..1) and the game now reads as true SDR. The format is fixed at swapchain
      // creation and cannot be changed afterwards, so this is decided once at launch from the primary monitor
      // (null hwnd, no swapchain exists yet). Restart the game after toggling the Windows HDR setting.
      bool hdr_supported = false, hdr_enabled = false;
      Display::IsHDRSupportedAndEnabled(0, hdr_supported, hdr_enabled);
      if (!hdr_enabled)
         swapchain_upgrade_type = SwapchainUpgradeType::None;

      HMAHardPause::Init();
#if DEVELOPMENT
      Mcp::RegisterToggles({{"hitman.hard_pause", &HMAHardPause::requested}});
      Mcp::RegisterCounter("hitman.hard_pause_available", &HMAHardPause::available_status);
      Mcp::RegisterCounter("hitman.hard_pause_actual", &HMAHardPause::actual_status);
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("flare.mip0", flare_res.mip0.tex), MCP_GAME_TEXTURE("flare.mip1", flare_res.mip1.tex),
         MCP_GAME_TEXTURE("flare.halo", flare_res.halo.tex), MCP_GAME_TEXTURE("flare.feature", flare_res.feature.tex),
         MCP_GAME_TEXTURE("flare.glare", flare_res.glare.tex), MCP_GAME_TEXTURE("flare.final", flare_res.final_flare.tex)});
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("exposure", exposure_tex)});
      // XeGTAO: knobs, intermediates and path counters
      Mcp::RegisterToggles({{"gtao.enable", &g_enable_xegtao}, {"gtao.vbao", &g_xegtao_vbao}});
      Mcp::RegisterValues({{"gtao.radius", &g_xegtao_radius, 0.05f, 10.f}, {"gtao.intensity", &g_xegtao_intensity, 0.25f, 8.f}, {"gtao.thickness", &g_xegtao_thickness, 0.05f, 5.f}});
      Mcp::RegisterCounters({{"gtao.fullres_runs", &g_xegtao_fullres_runs}, {"gtao.halfres_runs", &g_xegtao_halfres_runs},
         {"gtao.failures", &g_xegtao_failures}, {"gtao.t5_binds", &g_xegtao_t5_binds}});
      Mcp::RegisterCounters({{"gtao.fail_depth", &HMAXeGTAO::fail_depth}, {"gtao.fail_target", &HMAXeGTAO::fail_target},
         {"gtao.fail_shaders", &HMAXeGTAO::fail_shaders}, {"gtao.fail_sampler", &HMAXeGTAO::fail_sampler},
         {"gtao.fail_resources", &HMAXeGTAO::fail_resources}, {"gtao.fail_output", &HMAXeGTAO::fail_output}, {"gtao.fail_map", &HMAXeGTAO::fail_map}});
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("gtao.depth", gtao_data.resources.depth.texture), MCP_GAME_TEXTURE("gtao.ao", gtao_data.resources.ao.texture),
         MCP_GAME_TEXTURE("gtao.denoised_ao", gtao_data.resources.denoised_ao.texture), MCP_GAME_TEXTURE("gtao.final_ao", gtao_data.resources.final_ao.texture),
         MCP_GAME_TEXTURE("gtao.fullres_ao", gtao_data.fullres_ao.texture)});
      Mcp::RegisterCounter("gbuffer.clamps", &g_gbuffer_clamps);
#endif
      // You can add shader defines that will end up in the advanced settings for users to modify here.
      // These will be defined in the shaders, so they can be used to do static branches in them.
      // Ideally they should also be defined in the game's Settings.hlsl file.
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", /*default value*/ '1', true, false, /*tooltip*/ "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)", /*max value*/ 1},
         {"XE_GTAO_QUALITY", /*default value*/ '4', true, false, "0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", /*max value*/ 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES); // Make sure there's room for at least one extra custom define to add for development (this isn't really relevant outside of development)

      // Define these according to the game's original technical details and the mod's implementation (see their declarations for more).
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // What space are the colors in? Was the swapchain linear (sRGB texture format)? Did we change post processing to store in linear space?
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); // Whether we do gamma correction and paper white scaling during post processing or we delay them until the final display composition pass
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2: the HDR final pass encodes with pow(1/2.2) (DICE runs in that space), so decoding as sRGB overshot the peak by ~2%
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // What SDR transfer curve to we want to emulate? This is relevant even if we work in linear space, as there can be a gamma mismatch on it
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2'); // UI draws on the swapchain in gamma space after the final pass, which pre-divides the scene by UI paper white
#if !DEVELOPMENT && !TEST
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1');
      use_os_reference_white_level = false;
      g_pause_hotkey = false;
      g_tonemapped_resolve = 0u;
      g_xegtao_radius = 5.f;
      g_xegtao_intensity = 1.5f;
      g_xegtao_vbao = true;
#endif

      // Customize the cbuffers indexes here. They should be between 0 and 13 ("D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1"),
      // set them to invalid values (e.g. -1) to not set them (they won't be uploaded to the GPU, and thus not usable in shaders). All their values need to be different if they are valid.
      // These will be automatically sent to the GPU for every shader pass the mod overrides.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // Needed for debugging textures and having custom per pass data (including handling the final pass properly).
      luma_ui_cbuffer_index = -1; // Optional, see "UI_DRAW_TYPE" (this is for type 1)

      // Init the game settings here, you could also load them from ini in "LoadConfigs()".
      // These can be defined in a header shared between shaders and c++, and are sent to the GPU every time they change.
      //
      // It should be in "Shaders/Game/Includes/GameCBuffers.hlsl".
      //
      // To include it, the easiest way is adding as Forced Include within the Project Properties.
      // C/C++ -> All Options -> Forced Include File --> "../../../Shaders/$(ProjectName)/Includes/GameCBuffers.hlsl;" (w/o quotation marks)
      //
      // Otherwise, manually add it to the project files and include that at the top (before anything else, as the game cb settings struct needs to be defined),
      // or make a copy of it in c++ (making sure it's mirrored to hlsl), and define "LUMA_GAME_CB_STRUCTS" to avoid it being re-defined.
      //
      // Set "device_data.cb_luma_global_settings_dirty" to true if you changed values and need a re-upload to the GPU before OnPresent() (i.e. during OnDrawOrDispatch()).
      default_luma_global_game_settings.HighlightSaturation = cb_luma_global_settings.GameSettings.HighlightSaturation = 1.f;
      default_luma_global_game_settings.GamutExpansionHueLock = cb_luma_global_settings.GameSettings.GamutExpansionHueLock = 0.5f;
      default_luma_global_game_settings.BloomStrength = cb_luma_global_settings.GameSettings.BloomStrength = 0.75f;
      default_luma_global_game_settings.LumaBloom = cb_luma_global_settings.GameSettings.LumaBloom = 1u;
      default_luma_global_game_settings.LumaBloomStrength = cb_luma_global_settings.GameSettings.LumaBloomStrength = 1.f;
      default_luma_global_game_settings.VanillaGlareStrength = cb_luma_global_settings.GameSettings.VanillaGlareStrength = 0.25f;
      default_luma_global_game_settings.FilmGrainStrength = cb_luma_global_settings.GameSettings.FilmGrainStrength = 1.f;
      default_luma_global_game_settings.SkyHDRBoost = cb_luma_global_settings.GameSettings.SkyHDRBoost = 0.5f;
      default_luma_global_game_settings.SkyDebanding = cb_luma_global_settings.GameSettings.SkyDebanding = 1u;
      default_luma_global_game_settings.Exposure = cb_luma_global_settings.GameSettings.Exposure = 1.f;
      default_luma_global_game_settings.Contrast = cb_luma_global_settings.GameSettings.Contrast = 1.f;
      default_luma_global_game_settings.Saturation = cb_luma_global_settings.GameSettings.Saturation = 1.f;
      default_luma_global_game_settings.Highlights = cb_luma_global_settings.GameSettings.Highlights = 1.f;
      default_luma_global_game_settings.Shadows = cb_luma_global_settings.GameSettings.Shadows = 1.f;
      default_luma_global_game_settings.BlackFloorRemoval = cb_luma_global_settings.GameSettings.BlackFloorRemoval = 0.f;
      default_luma_global_game_settings.HighlightRolloff = cb_luma_global_settings.GameSettings.HighlightRolloff = 0.f;
      default_luma_global_game_settings.DistanceHaze = cb_luma_global_settings.GameSettings.DistanceHaze = 1.f;
      default_luma_global_game_settings.HighlightHuePreservation = cb_luma_global_settings.GameSettings.HighlightHuePreservation = 1.f;
      default_luma_global_game_settings.TopGradientStrength = cb_luma_global_settings.GameSettings.TopGradientStrength = 1.f;
      default_luma_global_game_settings.VignetteStrength = cb_luma_global_settings.GameSettings.VignetteStrength = 1.f;
      default_luma_global_game_settings.ShadowLuminanceGrading = cb_luma_global_settings.GameSettings.ShadowLuminanceGrading = 1.f;
      default_luma_global_game_settings.GammaCorrectionByLuminance = cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance = 0.f;
      default_luma_global_game_settings.GamutExpansion = cb_luma_global_settings.GameSettings.GamutExpansion = 1.f;
      default_luma_global_game_settings.DisplayMapping = cb_luma_global_settings.GameSettings.DisplayMapping = 2u;
      default_luma_global_game_settings.TonemapVersion = cb_luma_global_settings.GameSettings.TonemapVersion = 1u;
      default_luma_global_game_settings.HazeModel = cb_luma_global_settings.GameSettings.HazeModel = 0.f;
      default_luma_global_game_settings.ShoulderStart = cb_luma_global_settings.GameSettings.ShoulderStart = 0.75f;
      default_luma_global_game_settings.HighlightPurity = cb_luma_global_settings.GameSettings.HighlightPurity = 0.5f;
      default_luma_global_game_settings.EmissiveHDRBoost = cb_luma_global_settings.GameSettings.EmissiveHDRBoost = 0.5f;
      default_luma_global_game_settings.SunShaftHDRBoost = cb_luma_global_settings.GameSettings.SunShaftHDRBoost = 1.f;
      default_luma_global_game_settings.VideoAutoHDR = cb_luma_global_settings.GameSettings.VideoAutoHDR = 0.5f;
      default_luma_global_game_settings.VideoAutoHDREnable = cb_luma_global_settings.GameSettings.VideoAutoHDREnable = 1u;
      default_luma_global_game_settings.ModernLensFlares = cb_luma_global_settings.GameSettings.ModernLensFlares = 1u;
      default_luma_global_game_settings.BokehDOF = cb_luma_global_settings.GameSettings.BokehDOF = 1u;
      default_luma_global_game_settings.BokehDOFFiltering = cb_luma_global_settings.GameSettings.BokehDOFFiltering = 1u;
      default_luma_global_game_settings.BokehDOFRotation = cb_luma_global_settings.GameSettings.BokehDOFRotation = 0.f;
      default_luma_global_game_settings.BokehDOFTapLimit = cb_luma_global_settings.GameSettings.BokehDOFTapLimit = 64u;
      default_luma_global_game_settings.VanillaHueRestore = cb_luma_global_settings.GameSettings.VanillaHueRestore = 1.0f;
      default_luma_global_game_settings.AutoExposureMode = cb_luma_global_settings.GameSettings.AutoExposureMode = 1u;
      default_luma_global_game_settings.AutoExposureSpeed = cb_luma_global_settings.GameSettings.AutoExposureSpeed = 2.f;
      default_luma_global_game_settings.AutoExposureRange = cb_luma_global_settings.GameSettings.AutoExposureRange = 2.0f;
      default_luma_global_game_settings.AutoExposureKey = cb_luma_global_settings.GameSettings.AutoExposureKey = 0.11f;
      default_luma_global_game_settings.GIStrength = cb_luma_global_settings.GameSettings.GIStrength = 1.f;
      default_luma_global_game_settings.GIOcclusion = cb_luma_global_settings.GameSettings.GIOcclusion = 0.f;
      default_luma_global_game_settings.AODarkness = cb_luma_global_settings.GameSettings.AODarkness = 1.f;
      default_luma_global_game_settings.HighlightCompression = cb_luma_global_settings.GameSettings.HighlightCompression = 0.f;
#if !DEVELOPMENT && !TEST
      default_luma_global_game_settings.LumaBloomStrength = cb_luma_global_settings.GameSettings.LumaBloomStrength = 0.202f;
      default_luma_global_game_settings.FilmGrainStrength = cb_luma_global_settings.GameSettings.FilmGrainStrength = 0.500f;
      default_luma_global_game_settings.TopGradientStrength = cb_luma_global_settings.GameSettings.TopGradientStrength = 0.f;
      default_luma_global_game_settings.GamutExpansion = cb_luma_global_settings.GameSettings.GamutExpansion = 0.901f;
      default_luma_global_game_settings.GamutExpansionHueLock = cb_luma_global_settings.GameSettings.GamutExpansionHueLock = 1.f;
      default_luma_global_game_settings.ShoulderStart = cb_luma_global_settings.GameSettings.ShoulderStart = 0.5f;
      default_luma_global_game_settings.BokehDOFRotation = cb_luma_global_settings.GameSettings.BokehDOFRotation = 1.f;
      default_luma_global_game_settings.BokehDOFTapLimit = cb_luma_global_settings.GameSettings.BokehDOFTapLimit = 128u;
      default_luma_global_game_settings.AODarkness = cb_luma_global_settings.GameSettings.AODarkness = 0.5f;
      default_luma_global_game_settings.AutoExposureRange = cb_luma_global_settings.GameSettings.AutoExposureRange = 0.99f;
#endif

      native_shaders_definitions.emplace(CompileTimeStringHash("HMA Tonemapped Resolve"), ShaderDefinition{ "Luma_HMA_TonemappedResolve", reshade::api::pipeline_subobject_type::pixel_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA Tonemapped Resolve 2x2"), ShaderDefinition{ "Luma_HMA_TonemappedResolve2x2", reshade::api::pipeline_subobject_type::pixel_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA Ambient Volume"), ShaderDefinition{ "Luma_HMA_AmbientVolume", reshade::api::pipeline_subobject_type::pixel_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA GBuffer Clamp Zero"), ShaderDefinition{ "Luma_HMA_GBufferClamp_Zero", reshade::api::pipeline_subobject_type::pixel_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA GBuffer Clamp One"), ShaderDefinition{ "Luma_HMA_GBufferClamp_One", reshade::api::pipeline_subobject_type::pixel_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA XeGTAO Prefilter CS"), ShaderDefinition{ "Luma_HMA_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA XeGTAO Main CS"), ShaderDefinition{ "Luma_HMA_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA XeGTAO Main VBAO CS"), ShaderDefinition{ "Luma_HMA_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_vbao_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA XeGTAO Denoise CS"), ShaderDefinition{ "Luma_HMA_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA XeGTAO Denoise Final CS"), ShaderDefinition{ "Luma_HMA_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_final_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("SMAA Linear To sRGB CS"), ShaderDefinition{ "Luma_SMAA_LinearTosRGB_CS", reshade::api::pipeline_subobject_type::compute_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA Exposure Histogram CS"), ShaderDefinition{ "Luma_HMA_ExposureHistogram_CS", reshade::api::pipeline_subobject_type::compute_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("HMA Exposure Adapt CS"), ShaderDefinition{ "Luma_HMA_ExposureAdapt_CS", reshade::api::pipeline_subobject_type::compute_shader });
      reshade::register_event<reshade::addon_event::resolve_texture_region>(HMA_OnResolveTextureRegion);
      HMA_RegisterFlareShaders();
   }

   // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new TemplateGameDeviceData;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Convert the game's ambient subtraction to multiplication by visibility.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE
         && original_shader_hashes.Contains(0xD4342F38, reshade::api::shader_stage::pixel)
         && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1
         && original_draw_dispatch_func)
      {
         auto shader = device_data.native_pixel_shaders.find(CompileTimeStringHash("HMA Ambient Volume"));
         ComPtr<ID3D11BlendState> original_blend;
         FLOAT blend_factor[4];
         UINT sample_mask;
         native_device_context->OMGetBlendState(original_blend.put(), blend_factor, &sample_mask);
         if (shader != device_data.native_pixel_shaders.end() && shader->second.get() && original_blend)
         {
            D3D11_BLEND_DESC desc = {};
            original_blend->GetDesc(&desc);
            auto& rt = desc.RenderTarget[0];
            if (rt.BlendEnable && rt.SrcBlend == D3D11_BLEND_ONE && rt.DestBlend == D3D11_BLEND_ONE
               && rt.BlendOp == D3D11_BLEND_OP_REV_SUBTRACT)
            {
               if (game_device_data.ambient_original_blend.get() != original_blend.get())
               {
                  rt.SrcBlend = D3D11_BLEND_ZERO;
                  rt.DestBlend = D3D11_BLEND_INV_SRC_COLOR;
                  rt.BlendOp = D3D11_BLEND_OP_ADD;
                  ComPtr<ID3D11BlendState> multiply_blend;
                  if (SUCCEEDED(native_device->CreateBlendState(&desc, multiply_blend.put())))
                  {
                     game_device_data.ambient_original_blend = original_blend;
                     game_device_data.ambient_multiply_blend = multiply_blend;
                  }
               }
               if (game_device_data.ambient_original_blend.get() == original_blend.get() && game_device_data.ambient_multiply_blend)
               {
                  ComPtr<ID3D11PixelShader> saved;
                  ID3D11ClassInstance* instances[256] = {};
                  UINT count = 256;
                  native_device_context->PSGetShader(saved.put(), instances, &count);
                  native_device_context->PSSetShader(shader->second.get(), nullptr, 0);
                  native_device_context->OMSetBlendState(game_device_data.ambient_multiply_blend.get(), blend_factor, sample_mask);
                  (*original_draw_dispatch_func)();
                  native_device_context->OMSetBlendState(original_blend.get(), blend_factor, sample_mask);
                  native_device_context->PSSetShader(saved.get(), instances, count);
                  for (UINT i = 0; i < count; ++i) if (instances[i]) instances[i]->Release();
                  return DrawOrDispatchOverrideType::Skip;
               }
            }
         }
      }

#if !DEVELOPMENT
      if (!original_shader_hashes.Contains(kFinalPostHash, reshade::api::shader_stage::pixel)
         && !original_shader_hashes.Contains(kHurtCompositeHash, reshade::api::shader_stage::pixel)
         && !original_shader_hashes.Contains(0x563FB90F, reshade::api::shader_stage::pixel)
         && !original_shader_hashes.Contains(kFogHash, reshade::api::shader_stage::pixel)
         && !original_shader_hashes.Contains(kAOConsumerHash, reshade::api::shader_stage::pixel)
         && !std::any_of(std::begin(kExposureBindHashes), std::end(kExposureBindHashes), [&](uint32_t h) { return original_shader_hashes.Contains(h, reshade::api::shader_stage::pixel); }))
         original_draw_dispatch_func = nullptr;
#endif

      // Pair consecutive sky layers only when their texture views and render target match.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && (stages & reshade::api::shader_stage::pixel) != 0)
      {
         const bool sky_shader = original_shader_hashes.Contains(0x563FB90F, reshade::api::shader_stage::pixel) && original_shader_hashes.Contains(0x8A2E5A62, reshade::api::shader_stage::vertex);
         ComPtr<ID3D11ShaderResourceView> sky_texture;
         ComPtr<ID3D11RenderTargetView> sky_rtv;
         ComPtr<ID3D11Resource> texture_resource, rt_resource;
         ComPtr<ID3D11Texture2D> texture, rt_texture;
         D3D11_TEXTURE2D_DESC tex_desc = {}, rt_desc = {};
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         if (sky_shader)
         {
            native_device_context->PSGetShaderResources(0, 1, sky_texture.put());
            native_device_context->OMGetRenderTargets(1, sky_rtv.put(), nullptr);
            if (sky_texture) { sky_texture->GetDesc(&srv_desc); sky_texture->GetResource(texture_resource.put()); }
            if (texture_resource && SUCCEEDED(texture_resource->QueryInterface(texture.put()))) texture->GetDesc(&tex_desc);
            HMA_GetRTVTexture(sky_rtv.get(), rt_resource, rt_texture, rt_desc);
         }
         const bool valid = sky_shader && sky_rtv && texture && tex_desc.SampleDesc.Count == 1 && tex_desc.ArraySize == 1 &&
            srv_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && srv_desc.Texture2D.MostDetailedMip == 0 && srv_desc.Format == DXGI_FORMAT_BC1_UNORM_SRGB &&
            (rt_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || rt_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT);
         const bool lower = valid && tex_desc.Width == 512 && tex_desc.Height == 256;
         const bool cap = valid && tex_desc.Width == 256 && tex_desc.Height == 256;
         if (cap && cmd_list_data.pipeline_state_has_custom_pixel_shader && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1 && original_draw_dispatch_func)
         {
            ComPtr<ID3D11ShaderResourceView> saved;
            native_device_context->PSGetShaderResources(1, 1, saved.put());
            ID3D11ShaderResourceView* lower_srv = game_device_data.sky_frame == game_device_data.frame_index && game_device_data.sky_target.get() == sky_rtv.get() ? game_device_data.sky_lower_srv.get() : nullptr;
            native_device_context->PSSetShaderResources(1, 1, &lower_srv);
            (*original_draw_dispatch_func)();
            lower_srv = saved.get();
            native_device_context->PSSetShaderResources(1, 1, &lower_srv);
            game_device_data.sky_lower_srv.reset();
            game_device_data.sky_target.reset();
            return DrawOrDispatchOverrideType::Skip;
         }
         game_device_data.sky_lower_srv.reset();
         game_device_data.sky_target.reset();
         if (lower)
         {
            game_device_data.sky_lower_srv = sky_texture;
            game_device_data.sky_target = sky_rtv;
            game_device_data.sky_frame = game_device_data.frame_index;
         }
      }

      // G-buffer clamp (see "HMA_ClampGBuffers()"): detect the deferred fill, then clamp once before the first draw that reads it.
      if (g_gbuffer_clamp != 0u && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
      {
         ID3D11RenderTargetView* rtv_ptrs[4] = {};
         native_device_context->OMGetRenderTargets(4, rtv_ptrs, nullptr);
         ComPtr<ID3D11Resource> rt_resources[4];
         ComPtr<ID3D11Texture2D> rt_textures[4];
         D3D11_TEXTURE2D_DESC rt_descs[4] = {};
         bool rt_valid = true;
         for (UINT i = 0; rt_valid && i < 4; i++)
         {
            rt_valid = HMA_GetRTVTexture(rtv_ptrs[i], rt_resources[i], rt_textures[i], rt_descs[i]);
         }

         bool is_gbuffer_fill = rt_valid
            && rt_descs[0].Format == DXGI_FORMAT_R16G16B16A16_FLOAT
            && rt_descs[1].Format == DXGI_FORMAT_R16G16B16A16_FLOAT
            && rt_descs[2].Format == DXGI_FORMAT_R16G16B16A16_FLOAT
            && rt_descs[3].Format == DXGI_FORMAT_R32_FLOAT;
         for (UINT i = 1; is_gbuffer_fill && i < 4; i++)
         {
            is_gbuffer_fill = rt_descs[i].Width == rt_descs[0].Width && rt_descs[i].Height == rt_descs[0].Height && rt_descs[i].SampleDesc.Count == rt_descs[0].SampleDesc.Count;
         }

         if (is_gbuffer_fill)
         {
            for (UINT i = 0; i < 3; i++)
            {
               g_gbuffer_resources[i] = rt_resources[i];
               g_gbuffer_textures[i] = rt_textures[i];
            }
            g_gbuffer_dirty = true;
         }
         else if (g_gbuffer_dirty)
         {
            ID3D11ShaderResourceView* srv_ptrs[16] = {};
            native_device_context->PSGetShaderResources(0, 16, srv_ptrs);

            bool reads_gbuffer = false;
            for (UINT i = 0; i < 16 && !reads_gbuffer; i++)
            {
               if (srv_ptrs[i] == nullptr)
                  continue;
               ComPtr<ID3D11Resource> srv_resource;
               srv_ptrs[i]->GetResource(srv_resource.put());
               for (UINT j = 0; j < 3; j++)
               {
                  if (srv_resource && srv_resource.get() == g_gbuffer_resources[j].get())
                     reads_gbuffer = true;
               }
            }
            for (UINT i = 0; i < 16; i++)
            {
               if (srv_ptrs[i] != nullptr)
                  srv_ptrs[i]->Release();
            }

            if (reads_gbuffer)
            {
               g_gbuffer_dirty = false;
               HMA_ClampGBuffers(native_device, native_device_context, device_data);
            }
         }

         for (UINT i = 0; i < 4; i++)
         {
            if (rtv_ptrs[i] != nullptr)
               rtv_ptrs[i]->Release();
         }
      }

      // Main tonemap pass: mark the main post process as drawn (kTonemapHash/kTonemapHash2).
      // Here you can track and customize shader passes by hash, you can do whatever you want in it
      if (!game_device_data.has_drawn_tonemap && (original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kTonemapHash2, reshade::api::shader_stage::pixel)))
      {
         game_device_data.has_drawn_tonemap = true;

         device_data.has_drawn_main_post_processing = true;
      }

      // Luma bloom on the tonemap pass: the HDR scene at t1 feeds the pyramid, bound at t4 for this draw only when on.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && (original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kTonemapHash2, reshade::api::shader_stage::pixel))
         && cb_luma_global_settings.GameSettings.LumaBloom != 0 && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1)
      {
         ComPtr<ID3D11ShaderResourceView> srv_scene;
         native_device_context->PSGetShaderResources(1, 1, srv_scene.put());
         BindLumaBloom(native_device, native_device_context, device_data, srv_scene.get());
      }

      // Restart adaptation before any current-frame exposure consumer, including flare and the resolve hook.
      if (game_device_data.last_auto_exposure_mode != cb_luma_global_settings.GameSettings.AutoExposureMode)
      {
         game_device_data.need_reset = true;
         game_device_data.exposure_dispatched_this_frame = false;
      }
      game_device_data.last_auto_exposure_mode = cb_luma_global_settings.GameSettings.AutoExposureMode;

      // Modern lens flares on the tonemap pass: the flare chain reads the HDR scene (t1), the game fade (t3), the game cb4 and the exposure. Its result is bound at t3 for the final post draw (kFinalPostHash).
      // Runs before the exposure t7 bind below, and restores all the state it touches.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && (original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kTonemapHash2, reshade::api::shader_stage::pixel))
         && cb_luma_global_settings.GameSettings.LumaBloom != 0 && cb_luma_global_settings.GameSettings.ModernLensFlares != 0 && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1)
      {
         ComPtr<ID3D11ShaderResourceView> srv_scene;
         ComPtr<ID3D11ShaderResourceView> srv_fade;
         ComPtr<ID3D11Buffer> cb4;
         native_device_context->PSGetShaderResources(1, 1, srv_scene.put());
         native_device_context->PSGetShaderResources(3, 1, srv_fade.put());
         native_device_context->PSGetConstantBuffers(4, 1, cb4.put());
         const bool flare_exposure_ready = HMA_ExposureReady(device_data, cb_luma_global_settings.GameSettings.AutoExposureMode);
         ComPtr<ID3D11ShaderResourceView> srv_t7_saved;
         if (cb_luma_global_settings.GameSettings.AutoExposureMode == 2u && !flare_exposure_ready)
         {
            native_device_context->PSGetShaderResources(kExposureSlot, 1, srv_t7_saved.put());
            ID3D11ShaderResourceView* srv_null = nullptr;
            native_device_context->PSSetShaderResources(kExposureSlot, 1, &srv_null);
         }
         ID3D11ShaderResourceView* srv_flare = nullptr;
         if (srv_scene && HMA_DrawFlare(native_device, native_device_context, device_data, game_device_data.flare_res, g_flare_settings, srv_scene.get(), srv_fade.get(),
               flare_exposure_ready ? game_device_data.exposure_srv.get() : nullptr, cb4.get(), &srv_flare) && srv_flare)
         {
            game_device_data.flare_valid_this_frame = true;
            game_device_data.flare_srv_this_frame = srv_flare; // Non-owning, owned by flare_res
         }
         if (cb_luma_global_settings.GameSettings.AutoExposureMode == 2u && !flare_exposure_ready)
         {
            ID3D11ShaderResourceView* srv_t7_restore = srv_t7_saved.get();
            native_device_context->PSSetShaderResources(kExposureSlot, 1, &srv_t7_restore);
         }
      }

      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE
         && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1)
      {
         // Keep the game's cb4 for the adapt pass, from the tonemap draws.
         if (original_shader_hashes.Contains(kTonemapHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kTonemapHash2, reshade::api::shader_stage::pixel))
         {
            ComPtr<ID3D11Buffer> cb4;
            native_device_context->PSGetConstantBuffers(4, 1, cb4.put());
            if (cb4)
               game_device_data.last_game_cb4 = cb4;
         }

         // Histogram + adapt once per frame, on the exposure source draw. Runs even at mode 0 (vanilla): the exposure consumers fall back to the game's exposure,
         // but the adapt pass still meters the scene bright point (meter (1,0)) that FinalPost's BT.2390 display mapping uses.
         if (!game_device_data.exposure_dispatched_this_frame
            && (original_shader_hashes.Contains(kExposureSourceHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kExposureSourceHash2, reshade::api::shader_stage::pixel)))
         {
            ComPtr<ID3D11ShaderResourceView> srv_scene;
            native_device_context->PSGetShaderResources(0, 1, srv_scene.put());
            if (srv_scene)
               DispatchAutoExposure(native_device, native_device_context, device_data, srv_scene.get());
         }

         // Exposure at t7 only for the duration of the consumer draws: the game uses t7 elsewhere, so the previous t7 is restored right after.
         const bool exposure_consumer = std::any_of(std::begin(kExposureBindHashes), std::end(kExposureBindHashes), [&](uint32_t h) { return original_shader_hashes.Contains(h, reshade::api::shader_stage::pixel); });
         const bool exposure_mode = cb_luma_global_settings.GameSettings.AutoExposureMode != 0u;
         const bool adaptive_ready = HMA_ExposureReady(device_data, cb_luma_global_settings.GameSettings.AutoExposureMode);
         if (exposure_consumer && original_draw_dispatch_func
            && ((exposure_mode && adaptive_ready && game_device_data.exposure_srv) || cb_luma_global_settings.GameSettings.AutoExposureMode == 2u))
         {
            ComPtr<ID3D11ShaderResourceView> srv_t7_saved;
            native_device_context->PSGetShaderResources(kExposureSlot, 1, srv_t7_saved.put());
            ID3D11ShaderResourceView* srv_exposure = exposure_mode && adaptive_ready ? game_device_data.exposure_srv.get() : nullptr;
            native_device_context->PSSetShaderResources(kExposureSlot, 1, &srv_exposure);
            (*original_draw_dispatch_func)();
            ID3D11ShaderResourceView* srv_t7_restore = srv_t7_saved.get();
            native_device_context->PSSetShaderResources(kExposureSlot, 1, &srv_t7_restore);
            return DrawOrDispatchOverrideType::Skip;
         }
      }

      // FXAA replacement: the game's FXAA pixel shader draw becomes Luma SMAA when "AntiAliasingMode" is 1. On failure the original draw runs.
      if (g_anti_aliasing_mode == 1u && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(kFXAAHash, reshade::api::shader_stage::pixel))
      {
         if (HMA_DrawSMAAForFXAA(native_device, native_device_context, device_data))
            return DrawOrDispatchOverrideType::Skip;
      }

      // A new view's depth or AO pass invalidates the previous consumer's AO.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE &&
         (original_shader_hashes.Contains(kFullResDepthHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kSSAOHash, reshade::api::shader_stage::pixel)))
      {
         game_device_data.gi_ao_srv.reset();
         game_device_data.gi_normal_resource.reset();
         game_device_data.fullres_ao_valid_frame = ~0ull;
      }

      // The alternative path downsamples full-resolution R32 depth into the SSAO input.
      if (g_enable_xegtao && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(kDepthDownsampleHash, reshade::api::shader_stage::pixel))
      {
         game_device_data.downsample_source_srv.reset();
         game_device_data.downsample_target.reset();
         game_device_data.fullres_depth_srv.reset(); // A new downsample starts a new view.
         ComPtr<ID3D11ShaderResourceView> source;
         ComPtr<ID3D11RenderTargetView> target;
         ComPtr<ID3D11Resource> source_resource, target_resource;
         ComPtr<ID3D11Texture2D> source_texture, target_texture;
         native_device_context->PSGetShaderResources(0, 1, source.put());
         native_device_context->OMGetRenderTargets(1, target.put(), nullptr);
         if (source) source->GetResource(source_resource.put());
         if (target) target->GetResource(target_resource.put());
         if (source_resource && target_resource && SUCCEEDED(source_resource->QueryInterface(source_texture.put())) && SUCCEEDED(target_resource->QueryInterface(target_texture.put())))
         {
            D3D11_TEXTURE2D_DESC src = {}, dst = {};
            D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
            D3D11_RENDER_TARGET_VIEW_DESC rtv = {};
            source_texture->GetDesc(&src); target_texture->GetDesc(&dst);
            source->GetDesc(&srv); target->GetDesc(&rtv);
            UINT count = 1;
            D3D11_VIEWPORT viewport = {};
            native_device_context->RSGetViewports(&count, &viewport);
            if (src.SampleDesc.Count == 1 && dst.SampleDesc.Count == 1 && src.ArraySize == 1 && dst.ArraySize == 1 &&
               (src.Format == DXGI_FORMAT_R32_FLOAT || src.Format == DXGI_FORMAT_R32_TYPELESS) &&
               (dst.Format == DXGI_FORMAT_R32_FLOAT || dst.Format == DXGI_FORMAT_R32_TYPELESS) &&
               srv.Format == DXGI_FORMAT_R32_FLOAT && srv.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && srv.Texture2D.MostDetailedMip == 0 &&
               rtv.Format == DXGI_FORMAT_R32_FLOAT && rtv.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D && rtv.Texture2D.MipSlice == 0 &&
               src.Width > 1 && src.Height > 1 && (dst.Width == src.Width / 2 || dst.Width == (src.Width + 1) / 2) && (dst.Height == src.Height / 2 || dst.Height == (src.Height + 1) / 2) &&
               count == 1 && viewport.TopLeftX == 0 && viewport.TopLeftY == 0 &&
               ((viewport.Width == float(dst.Width) && viewport.Height == float(dst.Height)) || (viewport.Width == float(src.Width) && viewport.Height == float(src.Height))))
            {
               game_device_data.downsample_source_srv = source;
               game_device_data.downsample_target = target_resource;
               game_device_data.downsample_frame = game_device_data.frame_index;
            }
         }
      }

      // XeGTAO full res: keep the PS t0 of the draw just before the SSAO (full res raw device depth) for this frame. The draw itself runs.
      if (g_enable_xegtao && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(kFullResDepthHash, reshade::api::shader_stage::pixel))
      {
         ComPtr<ID3D11ShaderResourceView> srv_depth_full;
         native_device_context->PSGetShaderResources(0, 1, srv_depth_full.put());
         if (srv_depth_full)
         {
            game_device_data.fullres_depth_srv = srv_depth_full;
            game_device_data.fullres_depth_frame = game_device_data.frame_index;
         }
      }

      // XeGTAO replacement: the game's SSAO draw is replaced by XeGTAO, written into RT0. On any failure the original draw runs.
      if (g_enable_xegtao && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(kSSAOHash, reshade::api::shader_stage::pixel))
      {
         ComPtr<ID3D11ShaderResourceView> srv_depth;
         ComPtr<ID3D11Buffer> cb1;
         ComPtr<ID3D11RenderTargetView> rtv_0;
         native_device_context->PSGetShaderResources(0, 1, srv_depth.put());
         native_device_context->PSGetConstantBuffers(1, 1, cb1.put());
         native_device_context->OMGetRenderTargets(1, rtv_0.put(), nullptr);
         ComPtr<ID3D11Resource> ssao_depth_resource;
         if (srv_depth) srv_depth->GetResource(ssao_depth_resource.put());
         ID3D11ShaderResourceView* fullres_depth = nullptr;
         if (game_device_data.fullres_depth_srv && game_device_data.fullres_depth_frame == game_device_data.frame_index)
            fullres_depth = game_device_data.fullres_depth_srv.get();
         else if (game_device_data.downsample_frame == game_device_data.frame_index && ssao_depth_resource && ssao_depth_resource.get() == game_device_data.downsample_target.get())
            fullres_depth = game_device_data.downsample_source_srv.get();
         // Consume this candidate once; no later view may reuse it.
         ComPtr<ID3D11ShaderResourceView> selected_depth = fullres_depth;
         game_device_data.fullres_depth_srv.reset();
         game_device_data.downsample_source_srv.reset();
         game_device_data.downsample_target.reset();
         // Full res path: RT0 is not written; the AO consumer reads the full res output instead.
         if (selected_depth && cb1
            && HMAXeGTAO::DrawFullRes(native_device, native_device_context, device_data, game_device_data.gtao_data, selected_depth.get(), cb1.get(), g_xegtao_radius, g_xegtao_intensity, g_xegtao_vbao, g_xegtao_thickness))
         {
            game_device_data.fullres_ao_valid_frame = game_device_data.frame_index;
            g_xegtao_fullres_runs++;
            return DrawOrDispatchOverrideType::Skip;
         }
         if (srv_depth && cb1 && rtv_0)
         {
            ComPtr<ID3D11Resource> rt0_resource;
            rtv_0->GetResource(rt0_resource.put());
            ComPtr<ID3D11Texture2D> rt0_tex;
            if (rt0_resource && SUCCEEDED(rt0_resource->QueryInterface(rt0_tex.put())))
            {
               D3D11_TEXTURE2D_DESC rt0_desc = {};
               rt0_tex->GetDesc(&rt0_desc);
               if (HMAXeGTAO::Draw(native_device, native_device_context, device_data, game_device_data.gtao_data, srv_depth.get(), cb1.get(), rt0_resource.get(), rt0_desc, g_xegtao_radius, g_xegtao_intensity, g_xegtao_vbao, g_xegtao_thickness))
               {
                  g_xegtao_halfres_runs++;
                  return DrawOrDispatchOverrideType::Skip;
               }
            }
         }
         g_xegtao_failures++; // Neither path replaced the draw: the original SSAO runs
      }

      // Capture the exact AO and G-buffer normal used by this view's deferred lighting.
      static constexpr uint32_t ao_consumers[] = { 0x3994DDCA, 0x6A01F756, 0x2FBAC811, 0x9C3FA046, 0x46557B91, 0xE518B956, 0x4BAB9769, 0xB822DFF2, 0x04B76E1A, 0x5DE07B2B, 0x1E43437A, 0xAEACA752, 0x58C48E12, 0x86B62EAA };
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && std::any_of(std::begin(ao_consumers), std::end(ao_consumers), [&](uint32_t h) { return original_shader_hashes.Contains(h, reshade::api::shader_stage::pixel); }))
      {
         game_device_data.gi_ao_srv.reset();
         game_device_data.gi_normal_resource.reset();
         ComPtr<ID3D11ShaderResourceView> normal;
         native_device_context->PSGetShaderResources(2, 1, normal.put());
         if (normal) normal->GetResource(game_device_data.gi_normal_resource.put());
         const bool fullres = original_shader_hashes.Contains(kAOConsumerHash, reshade::api::shader_stage::pixel) && g_enable_xegtao && game_device_data.fullres_ao_valid_frame == game_device_data.frame_index && game_device_data.gtao_data.fullres_ao.srv;
         if (fullres)
            game_device_data.gi_ao_srv = game_device_data.gtao_data.fullres_ao.srv;
         else
            native_device_context->PSGetShaderResources(5, 1, game_device_data.gi_ao_srv.put());
         game_device_data.gi_ao_frame = game_device_data.frame_index;
         // Fog density copy at t6 for the AO consumers (this frame or the previous one, else null). The original t6 is restored after the draw.
         const bool fog_ao = GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1 && original_draw_dispatch_func
            && (original_shader_hashes.Contains(kAOConsumerHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(0x6A01F756, reshade::api::shader_stage::pixel));
         if (fog_ao)
         {
            ComPtr<ID3D11ShaderResourceView> saved_t6;
            native_device_context->PSGetShaderResources(6, 1, saved_t6.put());
            ID3D11ShaderResourceView* fog_density = game_device_data.fog_density_copy_srv && game_device_data.fog_density_frame + 1 >= game_device_data.frame_index ? game_device_data.fog_density_copy_srv.get() : nullptr;
            native_device_context->PSSetShaderResources(6, 1, &fog_density);
            if (fullres)
            {
               ComPtr<ID3D11ShaderResourceView> saved;
               native_device_context->PSGetShaderResources(5, 1, saved.put());
               ID3D11ShaderResourceView* ao = game_device_data.gi_ao_srv.get();
               native_device_context->PSSetShaderResources(5, 1, &ao);
               g_xegtao_t5_binds++;
               (*original_draw_dispatch_func)();
               ao = saved.get();
               native_device_context->PSSetShaderResources(5, 1, &ao);
            }
            else
            {
               (*original_draw_dispatch_func)();
            }
            ID3D11ShaderResourceView* t6_restore = saved_t6.get();
            native_device_context->PSSetShaderResources(6, 1, &t6_restore);
            return DrawOrDispatchOverrideType::Skip;
         }
         if (fullres && original_draw_dispatch_func)
         {
            ComPtr<ID3D11ShaderResourceView> saved;
            native_device_context->PSGetShaderResources(5, 1, saved.put());
            ID3D11ShaderResourceView* ao = game_device_data.gi_ao_srv.get();
            native_device_context->PSSetShaderResources(5, 1, &ao);
            g_xegtao_t5_binds++;
            (*original_draw_dispatch_func)();
            ao = saved.get();
            native_device_context->PSSetShaderResources(5, 1, &ao);
            return DrawOrDispatchOverrideType::Skip;
         }
      }

      static constexpr uint32_t gi_hashes[] = { 0x8A0F6018, 0xADD46D0F, 0x626315E7, 0x0EA2360C, 0xA3EDF879, 0xF074469E, 0xCED3C761 };
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE &&
         std::any_of(std::begin(gi_hashes), std::end(gi_hashes), [&](uint32_t h) { return original_shader_hashes.Contains(h, reshade::api::shader_stage::pixel); }))
      {
         if (GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) < 1 || (cb_luma_global_settings.GameSettings.GIStrength == 1.f && cb_luma_global_settings.GameSettings.GIOcclusion == 0.f))
         {
            Shader::UsePipelineVariant(cmd_list_data, native_device_context, reshade::api::shader_stage::pixel, Shader::PipelineVariant::Original);
            return DrawOrDispatchOverrideType::None;
         }
         if (!original_draw_dispatch_func) return DrawOrDispatchOverrideType::None;
         ComPtr<ID3D11ShaderResourceView> saved, normal;
         ComPtr<ID3D11Resource> normal_resource;
         native_device_context->PSGetShaderResources(6, 1, saved.put());
         native_device_context->PSGetShaderResources(4, 1, normal.put());
         if (normal) normal->GetResource(normal_resource.put());
         ID3D11ShaderResourceView* ao = game_device_data.gi_ao_frame == game_device_data.frame_index && normal_resource && normal_resource.get() == game_device_data.gi_normal_resource.get() ? game_device_data.gi_ao_srv.get() : nullptr;
         native_device_context->PSSetShaderResources(6, 1, &ao);
         (*original_draw_dispatch_func)();
         ao = saved.get();
         native_device_context->PSSetShaderResources(6, 1, &ao);
         return DrawOrDispatchOverrideType::Skip;
      }

      // Fog fade: copy the scene RT (still pre-fog) and bind it at t2 so the fog shader can fade its alpha toward black. On any failure the original draw runs.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(kFogHash, reshade::api::shader_stage::pixel)
         && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1 && original_draw_dispatch_func)
      {
         ComPtr<ID3D11RenderTargetView> rtv_scene;
         native_device_context->OMGetRenderTargets(1, rtv_scene.put(), nullptr);
         ComPtr<ID3D11Resource> rt_resource;
         if (rtv_scene)
            rtv_scene->GetResource(rt_resource.put());
         ComPtr<ID3D11Texture2D> rt_tex;
         if (rt_resource && SUCCEEDED(rt_resource->QueryInterface(rt_tex.put())))
         {
            D3D11_TEXTURE2D_DESC rt_desc = {};
            rt_tex->GetDesc(&rt_desc);
            if (rt_desc.Width > 0 && rt_desc.Height > 0 && rt_desc.SampleDesc.Count == 1)
            {
               bool copy_ready = false;
               if (game_device_data.fog_scene_copy && game_device_data.fog_scene_copy_srv)
               {
                  D3D11_TEXTURE2D_DESC copy_desc = {};
                  game_device_data.fog_scene_copy->GetDesc(&copy_desc);
                  copy_ready = copy_desc.Width == rt_desc.Width && copy_desc.Height == rt_desc.Height && copy_desc.Format == rt_desc.Format;
               }
               if (!copy_ready)
               {
                  D3D11_TEXTURE2D_DESC new_desc = rt_desc;
                  new_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                  new_desc.Usage = D3D11_USAGE_DEFAULT;
                  new_desc.CPUAccessFlags = 0;
                  new_desc.MiscFlags = 0;
                  game_device_data.fog_scene_copy.reset();
                  game_device_data.fog_scene_copy_srv.reset();
                  if (SUCCEEDED(native_device->CreateTexture2D(&new_desc, nullptr, game_device_data.fog_scene_copy.put())))
                     native_device->CreateShaderResourceView(game_device_data.fog_scene_copy.get(), nullptr, game_device_data.fog_scene_copy_srv.put());
                  copy_ready = game_device_data.fog_scene_copy && game_device_data.fog_scene_copy_srv;
               }
               if (copy_ready)
               {
                  native_device_context->CopyResource(game_device_data.fog_scene_copy.get(), rt_tex.get());
                  ID3D11ShaderResourceView* srv_fog_scene = game_device_data.fog_scene_copy_srv.get();
                  native_device_context->PSSetShaderResources(2, 1, &srv_fog_scene);
                  (*original_draw_dispatch_func)();
                  ID3D11ShaderResourceView* srv_fog_null = nullptr;
                  native_device_context->PSSetShaderResources(2, 1, &srv_fog_null);
                  return DrawOrDispatchOverrideType::Skip;
               }
            }
         }
      }

      // Fog density: the original draw runs, then the RT is copied for the AO consumers (t6). Always skipped here so the draw never runs twice.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE
         && std::any_of(std::begin(kFogDensityHashes), std::end(kFogDensityHashes), [&](uint32_t h) { return original_shader_hashes.Contains(h, reshade::api::shader_stage::pixel); })
         && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1 && original_draw_dispatch_func)
      {
         (*original_draw_dispatch_func)();
         ComPtr<ID3D11RenderTargetView> rtv_density;
         native_device_context->OMGetRenderTargets(1, rtv_density.put(), nullptr);
         ComPtr<ID3D11Resource> density_resource;
         if (rtv_density)
            rtv_density->GetResource(density_resource.put());
         ComPtr<ID3D11Texture2D> density_tex;
         if (density_resource && SUCCEEDED(density_resource->QueryInterface(density_tex.put())))
         {
            D3D11_TEXTURE2D_DESC density_desc = {};
            density_tex->GetDesc(&density_desc);
            if (density_desc.Width > 0 && density_desc.Height > 0 && density_desc.SampleDesc.Count == 1)
            {
               bool density_copy_ready = false;
               if (game_device_data.fog_density_copy && game_device_data.fog_density_copy_srv)
               {
                  D3D11_TEXTURE2D_DESC copy_desc = {};
                  game_device_data.fog_density_copy->GetDesc(&copy_desc);
                  density_copy_ready = copy_desc.Width == density_desc.Width && copy_desc.Height == density_desc.Height && copy_desc.Format == density_desc.Format;
               }
               if (!density_copy_ready)
               {
                  D3D11_TEXTURE2D_DESC new_desc = density_desc;
                  new_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                  new_desc.Usage = D3D11_USAGE_DEFAULT;
                  new_desc.CPUAccessFlags = 0;
                  new_desc.MiscFlags = 0;
                  game_device_data.fog_density_copy.reset();
                  game_device_data.fog_density_copy_srv.reset();
                  if (SUCCEEDED(native_device->CreateTexture2D(&new_desc, nullptr, game_device_data.fog_density_copy.put())))
                     native_device->CreateShaderResourceView(game_device_data.fog_density_copy.get(), nullptr, game_device_data.fog_density_copy_srv.put());
                  density_copy_ready = game_device_data.fog_density_copy && game_device_data.fog_density_copy_srv;
               }
               if (density_copy_ready)
               {
                  native_device_context->CopyResource(game_device_data.fog_density_copy.get(), density_tex.get());
                  game_device_data.fog_density_frame = game_device_data.frame_index;
               }
            }
         }
         return DrawOrDispatchOverrideType::Skip;
      }

      // Modern lens flares on the final post and hurt composite passes: t3 and t5 respectively get the flare result when valid, null otherwise.
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE
         && (original_shader_hashes.Contains(kFinalPostHash, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(kHurtCompositeHash, reshade::api::shader_stage::pixel))
         && GetShaderDefineCompiledNumericalValue(TONEMAP_TYPE_HASH) >= 1 && original_draw_dispatch_func)
      {
         const bool hurt_composite = original_shader_hashes.Contains(kHurtCompositeHash, reshade::api::shader_stage::pixel);
         const UINT flare_slot = hurt_composite ? 5u : 3u;
         ComPtr<ID3D11ShaderResourceView> srv_t7_saved;
         if (!hurt_composite)
         {
            native_device_context->PSGetShaderResources(kExposureSlot, 1, srv_t7_saved.put());
            ID3D11ShaderResourceView* srv_meter = game_device_data.exposure_dispatched_this_frame ? game_device_data.exposure_srv.get() : nullptr;
            native_device_context->PSSetShaderResources(kExposureSlot, 1, &srv_meter);
         }
         ComPtr<ID3D11ShaderResourceView> srv_flare_slot_saved;
         native_device_context->PSGetShaderResources(flare_slot, 1, srv_flare_slot_saved.put());
         ComPtr<ID3D11SamplerState> smp_flare_slot_saved;
         native_device_context->PSGetSamplers(flare_slot, 1, smp_flare_slot_saved.put());
         ID3D11SamplerState* smp_flare = game_device_data.flare_res.smp_clamp.get();
         if (game_device_data.flare_valid_this_frame && smp_flare)
            native_device_context->PSSetSamplers(flare_slot, 1, &smp_flare);
         ID3D11ShaderResourceView* srv_flare = game_device_data.flare_valid_this_frame && smp_flare ? game_device_data.flare_srv_this_frame : nullptr;
         native_device_context->PSSetShaderResources(flare_slot, 1, &srv_flare);
         (*original_draw_dispatch_func)();
         if (!hurt_composite)
         {
            ID3D11ShaderResourceView* srv_t7_restore = srv_t7_saved.get();
            native_device_context->PSSetShaderResources(kExposureSlot, 1, &srv_t7_restore);
         }
         ID3D11ShaderResourceView* srv_flare_slot_restore = srv_flare_slot_saved.get();
         native_device_context->PSSetShaderResources(flare_slot, 1, &srv_flare_slot_restore);
         ID3D11SamplerState* smp_flare_slot_restore = smp_flare_slot_saved.get();
         native_device_context->PSSetSamplers(flare_slot, 1, &smp_flare_slot_restore);
         return DrawOrDispatchOverrideType::Skip;
      }

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
#if DEVELOPMENT
      HMABackgroundInput::Install();
      UpdateWindowControls();
      HMAHardPause::Update(g_pause_hotkey, g_pause_on_focus_loss);
#elif TEST
      HMAHardPause::Update();
#else
      HMAHardPause::Update(g_pause_hotkey);
#endif
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.frame_index++;
      game_device_data.sky_lower_srv.reset();
      game_device_data.sky_target.reset();
      game_device_data.gi_ao_srv.reset();
      game_device_data.gi_normal_resource.reset();
      game_device_data.fullres_depth_srv.reset(); // Only used within the frame it was captured in
      game_device_data.downsample_source_srv.reset();
      game_device_data.downsample_target.reset();
      game_device_data.has_drawn_tonemap = false;
      game_device_data.exposure_dispatched_this_frame = false;
      device_data.has_drawn_main_post_processing = true;

      // Modern lens flares: the result only lives for this frame. Free the targets when the feature is off.
      game_device_data.flare_valid_this_frame = false;
      game_device_data.flare_srv_this_frame = nullptr;
      if (cb_luma_global_settings.GameSettings.ModernLensFlares == 0u || cb_luma_global_settings.GameSettings.LumaBloom == 0u)
         game_device_data.flare_res.Release();

      // LumaBloom off: give the address space back (core's DrawKarisAverage output, ~66 MB at 4K). Core only drops the UAV on swapchain init, so both views go here.
      if (cb_luma_global_settings.GameSettings.LumaBloom == 0u)
      {
         game_device_data.srv_luma_bloom.reset();
         auto& mr = device_data.managed_resources;
         mr.unordered_access_views[CompileTimeStringHash("luma_karis_average")].reset();
         mr.shader_resource_views[CompileTimeStringHash("luma_karis_average")].reset();
      }
#if !DEVELOPMENT
      // Reapply borderless fullscreen if the game restored borders or the window does not cover the monitor.
      if (game_window && !IsIconic(game_window))
      {
         MONITORINFO mi = {};
         mi.cbSize = sizeof(mi);
         RECT rc_window = {};
         bool has_borders = (GetWindowLongPtr(game_window, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME | WS_BORDER)) != 0;
         bool rect_mismatch = !(GetMonitorInfo(MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST), &mi) && GetWindowRect(game_window, &rc_window) && EqualRect(&rc_window, &mi.rcMonitor));
         if (has_borders || rect_mismatch)
            ForceBorderlessFullscreen();
      }
#endif
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

#if !DEVELOPMENT && !TEST
      float scene_peak_white = 0.f;
      if (!reshade::get_config_value(runtime, NAME, "ScenePeakWhite", scene_peak_white))
         hdr_display_mode_pending_auto_peak_white_calibration = true;
#endif

#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "ForceBorderless", g_force_borderless);
      reshade::get_config_value(runtime, NAME, "AlwaysOnTop", g_always_on_top);
      reshade::get_config_value(runtime, NAME, "PauseOnFocusLoss", g_pause_on_focus_loss);
      reshade::get_config_value(runtime, NAME, "PauseHotkey", g_pause_hotkey);
      reshade::get_config_value(runtime, NAME, "AllowBackgroundControls", g_allow_background_controls);
      HMABackgroundInput::allow.store(g_allow_background_controls, std::memory_order_relaxed);
#endif
      // ReShade.ini is ready here, so load its values.
#if DEVELOPMENT || TEST
      reshade::get_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      reshade::get_config_value(runtime, NAME, "GamutExpansionHueLock", cb_luma_global_settings.GameSettings.GamutExpansionHueLock);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      reshade::get_config_value(runtime, NAME, "LumaBloom", cb_luma_global_settings.GameSettings.LumaBloom);
      reshade::get_config_value(runtime, NAME, "ModernLensFlares", cb_luma_global_settings.GameSettings.ModernLensFlares);
      reshade::get_config_value(runtime, NAME, "FlareThreshold", g_flare_settings.threshold);
      reshade::get_config_value(runtime, NAME, "FlareKnee", g_flare_settings.knee);
      uint32_t modern_glare = g_flare_settings.glare ? 1u : 0u;
      reshade::get_config_value(runtime, NAME, "ModernGlare", modern_glare);
      g_flare_settings.glare = modern_glare != 0u;
      reshade::get_config_value(runtime, NAME, "VanillaGlareStrength", cb_luma_global_settings.GameSettings.VanillaGlareStrength);
#endif
      reshade::get_config_value(runtime, NAME, "LumaBloomStrength", cb_luma_global_settings.GameSettings.LumaBloomStrength);
#if !DEVELOPMENT && !TEST
      cb_luma_global_settings.GameSettings.LumaBloomStrength = std::clamp(cb_luma_global_settings.GameSettings.LumaBloomStrength, 0.f, 2.f);
#endif
      reshade::get_config_value(runtime, NAME, "FlareIntensity", g_flare_settings.intensity);
      reshade::get_config_value(runtime, NAME, "GlareIntensity", g_flare_settings.glare_intensity);
      reshade::get_config_value(runtime, NAME, "BokehDOF", cb_luma_global_settings.GameSettings.BokehDOF);
#if DEVELOPMENT || TEST
      reshade::get_config_value(runtime, NAME, "BokehDOFFiltering", cb_luma_global_settings.GameSettings.BokehDOFFiltering);
      reshade::get_config_value(runtime, NAME, "BokehDOFRotation", cb_luma_global_settings.GameSettings.BokehDOFRotation);
      cb_luma_global_settings.GameSettings.BokehDOFRotation = std::clamp(cb_luma_global_settings.GameSettings.BokehDOFRotation, 0.f, 1.f);
      reshade::get_config_value(runtime, NAME, "BokehDOFTapLimit", cb_luma_global_settings.GameSettings.BokehDOFTapLimit);
      cb_luma_global_settings.GameSettings.BokehDOFTapLimit = std::clamp(cb_luma_global_settings.GameSettings.BokehDOFTapLimit, 8u, 128u);
#endif
      reshade::get_config_value(runtime, NAME, "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength);
#if DEVELOPMENT || TEST
      reshade::get_config_value(runtime, NAME, "SkyHDRBoost", cb_luma_global_settings.GameSettings.SkyHDRBoost);
      reshade::get_config_value(runtime, NAME, "SkyDebanding", cb_luma_global_settings.GameSettings.SkyDebanding);
#endif
#if DEVELOPMENT || TEST
      reshade::get_config_value(runtime, NAME, "GIStrength", cb_luma_global_settings.GameSettings.GIStrength);
      reshade::get_config_value(runtime, NAME, "GIOcclusion", cb_luma_global_settings.GameSettings.GIOcclusion);
      reshade::get_config_value(runtime, NAME, "AODarkness", cb_luma_global_settings.GameSettings.AODarkness);
      cb_luma_global_settings.GameSettings.AODarkness = std::clamp(cb_luma_global_settings.GameSettings.AODarkness, 0.f, 1.f);
#endif
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      reshade::get_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      reshade::get_config_value(runtime, NAME, "BlackFloorRemoval", cb_luma_global_settings.GameSettings.BlackFloorRemoval);
      reshade::get_config_value(runtime, NAME, "DistanceHaze", cb_luma_global_settings.GameSettings.DistanceHaze);
      cb_luma_global_settings.GameSettings.DistanceHaze = std::clamp(cb_luma_global_settings.GameSettings.DistanceHaze, 0.f, 2.f);
#if DEVELOPMENT || TEST
      reshade::get_config_value(runtime, NAME, "HighlightRolloff", cb_luma_global_settings.GameSettings.HighlightRolloff);
      reshade::get_config_value(runtime, NAME, "HighlightHuePreservation", cb_luma_global_settings.GameSettings.HighlightHuePreservation);
      reshade::get_config_value(runtime, NAME, "TopGradientStrength", cb_luma_global_settings.GameSettings.TopGradientStrength);
      reshade::get_config_value(runtime, NAME, "VignetteStrength", cb_luma_global_settings.GameSettings.VignetteStrength);
      reshade::get_config_value(runtime, NAME, "ShadowLuminanceGrading", cb_luma_global_settings.GameSettings.ShadowLuminanceGrading);
      reshade::get_config_value(runtime, NAME, "GammaCorrectionByLuminance", cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance);
      reshade::get_config_value(runtime, NAME, "GamutExpansion", cb_luma_global_settings.GameSettings.GamutExpansion);
      reshade::get_config_value(runtime, NAME, "DisplayMapping", cb_luma_global_settings.GameSettings.DisplayMapping);
      reshade::get_config_value(runtime, NAME, "ShoulderStart", cb_luma_global_settings.GameSettings.ShoulderStart);
      reshade::get_config_value(runtime, NAME, "HighlightPurity", cb_luma_global_settings.GameSettings.HighlightPurity);
      reshade::get_config_value(runtime, NAME, "EmissiveHDRBoost", cb_luma_global_settings.GameSettings.EmissiveHDRBoost);
      reshade::get_config_value(runtime, NAME, "SunShaftHDRBoost", cb_luma_global_settings.GameSettings.SunShaftHDRBoost);
#endif
      reshade::get_config_value(runtime, NAME, "VideoAutoHDR", cb_luma_global_settings.GameSettings.VideoAutoHDR);
      reshade::get_config_value(runtime, NAME, "VideoAutoHDREnable", cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      reshade::get_config_value(runtime, NAME, "VanillaHueRestore", cb_luma_global_settings.GameSettings.VanillaHueRestore);
      reshade::get_config_value(runtime, NAME, "TonemapVersion", cb_luma_global_settings.GameSettings.TonemapVersion);
      reshade::get_config_value(runtime, NAME, "HazeModel", cb_luma_global_settings.GameSettings.HazeModel);
      cb_luma_global_settings.GameSettings.HazeModel = std::clamp(cb_luma_global_settings.GameSettings.HazeModel, 0.f, 1.f);
      reshade::get_config_value(runtime, NAME, "AutoExposureMode", cb_luma_global_settings.GameSettings.AutoExposureMode);
      if (cb_luma_global_settings.GameSettings.AutoExposureMode > 2u)
         cb_luma_global_settings.GameSettings.AutoExposureMode = 1u;
      reshade::get_config_value(runtime, NAME, "AutoExposureSpeed", cb_luma_global_settings.GameSettings.AutoExposureSpeed);
      reshade::get_config_value(runtime, NAME, "AutoExposureRange", cb_luma_global_settings.GameSettings.AutoExposureRange);
      reshade::get_config_value(runtime, NAME, "AutoExposureKey", cb_luma_global_settings.GameSettings.AutoExposureKey);
      reshade::get_config_value(runtime, NAME, "HighlightCompression", cb_luma_global_settings.GameSettings.HighlightCompression);
      reshade::get_config_value(runtime, NAME, "TonemappedResolve", g_tonemapped_resolve);
#if DEVELOPMENT || TEST
      reshade::get_config_value(runtime, NAME, "GBufferClamp", g_gbuffer_clamp);
#endif
      reshade::get_config_value(runtime, NAME, "AntiAliasingMode", g_anti_aliasing_mode);
      reshade::get_config_value(runtime, NAME, "EnableXeGTAO", g_enable_xegtao);
      reshade::get_config_value(runtime, NAME, "XeGTAORadius", g_xegtao_radius);
      reshade::get_config_value(runtime, NAME, "XeGTAOIntensity", g_xegtao_intensity);
      {
         int xegtao_mode = g_xegtao_vbao ? 1 : 0;
         reshade::get_config_value(runtime, NAME, "XeGTAOMode", xegtao_mode);
         g_xegtao_vbao = xegtao_mode != 0;
      }
      reshade::get_config_value(runtime, NAME, "XeGTAOThickness", g_xegtao_thickness);
      g_xegtao_thickness = g_xegtao_thickness < 0.05f ? 0.05f : (g_xegtao_thickness > 5.f ? 5.f : g_xegtao_thickness);
      // Caps: sliders max out at 2 in all builds, so clamp stale ini values into range.
      auto clamp_cap = [](float& v, float min_v) { v = v < min_v ? min_v : (v > 2.f ? 2.f : v); };
      clamp_cap(cb_luma_global_settings.GameSettings.Exposure, 0.25f);
      clamp_cap(cb_luma_global_settings.GameSettings.Contrast, 0.5f);
      clamp_cap(cb_luma_global_settings.GameSettings.Saturation, 0.f);
      clamp_cap(cb_luma_global_settings.GameSettings.Highlights, 0.5f);
      clamp_cap(cb_luma_global_settings.GameSettings.Shadows, 0.5f);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
 #if DEVELOPMENT || TEST
      ImGui::BeginDisabled(!HMAHardPause::available);
      if (ImGui::Button(HMAHardPause::requested ? "Resume Simulation" : "Hard Pause"))
         HMAHardPause::requested = !HMAHardPause::requested;
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::Text("%s | Requested: %s | Engine: %s", HMAHardPause::available ? "Available (Pause key)" : "Unavailable for this executable", HMAHardPause::requested ? "paused" : "running", HMAHardPause::actual_status ? "paused" : "running");
#endif
      reshade::api::effect_runtime* runtime = nullptr;
      
#if DEVELOPMENT
      if (ImGui::Checkbox("Force Borderless", &g_force_borderless)) reshade::set_config_value(runtime, NAME, "ForceBorderless", g_force_borderless);
      if (ImGui::Checkbox("Always on Top", &g_always_on_top)) reshade::set_config_value(runtime, NAME, "AlwaysOnTop", g_always_on_top);
      if (ImGui::Checkbox("Allow Background Controls", &g_allow_background_controls))
      {
         HMABackgroundInput::allow.store(g_allow_background_controls, std::memory_order_relaxed);
         reshade::set_config_value(runtime, NAME, "AllowBackgroundControls", g_allow_background_controls);
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(HMABackgroundInput::direct_input_available ? "Suppress game input while another window is active." : "DirectInput filtering unavailable for this executable; Win32 keyboard and XInput are filtered.");
      if (ImGui::Checkbox("Pause on Focus Loss", &g_pause_on_focus_loss)) reshade::set_config_value(runtime, NAME, "PauseOnFocusLoss", g_pause_on_focus_loss);
      if (ImGui::Checkbox("Enable Pause Hotkey", &g_pause_hotkey)) reshade::set_config_value(runtime, NAME, "PauseHotkey", g_pause_hotkey);
#endif
      // Draw whatever ImGui to let users modify settings.
#if DEVELOPMENT || TEST
      if (ImGui::SliderFloat("Highlight Saturation", &cb_luma_global_settings.GameSettings.HighlightSaturation, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightSaturation, default_luma_global_game_settings.HighlightSaturation, "HighlightSaturation", runtime);
#endif

#if DEVELOPMENT || TEST
      if (ImGui::SliderFloat("Bloom Strength", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, default_luma_global_game_settings.BloomStrength, "BloomStrength", runtime);

      bool luma_bloom = cb_luma_global_settings.GameSettings.LumaBloom != 0;
      if (ImGui::Checkbox("Luma Bloom", &luma_bloom))
      {
         cb_luma_global_settings.GameSettings.LumaBloom = luma_bloom ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "LumaBloom", cb_luma_global_settings.GameSettings.LumaBloom);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloom, default_luma_global_game_settings.LumaBloom, "LumaBloom", runtime);

      if (cb_luma_global_settings.GameSettings.LumaBloom != 0)
      {
         bool modern_flares = cb_luma_global_settings.GameSettings.ModernLensFlares != 0;
         if (ImGui::Checkbox("Modern Lens Flares", &modern_flares))
         {
            cb_luma_global_settings.GameSettings.ModernLensFlares = modern_flares ? 1u : 0u;
            reshade::set_config_value(runtime, NAME, "ModernLensFlares", cb_luma_global_settings.GameSettings.ModernLensFlares);
         }
         DrawResetButton(cb_luma_global_settings.GameSettings.ModernLensFlares, default_luma_global_game_settings.ModernLensFlares, "ModernLensFlares", runtime);

         if (modern_flares)
         {
            if (ImGui::SliderFloat("Flare Intensity", &g_flare_settings.intensity, 0.f, 20.f, "%.3f"))
               reshade::set_config_value(runtime, NAME, "FlareIntensity", g_flare_settings.intensity);
            DrawResetButton(g_flare_settings.intensity, g_flare_defaults.intensity, "FlareIntensity", runtime);

            if (ImGui::SliderFloat("Flare Threshold", &g_flare_settings.threshold, 0.f, 20.f, "%.3f"))
               reshade::set_config_value(runtime, NAME, "FlareThreshold", g_flare_settings.threshold);
            DrawResetButton(g_flare_settings.threshold, g_flare_defaults.threshold, "FlareThreshold", runtime);

            if (ImGui::SliderFloat("Flare Knee", &g_flare_settings.knee, 0.f, 1.f, "%.3f"))
               reshade::set_config_value(runtime, NAME, "FlareKnee", g_flare_settings.knee);
            DrawResetButton(g_flare_settings.knee, g_flare_defaults.knee, "FlareKnee", runtime);

            bool modern_glare_edit = g_flare_settings.glare;
            if (ImGui::Checkbox("Modern Glare", &modern_glare_edit))
            {
               g_flare_settings.glare = modern_glare_edit;
               uint32_t modern_glare_value = modern_glare_edit ? 1u : 0u;
               reshade::set_config_value(runtime, NAME, "ModernGlare", modern_glare_value);
            }
            // Reset goes through a uint32 (the config value type), copied back to the bool.
            uint32_t modern_glare_reset = g_flare_settings.glare ? 1u : 0u;
            DrawResetButton(modern_glare_reset, g_flare_defaults.glare ? 1u : 0u, "ModernGlare", runtime);
            g_flare_settings.glare = modern_glare_reset != 0u;

            if (g_flare_settings.glare)
            {
               if (ImGui::SliderFloat("Glare Intensity", &g_flare_settings.glare_intensity, 0.f, 10.f, "%.3f"))
                  reshade::set_config_value(runtime, NAME, "GlareIntensity", g_flare_settings.glare_intensity);
               DrawResetButton(g_flare_settings.glare_intensity, g_flare_defaults.glare_intensity, "GlareIntensity", runtime);
            }
         }

         if (ImGui::SliderFloat("Luma Bloom Strength", &cb_luma_global_settings.GameSettings.LumaBloomStrength, 0.f, 4.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "LumaBloomStrength", cb_luma_global_settings.GameSettings.LumaBloomStrength);
         DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloomStrength, default_luma_global_game_settings.LumaBloomStrength, "LumaBloomStrength", runtime);

         if (ImGui::SliderFloat("Game Glare/Flares with Luma Bloom", &cb_luma_global_settings.GameSettings.VanillaGlareStrength, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "VanillaGlareStrength", cb_luma_global_settings.GameSettings.VanillaGlareStrength);
         DrawResetButton(cb_luma_global_settings.GameSettings.VanillaGlareStrength, default_luma_global_game_settings.VanillaGlareStrength, "VanillaGlareStrength", runtime);
      }
#endif

#if DEVELOPMENT || TEST
      if (ImGui::SliderFloat("Sky HDR Boost", &cb_luma_global_settings.GameSettings.SkyHDRBoost, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "SkyHDRBoost", cb_luma_global_settings.GameSettings.SkyHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Expands the brightest parts of the sky (near the sun) into HDR");
      DrawResetButton(cb_luma_global_settings.GameSettings.SkyHDRBoost, default_luma_global_game_settings.SkyHDRBoost, "SkyHDRBoost", runtime);

      bool sky_debanding = cb_luma_global_settings.GameSettings.SkyDebanding != 0;
      if (ImGui::Checkbox("Sky Debanding", &sky_debanding))
      {
         cb_luma_global_settings.GameSettings.SkyDebanding = sky_debanding ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "SkyDebanding", cb_luma_global_settings.GameSettings.SkyDebanding);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.SkyDebanding, default_luma_global_game_settings.SkyDebanding, "SkyDebanding", runtime);

      if (ImGui::SliderFloat("Emissive HDR Boost", &cb_luma_global_settings.GameSettings.EmissiveHDRBoost, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "EmissiveHDRBoost", cb_luma_global_settings.GameSettings.EmissiveHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Pushes the bright parts of lamps, screens and other self-lit surfaces into HDR. 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.EmissiveHDRBoost, default_luma_global_game_settings.EmissiveHDRBoost, "EmissiveHDRBoost", runtime);

      if (ImGui::SliderFloat("Sun Shaft HDR Boost", &cb_luma_global_settings.GameSettings.SunShaftHDRBoost, 0.f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "SunShaftHDRBoost", cb_luma_global_settings.GameSettings.SunShaftHDRBoost);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Makes sun shafts and glare brighter near the sun, into HDR. 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.SunShaftHDRBoost, default_luma_global_game_settings.SunShaftHDRBoost, "SunShaftHDRBoost", runtime);
#endif

      ImGui::Separator();
      ImGui::TextUnformatted("Color Grading");
#if DEVELOPMENT || TEST
      if (ImGui::SliderFloat("Film Grain Strength", &cb_luma_global_settings.GameSettings.FilmGrainStrength, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength);
      DrawResetButton(cb_luma_global_settings.GameSettings.FilmGrainStrength, default_luma_global_game_settings.FilmGrainStrength, "FilmGrainStrength", runtime);
#endif

      if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.25f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Overall image brightness multiplier. 1 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, default_luma_global_game_settings.Exposure, "Exposure", runtime);

      if (ImGui::SliderFloat("Contrast", &cb_luma_global_settings.GameSettings.Contrast, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Contrast around mid gray. 1 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Contrast, default_luma_global_game_settings.Contrast, "Contrast", runtime);

      if (ImGui::SliderFloat("Saturation", &cb_luma_global_settings.GameSettings.Saturation, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Color intensity. 1 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Saturation, default_luma_global_game_settings.Saturation, "Saturation", runtime);

      if (ImGui::SliderFloat("Highlights", &cb_luma_global_settings.GameSettings.Highlights, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Brightness of the highlights only. 1 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Highlights, default_luma_global_game_settings.Highlights, "Highlights", runtime);

      if (ImGui::SliderFloat("Shadows", &cb_luma_global_settings.GameSettings.Shadows, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Brightness of the shadows only. 1 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.Shadows, default_luma_global_game_settings.Shadows, "Shadows", runtime);

#if DEVELOPMENT || TEST
      if (ImGui::SliderFloat("Black Floor Reduction", &cb_luma_global_settings.GameSettings.BlackFloorRemoval, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "BlackFloorRemoval", cb_luma_global_settings.GameSettings.BlackFloorRemoval);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Removes the raised black level of the color grading, fading out toward mid gray (keeps shadow tint). 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.BlackFloorRemoval, default_luma_global_game_settings.BlackFloorRemoval, "BlackFloorRemoval", runtime);

      if (ImGui::SliderFloat("Highlight Rolloff", &cb_luma_global_settings.GameSettings.HighlightRolloff, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightRolloff", cb_luma_global_settings.GameSettings.HighlightRolloff);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("0 = grade continues into HDR highlights, 1 = vanilla SDR highlight rolloff");
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightRolloff, default_luma_global_game_settings.HighlightRolloff, "HighlightRolloff", runtime);
#endif

#if DEVELOPMENT || TEST
      ImGui::Separator();
      ImGui::TextUnformatted("Atmosphere");
      int haze_model = cb_luma_global_settings.GameSettings.HazeModel >= 0.5f ? 1 : 0;
      if (ImGui::Combo("Haze Model", &haze_model, "Curve blend\0Linear HDR experiment\0"))
      {
         cb_luma_global_settings.GameSettings.HazeModel = (float)haze_model;
         reshade::set_config_value(runtime, NAME, "HazeModel", cb_luma_global_settings.GameSettings.HazeModel);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.HazeModel, default_luma_global_game_settings.HazeModel, "HazeModel", runtime);
      haze_model = cb_luma_global_settings.GameSettings.HazeModel >= 0.5f ? 1 : 0;
      const float haze_max = cb_luma_global_settings.GameSettings.TonemapVersion != 0u && haze_model == 1 ? 2.f : 1.f;
      float displayed_haze = std::clamp(cb_luma_global_settings.GameSettings.DistanceHaze, 0.f, haze_max);
      if (ImGui::SliderFloat("Distance Haze", &displayed_haze, 0.f, haze_max, "%.3f"))
      {
         cb_luma_global_settings.GameSettings.DistanceHaze = displayed_haze;
         reshade::set_config_value(runtime, NAME, "DistanceHaze", displayed_haze);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the game's distance haze. 1 = vanilla; up to 2 is available with the linear HDR model.");
      DrawResetButton(cb_luma_global_settings.GameSettings.DistanceHaze, default_luma_global_game_settings.DistanceHaze, "DistanceHaze", runtime);
#endif

#if DEVELOPMENT || TEST

      if (ImGui::SliderFloat("Highlight Hue Preservation", &cb_luma_global_settings.GameSettings.HighlightHuePreservation, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightHuePreservation", cb_luma_global_settings.GameSettings.HighlightHuePreservation);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Keeps hues in bright highlights instead of per-channel shifts. 0 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightHuePreservation, default_luma_global_game_settings.HighlightHuePreservation, "HighlightHuePreservation", runtime);

      if (ImGui::SliderFloat("Vanilla Hue Restore", &cb_luma_global_settings.GameSettings.VanillaHueRestore, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "VanillaHueRestore", cb_luma_global_settings.GameSettings.VanillaHueRestore);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Pulls HDR colors toward the vanilla grade's hue and saturation (fixes over-saturated highlights)");
      DrawResetButton(cb_luma_global_settings.GameSettings.VanillaHueRestore, default_luma_global_game_settings.VanillaHueRestore, "VanillaHueRestore", runtime);

      int tonemap_version = (int)cb_luma_global_settings.GameSettings.TonemapVersion;
      if (ImGui::Combo("Tonemapper", &tonemap_version, "Legacy (extended LUT)\0Neutral LUT upgrade\0"))
      {
         cb_luma_global_settings.GameSettings.TonemapVersion = tonemap_version != 0 ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "TonemapVersion", cb_luma_global_settings.GameSettings.TonemapVersion);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Neutral LUT upgrade grades a compressed copy of the scene with the game's LUT and carries the grade onto the HDR image, so the final display mapping is the only highlight compression.");
      DrawResetButton(cb_luma_global_settings.GameSettings.TonemapVersion, default_luma_global_game_settings.TonemapVersion, "TonemapVersion", runtime);

      int auto_exposure_mode = (int)cb_luma_global_settings.GameSettings.AutoExposureMode;
      if (ImGui::Combo("Auto Exposure", &auto_exposure_mode, "Vanilla\0Luma Histogram\0Luma Adaptive\0"))
      {
         cb_luma_global_settings.GameSettings.AutoExposureMode = (uint32_t)std::clamp(auto_exposure_mode, 0, 2);
         reshade::set_config_value(runtime, NAME, "AutoExposureMode", cb_luma_global_settings.GameSettings.AutoExposureMode);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Vanilla = game's exposure. Luma Histogram = full-frame GPU histogram. Luma Adaptive = zone-weighted color metering with independent highlight tracking; currently color-only pending depth trace validation");
      DrawResetButton(cb_luma_global_settings.GameSettings.AutoExposureMode, default_luma_global_game_settings.AutoExposureMode, "AutoExposureMode", runtime);

      if (cb_luma_global_settings.GameSettings.AutoExposureMode != 0u)
      {
         if (ImGui::SliderFloat("Auto Exposure Speed", &cb_luma_global_settings.GameSettings.AutoExposureSpeed, 0.1f, 10.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "AutoExposureSpeed", cb_luma_global_settings.GameSettings.AutoExposureSpeed);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("How fast the exposure adapts to brightness changes");
         DrawResetButton(cb_luma_global_settings.GameSettings.AutoExposureSpeed, default_luma_global_game_settings.AutoExposureSpeed, "AutoExposureSpeed", runtime);

         if (ImGui::SliderFloat("Auto Exposure Range", &cb_luma_global_settings.GameSettings.AutoExposureRange, 0.f, 4.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "AutoExposureRange", cb_luma_global_settings.GameSettings.AutoExposureRange);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Stops of exposure change allowed around the vanilla exposure");
         DrawResetButton(cb_luma_global_settings.GameSettings.AutoExposureRange, default_luma_global_game_settings.AutoExposureRange, "AutoExposureRange", runtime);

         if (ImGui::SliderFloat("Auto Exposure Target", &cb_luma_global_settings.GameSettings.AutoExposureKey, 0.02f, 0.4f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "AutoExposureKey", cb_luma_global_settings.GameSettings.AutoExposureKey);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Scene brightness the Luma meter aims for (higher = brighter). 0.11 matches vanilla.");
         DrawResetButton(cb_luma_global_settings.GameSettings.AutoExposureKey, default_luma_global_game_settings.AutoExposureKey, "AutoExposureKey", runtime);
      }

      if (cb_luma_global_settings.GameSettings.TonemapVersion == 0u)
      {
         if (ImGui::SliderFloat("Highlight Compression", &cb_luma_global_settings.GameSettings.HighlightCompression, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "HighlightCompression", cb_luma_global_settings.GameSettings.HighlightCompression);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Soft-compresses bright highlights. 0 = off");
         DrawResetButton(cb_luma_global_settings.GameSettings.HighlightCompression, default_luma_global_game_settings.HighlightCompression, "HighlightCompression", runtime);
      }

      if (ImGui::SliderFloat("Top Gradient", &cb_luma_global_settings.GameSettings.TopGradientStrength, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "TopGradientStrength", cb_luma_global_settings.GameSettings.TopGradientStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the warm color the game adds to the top of the screen (raises blacks there). 1 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.TopGradientStrength, default_luma_global_game_settings.TopGradientStrength, "TopGradientStrength", runtime);

      if (ImGui::SliderFloat("Vignette", &cb_luma_global_settings.GameSettings.VignetteStrength, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "VignetteStrength", cb_luma_global_settings.GameSettings.VignetteStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the game's tinted vignette. 1 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.VignetteStrength, default_luma_global_game_settings.VignetteStrength, "VignetteStrength", runtime);

      if (ImGui::SliderFloat("Hue Preserving Grading", &cb_luma_global_settings.GameSettings.ShadowLuminanceGrading, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowLuminanceGrading", cb_luma_global_settings.GameSettings.ShadowLuminanceGrading);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Grades brightness by luminance and keeps hues (with the grade tint), taking saturation from the original per-channel grade (RenoDX HITMAN method). 0 = vanilla per-channel");
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowLuminanceGrading, default_luma_global_game_settings.ShadowLuminanceGrading, "ShadowLuminanceGrading", runtime);

      if (ImGui::SliderFloat("Hue Preserving Gamma", &cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GammaCorrectionByLuminance", cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Applies the gamma 2.0 to 2.2 correction by luminance instead of per channel (no saturation/hue shift). 0 = per channel");
      DrawResetButton(cb_luma_global_settings.GameSettings.GammaCorrectionByLuminance, default_luma_global_game_settings.GammaCorrectionByLuminance, "GammaCorrectionByLuminance", runtime);

      if (ImGui::SliderFloat("Gamut Expansion", &cb_luma_global_settings.GameSettings.GamutExpansion, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GamutExpansion", cb_luma_global_settings.GameSettings.GamutExpansion);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Expands already saturated colors beyond BT.709 into wide gamut, keeping hue and brightness. Neutrals and skin tones are protected. 0 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.GamutExpansion, default_luma_global_game_settings.GamutExpansion, "GamutExpansion", runtime);

      if (ImGui::SliderFloat("Gamut Expansion Hue Lock", &cb_luma_global_settings.GameSettings.GamutExpansionHueLock, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GamutExpansionHueLock", cb_luma_global_settings.GameSettings.GamutExpansionHueLock);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Keeps the game's original hues when expanding the gamut (1 = fully locked)");
      DrawResetButton(cb_luma_global_settings.GameSettings.GamutExpansionHueLock, default_luma_global_game_settings.GamutExpansionHueLock, "GamutExpansionHueLock", runtime);

      int display_mapping = (int)cb_luma_global_settings.GameSettings.DisplayMapping;
      if (ImGui::Combo("Display Mapping", &display_mapping, "DICE (per channel)\0Reinhard Piecewise\0BT.2390 (adaptive)\0"))
      {
         cb_luma_global_settings.GameSettings.DisplayMapping = (uint32_t)display_mapping;
         reshade::set_config_value(runtime, NAME, "DisplayMapping", cb_luma_global_settings.GameSettings.DisplayMapping);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Tone mapping of the display peak. DICE = per channel (vanilla-like). Reinhard Piecewise = by luminance with highlight desaturation control. BT.2390: perceptual roll-off from the scene's measured brightest point to the display peak.");
      DrawResetButton(cb_luma_global_settings.GameSettings.DisplayMapping, default_luma_global_game_settings.DisplayMapping, "DisplayMapping", runtime);

      if (cb_luma_global_settings.GameSettings.DisplayMapping == 1u)
      {
         if (ImGui::SliderFloat("Shoulder Start", &cb_luma_global_settings.GameSettings.ShoulderStart, 0.1f, 0.9f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "ShoulderStart", cb_luma_global_settings.GameSettings.ShoulderStart);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fraction of the display peak where highlight compression starts. Higher = more of the image untouched, sharper roll-off");
         DrawResetButton(cb_luma_global_settings.GameSettings.ShoulderStart, default_luma_global_game_settings.ShoulderStart, "ShoulderStart", runtime);

         if (ImGui::SliderFloat("Highlight Saturation (Display Mapping)", &cb_luma_global_settings.GameSettings.HighlightPurity, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "HighlightPurity", cb_luma_global_settings.GameSettings.HighlightPurity);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Saturation of compressed highlights taken from a per-channel pass. 0 = pure luminance mapping (keeps saturation, may look neon), 1 = natural highlight desaturation");
         DrawResetButton(cb_luma_global_settings.GameSettings.HighlightPurity, default_luma_global_game_settings.HighlightPurity, "HighlightPurity", runtime);
      }

      const char* tonemapped_resolve_items[] = { "Off", "Standard", "2x2 grid (NVIDIA)" };
      int tonemapped_resolve = (int)(g_tonemapped_resolve > 2u ? 2u : g_tonemapped_resolve);
      if (ImGui::Combo("Tonemapped MSAA Resolve", &tonemapped_resolve, tonemapped_resolve_items, IM_ARRAYSIZE(tonemapped_resolve_items)))
      {
         g_tonemapped_resolve = (uint32_t)tonemapped_resolve;
         reshade::set_config_value(runtime, NAME, "TonemappedResolve", g_tonemapped_resolve);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Resolves the 4x MSAA HDR scene with a tonemapped weighted average instead of the hardware resolve. Off = native resolve. Standard = sample layout 0..3; 2x2 grid = NVIDIA layout (samples in a 2x2 grid in sample 0)");

      bool gbuffer_clamp = g_gbuffer_clamp != 0u;
      if (ImGui::Checkbox("Clamp G-Buffers (fixes light blowouts)", &gbuffer_clamp))
      {
         g_gbuffer_clamp = gbuffer_clamp ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "GBufferClamp", g_gbuffer_clamp);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Clamps the deferred G-buffers to 0..1 after they are filled, as the vanilla 8-bit targets did. Fixes light blowouts from the fp16 upgrade");
#endif

      ImGui::Separator();
#if DEVELOPMENT || TEST
      ImGui::TextUnformatted("Rendering");
#else
      ImGui::TextUnformatted("Post Process");
      if (ImGui::SliderFloat("Film Grain Strength", &cb_luma_global_settings.GameSettings.FilmGrainStrength, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength);
      DrawResetButton(cb_luma_global_settings.GameSettings.FilmGrainStrength, default_luma_global_game_settings.FilmGrainStrength, "FilmGrainStrength", runtime);
      int haze_model = cb_luma_global_settings.GameSettings.HazeModel >= 0.5f ? 1 : 0;
      const float haze_max = cb_luma_global_settings.GameSettings.TonemapVersion != 0u && haze_model == 1 ? 2.f : 1.f;
      float displayed_haze = std::clamp(cb_luma_global_settings.GameSettings.DistanceHaze, 0.f, haze_max);
      if (ImGui::SliderFloat("Distance Haze", &displayed_haze, 0.f, haze_max, "%.3f"))
      {
         cb_luma_global_settings.GameSettings.DistanceHaze = displayed_haze;
         reshade::set_config_value(runtime, NAME, "DistanceHaze", displayed_haze);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Strength of the game's distance haze. 1 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.DistanceHaze, default_luma_global_game_settings.DistanceHaze, "DistanceHaze", runtime);
#endif

      ImGui::Separator();
      ImGui::TextUnformatted("Image Quality");
#if DEVELOPMENT || TEST
      int anti_aliasing_mode = (int)g_anti_aliasing_mode;
      if (ImGui::Combo("Anti Aliasing", &anti_aliasing_mode, "FXAA (vanilla)\0SMAA\0"))
      {
         g_anti_aliasing_mode = (uint32_t)anti_aliasing_mode;
         reshade::set_config_value(runtime, NAME, "AntiAliasingMode", g_anti_aliasing_mode);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA. 0 = vanilla FXAA");

#else
      bool smaa = g_anti_aliasing_mode == 1u;
      if (ImGui::Checkbox("SMAA", &smaa))
      {
         g_anti_aliasing_mode = smaa ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "AntiAliasingMode", g_anti_aliasing_mode);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA.");
#endif
      DrawResetButton(g_anti_aliasing_mode, 1u, "AntiAliasingMode", runtime);

#if DEVELOPMENT || TEST
      if (ImGui::SliderFloat("GI Strength", &cb_luma_global_settings.GameSettings.GIStrength, 0.f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GIStrength", cb_luma_global_settings.GameSettings.GIStrength);
      DrawResetButton(cb_luma_global_settings.GameSettings.GIStrength, default_luma_global_game_settings.GIStrength, "GIStrength", runtime);
      if (ImGui::SliderFloat("GI Occlusion", &cb_luma_global_settings.GameSettings.GIOcclusion, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GIOcclusion", cb_luma_global_settings.GameSettings.GIOcclusion);
      DrawResetButton(cb_luma_global_settings.GameSettings.GIOcclusion, default_luma_global_game_settings.GIOcclusion, "GIOcclusion", runtime);

      if (ImGui::SliderFloat("AO Darkness", &cb_luma_global_settings.GameSettings.AODarkness, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "AODarkness", cb_luma_global_settings.GameSettings.AODarkness);
      DrawResetButton(cb_luma_global_settings.GameSettings.AODarkness, default_luma_global_game_settings.AODarkness, "AODarkness", runtime);
#endif

      if (ImGui::Checkbox("XeGTAO", &g_enable_xegtao))
      {
         if (!g_enable_xegtao)
            HMAXeGTAO::Clear(GetGameDeviceData(device_data).gtao_data);
         reshade::set_config_value(runtime, NAME, "EnableXeGTAO", g_enable_xegtao);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's SSAO with XeGTAO ambient occlusion. Quality is in the advanced settings (XE_GTAO_QUALITY)");
      DrawResetButton(g_enable_xegtao, true, "EnableXeGTAO", runtime);
#if DEVELOPMENT || TEST
      if (g_enable_xegtao)
      {
         if (ImGui::SliderFloat("XeGTAO Radius", &g_xegtao_radius, 0.05f, 5.f, "%.2f m", ImGuiSliderFlags_Logarithmic))
            reshade::set_config_value(runtime, NAME, "XeGTAORadius", g_xegtao_radius);
         if (ImGui::SliderFloat("XeGTAO Intensity", &g_xegtao_intensity, 0.5f, 5.f, "%.2f", ImGuiSliderFlags_Logarithmic))
            reshade::set_config_value(runtime, NAME, "XeGTAOIntensity", g_xegtao_intensity);
         int xegtao_mode = g_xegtao_vbao ? 1 : 0;
         if (ImGui::Combo("AO Mode", &xegtao_mode, "GTAO\0GT-VBAO\0"))
         {
            g_xegtao_vbao = xegtao_mode != 0;
            reshade::set_config_value(runtime, NAME, "XeGTAOMode", xegtao_mode);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("GTAO: horizon angle integration. GT-VBAO: visibility bitmask (32 sectors per slice) with a constant surface thickness");
         if (g_xegtao_vbao)
         {
            if (ImGui::SliderFloat("VBAO Thickness", &g_xegtao_thickness, 0.05f, 5.f, "%.2f m", ImGuiSliderFlags_Logarithmic))
               reshade::set_config_value(runtime, NAME, "XeGTAOThickness", g_xegtao_thickness);
         }
      }

#else
      bool vbao = g_enable_xegtao && g_xegtao_vbao;
      ImGui::BeginDisabled(!g_enable_xegtao);
      if (ImGui::Checkbox("GT-VBAO", &vbao))
      {
         g_xegtao_vbao = vbao;
         int xegtao_mode = vbao ? 1 : 0;
         reshade::set_config_value(runtime, NAME, "XeGTAOMode", xegtao_mode);
      }
      DrawResetButton(g_xegtao_vbao, true, "XeGTAOMode", runtime);
      ImGui::EndDisabled();
#endif

      bool bokeh_dof = cb_luma_global_settings.GameSettings.BokehDOF != 0;
      if (ImGui::Checkbox("Bokeh Depth of Field", &bokeh_dof))
      {
         cb_luma_global_settings.GameSettings.BokehDOF = bokeh_dof ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "BokehDOF", cb_luma_global_settings.GameSettings.BokehDOF);
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's half-res depth of field with a full-res bokeh blur using the game's focus. Off = vanilla DOF.");
      DrawResetButton(cb_luma_global_settings.GameSettings.BokehDOF, default_luma_global_game_settings.BokehDOF, "BokehDOF", runtime);

#if DEVELOPMENT || TEST
      if (cb_luma_global_settings.GameSettings.BokehDOF != 0u)
      {
         bool bokeh_filtering = cb_luma_global_settings.GameSettings.BokehDOFFiltering != 0u;
         if (ImGui::Checkbox("Depth-Aware Bokeh Filtering", &bokeh_filtering))
         {
            cb_luma_global_settings.GameSettings.BokehDOFFiltering = bokeh_filtering ? 1u : 0u;
            reshade::set_config_value(runtime, NAME, "BokehDOFFiltering", cb_luma_global_settings.GameSettings.BokehDOFFiltering);
         }
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use depth-aware filtering to reduce bokeh bleeding across depth boundaries.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BokehDOFFiltering, default_luma_global_game_settings.BokehDOFFiltering, "BokehDOFFiltering", runtime);

         if (ImGui::SliderFloat("Bokeh Sample Rotation", &cb_luma_global_settings.GameSettings.BokehDOFRotation, 0.f, 1.f, "%.3f"))
            reshade::set_config_value(runtime, NAME, "BokehDOFRotation", cb_luma_global_settings.GameSettings.BokehDOFRotation);
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adjust the bokeh sampling pattern rotation.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BokehDOFRotation, default_luma_global_game_settings.BokehDOFRotation, "BokehDOFRotation", runtime);

         int bokeh_tap_limit = (int)cb_luma_global_settings.GameSettings.BokehDOFTapLimit;
         if (ImGui::SliderInt("Bokeh Tap Limit", &bokeh_tap_limit, 8, 128))
         {
            cb_luma_global_settings.GameSettings.BokehDOFTapLimit = (uint32_t)bokeh_tap_limit;
            reshade::set_config_value(runtime, NAME, "BokehDOFTapLimit", cb_luma_global_settings.GameSettings.BokehDOFTapLimit);
         }
         if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher limits improve bokeh quality at greater GPU cost.");
         DrawResetButton(cb_luma_global_settings.GameSettings.BokehDOFTapLimit, default_luma_global_game_settings.BokehDOFTapLimit, "BokehDOFTapLimit", runtime);
      }
#endif

      ImGui::Separator();
      ImGui::TextUnformatted("Video");
      bool video_auto_hdr = cb_luma_global_settings.GameSettings.VideoAutoHDREnable != 0;
      if (ImGui::Checkbox("Video AutoHDR", &video_auto_hdr))
      {
         cb_luma_global_settings.GameSettings.VideoAutoHDREnable = video_auto_hdr ? 1u : 0u;
         reshade::set_config_value(runtime, NAME, "VideoAutoHDREnable", cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("HDR highlight expansion for pre-rendered videos. Off = vanilla SDR videos.");
      DrawResetButton(cb_luma_global_settings.GameSettings.VideoAutoHDREnable, default_luma_global_game_settings.VideoAutoHDREnable, "VideoAutoHDREnable", runtime);
      ImGui::BeginDisabled(cb_luma_global_settings.GameSettings.VideoAutoHDREnable == 0u);
      if (ImGui::SliderFloat("Video AutoHDR Strength", &cb_luma_global_settings.GameSettings.VideoAutoHDR, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "VideoAutoHDR", cb_luma_global_settings.GameSettings.VideoAutoHDR);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("How far video highlights are pushed toward the display peak. 0 = vanilla");
      DrawResetButton(cb_luma_global_settings.GameSettings.VideoAutoHDR, default_luma_global_game_settings.VideoAutoHDR, "VideoAutoHDR", runtime);
      ImGui::EndDisabled();
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Hitman Absolution\" is developed by Ostad Gaming and is open source and free.\n"
         "It adds true HDR (Neutral LUT tonemapper with BT.2390 display mapping, windowed/borderless required), auto-exposure, emissive/sky HDR boosts, SMAA anti-aliasing, XeGTAO ambient occlusion, bokeh depth of field, lens flares, and video AutoHDR, on top of the game's upgraded fp16 rendering pipeline.\n"
         "Do NOT run another HDR mod (e.g. RenoDX) alongside it.\n"
         "Thanks to the Luma team and contributors.\n"
         "If you enjoy it, consider donating.");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link = std::string("Buy Ostad Gaming a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link.c_str()))
         ShellExecuteA(nullptr, "open", "https://ko-fi.com/ostadgaming", nullptr, nullptr, SW_SHOWNORMAL);
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
         static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
         ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nOstad Gaming"
                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nRenoDX (HITMAN WoA hue-preserving grading method)"
                  "\nDICE.hlsl (HDR display mapping)"
                  "\nSMAA (Iryoku)"
                  "\nIntel FidelityFX XeGTAO (ambient occlusion)"
                  "\nAMD FidelityFX (RCAS)"
                  "\nFroyok (Pretty Post Process lens flare method)");
   }
};

// This is where everything starts from, the very first call to the dll.
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // Setup the globals (e.g. name etc). It's good to do this stuff before registering the ReShade addon, to make sure the names are up to date.
      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name; // Remove the potential "_" at the beginning. This can include spaces!

      uint32_t mod_version = 2; // Increase this to reset the game settings and shader binaries after making large changes to your mod (2: removed the SunBrightness setting, changing the GameSettings cbuffer layout)
      Globals::SetGlobals(cleared_project_name, "Hitman Absolution Luma mod", "https://ko-fi.com/ostadgaming", mod_version);
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;

      // The following can be toggled in the dev settings (it generally only fully applies after changing the game's resolution).
      // Default these to "false" for the mod to not do "anything" that might cause issues with the game by default.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      prevent_fullscreen_state = false; // HMA crashes (MSVCR100 0xc0000417) when FSE is blocked
      force_borderless = true;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Texture upgrades (8 bit unorm and 11 bit float etc to 16 bit float)
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_unorm_srgb,
            reshade::api::format::b8g8r8x8_typeless,

            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,

            //reshade::api::format::r16g16b16a16_unorm,

            reshade::api::format::r11g11b10_float,
      };
      // Set this to the size and type of the main color grading LUT you might want to upgrade (optional)
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;
		// In case the textures failed to upgrade, tweak the filtering conditions to be more lenient (e.g. aspect ratio checks etc).
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      // Update samples to override the bias, based on the rendering resolution etc
      enable_samplers_upgrade = false;

#if DEVELOPMENT // If you want to track any shader names over time, you can hardcode them here by hash (they can be a useful reference in the pipeline)
      forced_shader_names.emplace(std::stoul("FD2925B4", nullptr, 16), "Tracked Shader Name");
#endif

#if !DEVELOPMENT // Put shaders that a previous version of the mod used but has ever since been deleted here, so that users updating the mod from an older version won't accidentally load them
      old_shader_file_names.emplace("Bloom_0xDC9373A8.ps_5_0.hlsl");
#endif

      // Create your game sub-class instance (it will be automatically destroyed on exit).
      // You do not need to do this if you have no custom data to store.
      game = new TemplateGame();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
#if DEVELOPMENT
      HMABackgroundInput::Uninstall();
#endif
      HMAHardPause::Resume();
      reshade::unregister_event<reshade::addon_event::resolve_texture_region>(HMA_OnResolveTextureRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
