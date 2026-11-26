// Brütal Legend - Luma HDR mod (Double Fine Lua engine, 32-bit, DX9 -> D3D11 via dgVoodoo2).
// Shader hashes are translated by dgVoodoo and change with the wrapper build. The hashes here are from dgVoodoo 2.87.5/2.87.6 (ps_5_0).
// Any other build needs its shaders re-dumped.

// The DEVELOPMENT auto-debugger MessageBox on DLL attach is disabled: it is invisible under borderless/fullscreen
// and blocks the loader, so ReShade times out loading the addon (error 1114).
#define DISABLE_AUTO_DEBUGGER 1

#define GAME_BRUTAL_LEGEND 1

//// Define all the global "core" defines before including its files: ////

// Enable support for Geometry Shaders (GS), rather rare.
#define GEOMETRY_SHADER_SUPPORT 0

// Enable Luma's SMAA (core registers its passes; used by "Replace FXAA with SMAA", see DrawSMAAForFXAA()).
#define ENABLE_SMAA 1

// Enable Luma's bloom (core DrawBloom): "Luma Bloom" option, see Shaders/Brutal Legend/Luma_Bloom_impl.hlsl.
#define ENABLE_BLOOM 1

// Release builds only pass original_draw_dispatch_func to OnDrawOrDispatch with this (XeGTAO, PBR highlights, A2C, shadows need it).
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

#include <algorithm>
#include <shellapi.h> // ShellExecuteA for About links (system("start ...") hangs the render thread in exclusive fullscreen)
#include <imagehlp.h> // CheckSumMappedFile for the 4GB patch
#pragma comment(lib, "imagehlp.lib")
#include <atomic>
#include <string>
#include <vector>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#if DEVELOPMENT
#include <set>
#include <tuple>
#endif

// XeGTAO replaces the game's AO generation/blur/upsample (see gtao.h and Shaders/Brutal Legend/Luma_BL_XeGTAO.hlsl).
#include "gtao.h"

#include "sdl_redirect.h"
#include "fmod_upgrade.h"
#include "sim_tick.h"
#include "dgvoodoo_msaa.h"
#include "alpha_test_shaders.h"
#include "alpha_to_coverage_patch.h"
#include "spec_highlights_patch.h"
#include "draw_distance.h"
#include "d3d9_call_filter.h"

// Brutal Legend: D3D9 command-stream glue. [BL CS] lines go through ReShade's log.
static void BLCsLog(int level, const char* msg)
{
   reshade::log::message(level == 2 ? reshade::log::level::error : (level == 1 ? reshade::log::level::warning : reshade::log::level::info), msg);
}

// D3D9 command-stream dev values (MCP). Plain ints because Mcp binds int*; copied into the std::atomic globals every OnPresent.
static int g_cs_dev_mode = 0;
static int g_cs_dev_max_frames = 1;
static int g_cs_dev_staging = 1;
static int g_cs_dev_shadow = 1;
static int g_cs_dev_async_refs = 1;
static int g_cs_dev_async_sb = 1;
static int g_cs_dev_coalesce = 0; // Dev value: command coalescing (0 = off)
static int g_cs_dev_writewatch = 1; // 1 = upload only the write-watch dirty span of big dynamic buffers; 0 = whole range

// User setting "Multithreaded D3D9 (experimental)" (config key "CsMultithreaded"): live, default off.
static bool g_cs_user_threaded = false;

static void BLCsSyncDevValues()
{
   g_cs_async_sb.store(g_cs_dev_async_sb, std::memory_order_relaxed);
#if DEVELOPMENT
   // Dev value d3d9.cs wins when non-zero; otherwise the user setting decides.
   const int cs_mode = g_cs_dev_mode != 0 ? g_cs_dev_mode : (g_cs_user_threaded ? 1 : 0);
#else
   const int cs_mode = g_cs_user_threaded ? 1 : 0;
#endif
   g_cs_mode_request.store(cs_mode, std::memory_order_relaxed);
   g_cs_max_frames.store(g_cs_dev_max_frames, std::memory_order_relaxed);
   g_cs_staging.store(g_cs_dev_staging, std::memory_order_relaxed);
   g_cs_shadow.store(g_cs_dev_shadow, std::memory_order_relaxed);
   g_cs_async_refs.store(g_cs_dev_async_refs, std::memory_order_relaxed);
   g_cs_coalesce.store(g_cs_dev_coalesce, std::memory_order_relaxed);
   g_cs_writewatch.store(g_cs_dev_writewatch, std::memory_order_relaxed);
}

// Locates and hooks the device vtable (command stream), then installs the call filter on top of it. Runs once.
// Needs TryPatchDgVoodooGetTextureBounds() to have recorded its site. Nothing is patched when the locator fails.
static void BLInstallD3D9Cs()
{
   static bool attempted = false;
   if (attempted || g_dgv_gettexture_site == 0)
      return;
   attempted = true;
   g_cs_log = &BLCsLog;

   HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
   void** vtable = d3d9 != nullptr ? InstallD3D9Cs(d3d9, &BLDgvGetTextureSlotAccept) : nullptr;
   InstallD3D9CallFilter(vtable);
}

// Persisted setting widgets: the control, a save to the config on change, a tooltip (also shown
// while disabled) and Core's reset button. Each returns true when the value changed this frame (or was reset).
static bool DrawSettingCheckbox(const char* label, const char* key, bool& value, bool default_value, const char* tooltip)
{
   bool changed = ImGui::Checkbox(label, &value);
   if (changed)
      reshade::set_config_value(nullptr, NAME, key, value);
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", tooltip);
   bool reset = DrawResetButton(value, default_value, key);
   return changed || reset;
}

static bool DrawSettingSlider(const char* label, const char* key, float& value, float default_value, float min_value, float max_value, const char* tooltip)
{
   bool changed = ImGui::SliderFloat(label, &value, min_value, max_value, "%.2f");
   if (changed)
      reshade::set_config_value(nullptr, NAME, key, value);
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", tooltip);
   bool reset = DrawResetButton(value, default_value, key);
   return changed || reset;
}

static bool DrawSettingCombo(const char* label, const char* key, int& value, int default_value, const char* const* items, int item_count, const char* tooltip)
{
   bool changed = ImGui::Combo(label, &value, items, item_count);
   if (changed)
      reshade::set_config_value(nullptr, NAME, key, value);
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", tooltip);
   bool reset = DrawResetButton(value, default_value, key);
   return changed || reset;
}

// dgVoodoo2 2.87.5/2.87.6 GetTexture fix. Its GetTexture indexes the device's bound-texture table with the caller's
// raw Stage. The game probes stages 257-260 (0x100 + its internal slot ids); the MS runtime rejects those with
// D3DERR_INVALIDCALL, but dgVoodoo reads out of bounds and can fault in its type check when adjacent memory holds
// a freed resource pointer.
// The DLLs are packed on disk, so this patches the live image: a detour on the 7-byte unchecked table load into a
// nearby cave that bounds-checks Stage.
// The out-parameter is zeroed by the original code ahead of the patch site, so the rejected path returns a NULL
// texture, as the MS runtime does, and the game's null-checks skip the slot.
// If no unique signature match is found, the fix is logged and left unapplied.
static bool TryPatchDgVoodooGetTextureBounds()
{
   static bool done = false;
   if (done)
      return true;

   HMODULE module = GetModuleHandleW(L"d3d9.dll"); // only the game folder's d3d9.dll exists on this path (dgVoodoo)
   if (module == nullptr)
      return false; // not loaded yet: stay retryable (dgVoodoo normally loads before ReShade inits, so this is just belt-and-braces)

   // mov ecx,[esi+eax*4 - disp] ; pop esi ; test ecx,ecx ; je rel8/rel32 ; mov eax,[ecx+0x18] ; sub eax,3
   uint8_t* found = nullptr;
   int matches = 0;
   for (uint8_t* addr = static_cast<uint8_t*>(static_cast<void*>(module));;)
   {
      MEMORY_BASIC_INFORMATION mbi = {};
      if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
         break;
      if (mbi.AllocationBase != static_cast<void*>(module))
         break; // walked out of the mapped image
      if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0)
      {
         uint8_t* p = static_cast<uint8_t*>(mbi.BaseAddress);
         const uint8_t* const end = p + mbi.RegionSize;
         while (end - p >= 24)
         {
            if (p[0] == 0x8B && p[1] == 0x8C && p[2] == 0x86 && p[7] == 0x5E && p[8] == 0x85 && p[9] == 0xC9 &&
                ((p[10] == 0x74 && p[12] == 0x8B && p[13] == 0x41 && p[14] == 0x18 && p[15] == 0x83 && p[16] == 0xE8 && p[17] == 0x03) ||
                 (p[10] == 0x0F && p[11] == 0x84 && p[18] == 0x8B && p[19] == 0x41 && p[20] == 0x18 && p[21] == 0x83 && p[22] == 0xE8 && p[23] == 0x03)))
            {
               ++matches;
               found = p;
            }
            ++p;
         }
      }
      addr = static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
   }

   // Torn-write note: the 7-byte patch is written non-atomically, but this runs inside dgVoodoo's own
   // Direct3DCreate9 -> D3D11 device creation -> ReShade init -> OnInit, i.e. BEFORE the game received the
   // D3D9 object and started calling it from its render thread, so no thread can be executing the site.
   if (matches != 1)
   {
      done = true; // terminal: wrong dgVoodoo build, retrying per-draw would only spam the log
      reshade::log::message(reshade::log::level::warning, std::format("[BL] dgVoodoo GetTexture fix: signature matched {} time(s) (expected 1) - dgVoodoo build not recognized, level loads may crash", matches).c_str());
      return false;
   }

   const int32_t disp = *reinterpret_cast<int32_t*>(found + 3);
   uint8_t* const back = found + 7; // the displaced instruction's successor ("pop esi")

   // A rel32 detour needs the cave within +/-2GB of the patch site, and with LAA the default VirtualAlloc
   // can land above 2GB - probe candidates at fixed offsets outward from the site instead.
   // VirtualAlloc rounds a non-null address DOWN to the 64 KB allocation granularity, so align the candidates
   // ourselves or the returned pointer will never equal the hint.
   static constexpr uintptr_t granularity = 0x10000;
   static constexpr uintptr_t granularity_mask = granularity - 1;
   uint8_t* cave = nullptr;
   for (int k = 1; k <= 512 && cave == nullptr; ++k)
   {
      static constexpr size_t step = 0x40000; // 256 KB per step, 128 MB reach either way
      for (int direction = 0; direction < 2 && cave == nullptr; ++direction)
      {
         uintptr_t candidate_addr = direction == 0
            ? (reinterpret_cast<uintptr_t>(found) + step * k) & ~granularity_mask
            : (reinterpret_cast<uintptr_t>(found) - step * k) & ~granularity_mask;
         if (candidate_addr < granularity || candidate_addr > 0xFFFF0000u)
            continue;
         void* candidate = reinterpret_cast<void*>(candidate_addr);
         if (VirtualAlloc(candidate, 0x40, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE) == candidate)
            cave = static_cast<uint8_t*>(candidate);
      }
   }
   if (cave == nullptr)
   {
      done = true;
      reshade::log::message(reshade::log::level::warning, "[BL] dgVoodoo GetTexture fix: no reachable cave address, workaround inactive");
      return false;
   }

   uint8_t* w = cave;
   *w++ = 0x83; *w++ = 0xF8; *w++ = 0x10; // cmp eax, 16
   uint8_t* const jb = w;
   *w++ = 0x72; *w++ = 0x00; // jb load
   *w++ = 0x31; *w++ = 0xC9; // xor ecx, ecx (OOB stage: treat as unbound; out-param already zeroed upstream)
   *w++ = 0xE9;
   *reinterpret_cast<int32_t*>(w) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(back) - reinterpret_cast<uintptr_t>(w + 4));
   w += 4;
   uint8_t* const load = w;
   jb[1] = static_cast<int8_t>(load - (jb + 2));
   *w++ = 0x8B; *w++ = 0x8C; *w++ = 0x86; // mov ecx,[esi+eax*4 - disp] (the displaced original)
   *reinterpret_cast<int32_t*>(w) = disp;
   w += 4;
   *w++ = 0xE9;
   *reinterpret_cast<int32_t*>(w) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(back) - reinterpret_cast<uintptr_t>(w + 4));
   w += 4;

   DWORD old_protection = 0;
   if (!VirtualProtect(found, 7, PAGE_EXECUTE_READWRITE, &old_protection))
   {
      done = true;
      reshade::log::message(reshade::log::level::warning, "[BL] dgVoodoo GetTexture fix: VirtualProtect on the patch site failed, workaround inactive");
      return false;
   }
   found[0] = 0xE9;
   *reinterpret_cast<int32_t*>(found + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(cave) - reinterpret_cast<uintptr_t>(found + 5));
   found[5] = 0x90;
   found[6] = 0x90;
   DWORD ignored = 0;
   VirtualProtect(found, 7, old_protection, &ignored);
   FlushInstructionCache(GetCurrentProcess(), found, 7);
   FlushInstructionCache(GetCurrentProcess(), cave, static_cast<SIZE_T>(w - cave));

   reshade::log::message(reshade::log::level::info, std::format("[BL] dgVoodoo GetTexture fix: bounds-check detour applied at d3d9.dll+0x{:X} (stage table disp 0x{:X}, cave at site{:+d})",
      reinterpret_cast<uintptr_t>(found) - reinterpret_cast<uintptr_t>(module), disp & 0xFFFFFFFFu, static_cast<int>(reinterpret_cast<uintptr_t>(cave) - reinterpret_cast<uintptr_t>(found))).c_str());
   done = true;
   g_dgv_gettexture_site = reinterpret_cast<uintptr_t>(found);
   return true;
}

// Luma bloom sigmas (mip texels), scaled by LumaBloomRadius at draw time.
static constexpr float g_luma_bloom_sigmas[] = { 1.5f, 2.f, 2.f, 2.f, 1.f, 0.5f };

struct BrutalLegendDeviceData final : public GameDeviceData
{
   // Luma bloom: half-res prefilter of the tonemap's scene (cached per size), and core DrawBloom's output (t10 for the tonemap draw)
   ComPtr<ID3D11Texture2D> tex_luma_bloom_prefilter;
   ComPtr<ID3D11RenderTargetView> rtv_luma_bloom_prefilter;
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom_prefilter;
   UINT luma_bloom_prefilter_w = 0;
   UINT luma_bloom_prefilter_h = 0;
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom;

   // SMAA-for-FXAA scratch resources (DrawSMAAForFXAA), cached per scene size and format; recreated when those change
   ComPtr<ID3D11Texture2D> smaa_tex_linear, smaa_tex_gamma;
   ComPtr<ID3D11UnorderedAccessView> smaa_uav_linear, smaa_uav_gamma;
   ComPtr<ID3D11ShaderResourceView> smaa_srv_linear, smaa_srv_gamma;
   ComPtr<ID3D11Buffer> smaa_cb_metrics;
   UINT smaa_width = 0;
   UINT smaa_height = 0;
   DXGI_FORMAT smaa_format = DXGI_FORMAT_UNKNOWN;

   bool has_drawn_tonemap = false;

   // XeGTAO
   BLXeGTAO::Resources gtao_resources;
   bool gtao_ok_last_frame = false; // set by the AO upsample draw: XeGTAO produced this frame's AO
   bool gtao_generation_skipped_this_frame = false; // set when the game's AO generation/blur were skipped (reset in OnPresent)
   ComPtr<ID3D11ShaderResourceView> sky_depth_srv; // scene depth captured from the half-res particles draw, bound at t8 for the tonemap
   bool fog_cb_copied_this_frame = false; // the game's fog constant buffers were copied this frame (see OnDrawOrDispatch)
   ComPtr<ID3D11Buffer> fog_vs_cb; // GPU copy of the VS constant buffer in slot 4 (fog)
   ComPtr<ID3D11Buffer> fog_ps_cb; // GPU copy of the PS constant buffer in slot 4 (fog)

   // Shadow MSAA fix: the MS (Count > 1) shadow atlas caster target of this frame, and its Texture2DMS SRV (cached for the source texture)
   ComPtr<ID3D11Texture2D> ms_shadow_atlas;
   ComPtr<ID3D11Texture2D> ms_shadow_atlas_srv_source;
   ComPtr<ID3D11ShaderResourceView> ms_shadow_atlas_srv;
   bool ms_shadow_atlas_no_srv_flag_logged = false;
   uint32_t shadow_ms_bound = 0; // this frame: the MS atlas was bound to the shadow resolve
#if DEVELOPMENT
   uint32_t shadow_ms_bound_last = 0; // dev value "shadow.ms_bound"
#endif

   // Normal Map AF: upgraded sampler -> the game's original sampler (rebuilt from custom_sampler_by_original_sampler)
   struct OriginalSampler
   {
      ID3D11SamplerState* original = nullptr;
      bool is_clamp = false; // the original's AddressU is D3D11_TEXTURE_ADDRESS_CLAMP
   };
   std::mutex custom_to_original_mutex; // guards the members below (drawn from any thread)
   std::unordered_map<ID3D11SamplerState*, OriginalSampler> custom_to_original_samplers;
   size_t custom_to_original_source_size = SIZE_MAX; // custom_sampler_by_original_sampler size when last rebuilt
   bool custom_to_original_rebuilt_this_frame = false; // at most one miss-driven rebuild per frame
   uint32_t af_vanilla_swaps = 0; // samplers swapped back this frame (guarded by custom_to_original_mutex)
#if DEVELOPMENT
   uint32_t af_vanilla_swaps_last = 0; // last finished frame's count (dev value "af.vanilla_swaps")
#endif

   // Foliage Alpha to Coverage: original blend state -> clone with AlphaToCoverageEnable (the original is held so its address can't be reused)
   struct A2CBlendEntry
   {
      ComPtr<ID3D11BlendState> original;
      ComPtr<ID3D11BlendState> clone;
   };
   std::mutex a2c_mutex; // guards the members below (drawn from any thread)
   std::unordered_map<ID3D11BlendState*, A2CBlendEntry> a2c_blend_states; // key null = default blend desc
   uint32_t a2c_draws = 0; // draws this frame (guarded by a2c_mutex)
#if DEVELOPMENT
   uint32_t a2c_draws_last = 0; // last finished frame's count (dev value "a2c.draws")
#endif
   uint32_t a2c_patched_draws = 0; // draws with the patched alpha test shader this frame (guarded by a2c_mutex)
#if DEVELOPMENT
   uint32_t a2c_patched_draws_last = 0; // dev value "a2c.patched_draws"
#endif
   // Original pixel shader hash -> its alpha to coverage version (null if it couldn't be patched)
   std::mutex a2c_patch_mutex; // guards the members below
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> a2c_patched_shaders;
   uint32_t a2c_patch_failures = 0; // new hashes that failed this frame (guarded by a2c_patch_mutex)
#if DEVELOPMENT
   uint32_t a2c_patch_failures_last = 0; // dev value "a2c.patch_failures"
#endif

   // Physically based highlights (spec_highlights_patch.h): patched pixel shaders per (original hash << 1 | a2c flag), the keys that have no
   // specular site (or failed) as a negative cache, and counters. Both caches are cleared when the params change (see OnPresent).
   std::mutex spec_mutex; // guards the members below (drawn from any thread)
   std::unordered_map<uint64_t, ComPtr<ID3D11PixelShader>> spec_patched_shaders;
   std::unordered_set<uint64_t> spec_no_site_keys;
   SpecularHighlights::Params spec_params_applied; // the params the caches were built with
   uint32_t spec_patched_draws = 0; // draws with a patched specular shader this frame (guarded by spec_mutex)
#if DEVELOPMENT
   uint32_t spec_patched_draws_last = 0; // dev value "spec.patched_draws"
   uint32_t spec_shaders_last = 0; // dev value "spec.shaders" (cached patched count)
#endif
   uint32_t spec_patch_failures = 0; // new keys that failed this frame (guarded by spec_mutex)
#if DEVELOPMENT
   uint32_t spec_patch_failures_last = 0; // dev value "spec.patch_failures"
#endif
};

// Copies a whole constant buffer to a GPU-only buffer (no CPU readback). "dst" is (re)created when the size changes.
static void CopyConstantBufferForFog(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, ComPtr<ID3D11Buffer>& dst, ID3D11Buffer* src)
{
   D3D11_BUFFER_DESC src_desc = {};
   src->GetDesc(&src_desc);
   bool recreate = !dst;
   if (dst)
   {
      D3D11_BUFFER_DESC dst_desc = {};
      dst->GetDesc(&dst_desc);
      recreate = dst_desc.ByteWidth != src_desc.ByteWidth;
   }
   if (recreate)
   {
      D3D11_BUFFER_DESC desc = src_desc;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      desc.CPUAccessFlags = 0;
      desc.MiscFlags = 0;
      desc.StructureByteStride = 0;
      ComPtr<ID3D11Buffer> buffer;
      if (FAILED(native_device->CreateBuffer(&desc, nullptr, buffer.put())))
      {
         dst = nullptr;
         return;
      }
      dst = buffer;
   }
   native_device_context->CopyResource(dst.get(), src);
}

// Replaces the game's FXAA draw (pixel shader B78D5AEC, SRV2 = scene, RTV0 = output) with Luma's SMAA.
// The scene is gamma 2.2 encoded, sign mirrored and HDR: it is decoded to linear (HDR and negatives kept) for the blending,
// and a saturated gamma copy is used for edge detection. SMAA's output is encoded back to gamma 2.2 (see Luma_SMAA_impl.hlsl).
// Returns false, without changing any state, on any failure: the caller then runs the original FXAA draw.
static bool DrawSMAAForFXAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
{
   auto* bl_device_data = static_cast<BrutalLegendDeviceData*>(device_data.game);
   if (!bl_device_data)
      return false;

   ComPtr<ID3D11ShaderResourceView> srv_scene;
   native_device_context->PSGetShaderResources(2, 1, srv_scene.put());
   if (!srv_scene)
      return false;

   ComPtr<ID3D11RenderTargetView> rtv_output;
   native_device_context->OMGetRenderTargets(1, rtv_output.put(), nullptr);
   if (!rtv_output)
      return false;

   auto cs_it = device_data.native_compute_shaders.find(CompileTimeStringHash("BL SMAA Decode CS"));
   if (cs_it == device_data.native_compute_shaders.end() || !cs_it->second.get())
      return false;

   ComPtr<ID3D11Resource> scene_resource;
   srv_scene->GetResource(scene_resource.put());
   ComPtr<ID3D11Texture2D> scene_tex;
   if (!scene_resource || FAILED(scene_resource->QueryInterface(scene_tex.put())))
      return false;
   D3D11_TEXTURE2D_DESC tex_desc = {};
   scene_tex->GetDesc(&tex_desc);
   if (tex_desc.Width == 0 || tex_desc.Height == 0 || tex_desc.SampleDesc.Count != 1 || tex_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
      return false;

   ComPtr<ID3D11Resource> rtv_resource;
   rtv_output->GetResource(rtv_resource.put());
   ComPtr<ID3D11Texture2D> rtv_tex;
   if (!rtv_resource || FAILED(rtv_resource->QueryInterface(rtv_tex.put())))
      return false;
   D3D11_TEXTURE2D_DESC rtv_desc = {};
   rtv_tex->GetDesc(&rtv_desc);
   if (rtv_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || rtv_desc.Width != tex_desc.Width || rtv_desc.Height != tex_desc.Height)
      return false;

   // Predication: the captured scene depth (sky_depth_srv), only if it matches the scene. Otherwise no predication.
   ID3D11ShaderResourceView* srv_predication = nullptr;
   if (bl_device_data->sky_depth_srv)
   {
      ComPtr<ID3D11Resource> depth_resource;
      bl_device_data->sky_depth_srv->GetResource(depth_resource.put());
      ComPtr<ID3D11Texture2D> depth_tex;
      if (depth_resource && SUCCEEDED(depth_resource->QueryInterface(depth_tex.put())))
      {
         D3D11_TEXTURE2D_DESC depth_desc = {};
         depth_tex->GetDesc(&depth_desc);
         if (depth_desc.Format == DXGI_FORMAT_R32_FLOAT && depth_desc.SampleDesc.Count == 1 && depth_desc.Width == tex_desc.Width && depth_desc.Height == tex_desc.Height)
            srv_predication = bl_device_data->sky_depth_srv.get();
      }
   }

   // Scratch resources are cached in BrutalLegendDeviceData and only (re)created when missing or when the scene size/format changed.
   tex_desc.Usage = D3D11_USAGE_DEFAULT;
   tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
   tex_desc.CPUAccessFlags = 0;
   tex_desc.MiscFlags = 0;
   const bool smaa_recreate = !bl_device_data->smaa_tex_linear || !bl_device_data->smaa_tex_gamma || !bl_device_data->smaa_uav_linear || !bl_device_data->smaa_uav_gamma
      || !bl_device_data->smaa_srv_linear || !bl_device_data->smaa_srv_gamma || !bl_device_data->smaa_cb_metrics
      || bl_device_data->smaa_width != tex_desc.Width || bl_device_data->smaa_height != tex_desc.Height || bl_device_data->smaa_format != tex_desc.Format;
   if (smaa_recreate)
   {
      // SMAA metrics (1/size, size) for the metrics cbuffer at b1 (see Shaders/Brutal Legend/Luma_SMAA_impl.hlsl).
      const float smaa_metrics[4] = { 1.f / (float)tex_desc.Width, 1.f / (float)tex_desc.Height, (float)tex_desc.Width, (float)tex_desc.Height };
      D3D11_BUFFER_DESC cb_desc = {};
      cb_desc.ByteWidth = sizeof(smaa_metrics);
      cb_desc.Usage = D3D11_USAGE_IMMUTABLE;
      cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      D3D11_SUBRESOURCE_DATA cb_data = {};
      cb_data.pSysMem = smaa_metrics;

      const bool created = SUCCEEDED(native_device->CreateTexture2D(&tex_desc, nullptr, bl_device_data->smaa_tex_linear.put()))
         && SUCCEEDED(native_device->CreateTexture2D(&tex_desc, nullptr, bl_device_data->smaa_tex_gamma.put()))
         && SUCCEEDED(native_device->CreateUnorderedAccessView(bl_device_data->smaa_tex_linear.get(), nullptr, bl_device_data->smaa_uav_linear.put()))
         && SUCCEEDED(native_device->CreateUnorderedAccessView(bl_device_data->smaa_tex_gamma.get(), nullptr, bl_device_data->smaa_uav_gamma.put()))
         && SUCCEEDED(native_device->CreateShaderResourceView(bl_device_data->smaa_tex_linear.get(), nullptr, bl_device_data->smaa_srv_linear.put()))
         && SUCCEEDED(native_device->CreateShaderResourceView(bl_device_data->smaa_tex_gamma.get(), nullptr, bl_device_data->smaa_srv_gamma.put()))
         && SUCCEEDED(native_device->CreateBuffer(&cb_desc, &cb_data, bl_device_data->smaa_cb_metrics.put()));
      if (!created)
      {
         bl_device_data->smaa_tex_linear = nullptr;
         bl_device_data->smaa_tex_gamma = nullptr;
         bl_device_data->smaa_uav_linear = nullptr;
         bl_device_data->smaa_uav_gamma = nullptr;
         bl_device_data->smaa_srv_linear = nullptr;
         bl_device_data->smaa_srv_gamma = nullptr;
         bl_device_data->smaa_cb_metrics = nullptr;
         bl_device_data->smaa_width = 0;
         bl_device_data->smaa_height = 0;
         bl_device_data->smaa_format = DXGI_FORMAT_UNKNOWN;
         return false;
      }
      bl_device_data->smaa_width = tex_desc.Width;
      bl_device_data->smaa_height = tex_desc.Height;
      bl_device_data->smaa_format = tex_desc.Format;
   }

   // Local handles to the cached objects (same names as before, used by the passes below).
   ComPtr<ID3D11UnorderedAccessView> uav_linear = bl_device_data->smaa_uav_linear;
   ComPtr<ID3D11UnorderedAccessView> uav_gamma = bl_device_data->smaa_uav_gamma;
   ComPtr<ID3D11ShaderResourceView> srv_linear = bl_device_data->smaa_srv_linear;
   ComPtr<ID3D11ShaderResourceView> srv_gamma = bl_device_data->smaa_srv_gamma;
   ComPtr<ID3D11Buffer> cb_smaa_metrics = bl_device_data->smaa_cb_metrics;

   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

   // Decode pass: reads the scene, writes the linear copy (uav0) and the saturated gamma copy (uav1).
   ID3D11ShaderResourceView* srv_null = nullptr;
   native_device_context->PSSetShaderResources(2, 1, &srv_null); // The scene can't stay bound as an SRV while it's the CS input (and DrawSMAA rebinds PS SRVs anyway).
   ID3D11ShaderResourceView* cs_srv = srv_scene.get();
   native_device_context->CSSetShaderResources(0, 1, &cs_srv);
   const std::array<ID3D11UnorderedAccessView*, 2> cs_uavs = { uav_linear.get(), uav_gamma.get() };
   native_device_context->CSSetUnorderedAccessViews(0, cs_uavs.size(), cs_uavs.data(), nullptr);
   native_device_context->CSSetShader(cs_it->second.get(), nullptr, 0);
   native_device_context->Dispatch((tex_desc.Width + 8 - 1) / 8, (tex_desc.Height + 8 - 1) / 8, 1);

   // Unbind UAVs, SRVs and the shader.
   const std::array<ID3D11UnorderedAccessView*, 2> uav_nulls = {};
   native_device_context->CSSetUnorderedAccessViews(0, uav_nulls.size(), uav_nulls.data(), nullptr);
   native_device_context->CSSetShaderResources(0, 1, &srv_null);
   native_device_context->CSSetShader(nullptr, nullptr, 0);

   // SMAA: linear copy as the color input, gamma copy for edge detection, output into the game's FXAA target.
   DrawSMAA(native_device, native_device_context, device_data, rtv_output.get(), srv_linear.get(), srv_gamma.get(), srv_predication, cb_smaa_metrics.get());

   draw_state_stack.Restore(native_device_context);
   return true;
}

// Cursor hiding: the game window's WM_SETCURSOR is intercepted so the cursor is hidden over the client area.
HWND cursor_hook_window = nullptr;
WNDPROC cursor_prev_wndproc = nullptr;
std::atomic<bool> cursor_hide_enabled = true;

// Foliage Alpha to Coverage: the game's alpha test pixel shader with its alpha test replaced by a coverage alpha (see
// "alpha_to_coverage_patch.h"), patched from the bytecode Core keeps for the pipeline and cached per original hash.
// Null if it can't be patched, in which case the draw keeps the original shader.
static ComPtr<ID3D11PixelShader> GetAlphaToCoveragePixelShader(ID3D11Device* native_device, DeviceData& device_data, BrutalLegendDeviceData& game_device_data, reshade::api::pipeline pipeline, uint32_t hash)
{
   const std::lock_guard<std::mutex> lock(game_device_data.a2c_patch_mutex);
   if (const auto it = game_device_data.a2c_patched_shaders.find(hash); it != game_device_data.a2c_patched_shaders.end())
   {
      return it->second;
   }

   std::vector<uint8_t> patched;
   std::string error = "no bytecode";
   bool has_bytecode = false;
   {
      const std::shared_lock lock_pipelines(s_mutex_generic);
      if (const auto it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle); it != device_data.pipeline_cache_by_pipeline_handle.end() && it->second && it->second->subobjects_cache && it->second->subobject_count > 0)
      {
         const auto* desc = static_cast<const reshade::api::shader_desc*>(it->second->subobjects_cache[0].data);
         if (desc && desc->code && desc->code_size)
         {
            has_bytecode = true;
            const auto* code = static_cast<const uint8_t*>(desc->code);
            AlphaToCoverage::PatchAlphaTestToCoverage(code, desc->code_size, &patched, &error);
         }
      }
   }
   if (!has_bytecode)
      return nullptr; // Not cached: the pipeline may show up later

   ComPtr<ID3D11PixelShader> shader;
   if (!patched.empty() && FAILED(native_device->CreatePixelShader(patched.data(), patched.size(), nullptr, shader.put())))
   {
      shader = nullptr;
      error = "CreatePixelShader failed";
   }

   if (!shader)
   {
      game_device_data.a2c_patch_failures++;
      reshade::log::message(reshade::log::level::warning, std::format("[BL A2C] PS 0x{:08X} not patched: {}", hash, error).c_str());
   }
   game_device_data.a2c_patched_shaders.emplace(hash, shader);
   return shader;
}

// Physically based highlights: the game's pixel shader with its specular pow patched (see "spec_highlights_patch.h"), from the bytecode Core keeps
// for the pipeline. "a2c" applies the alpha to coverage patch first (the same bytecode the coverage shader uses). Cached per (hash, a2c); a
// shader without a specular site, or one that failed, is remembered as such. Null = the draw keeps its shader.
static ComPtr<ID3D11PixelShader> GetSpecularPixelShader(ID3D11Device* native_device, DeviceData& device_data, BrutalLegendDeviceData& game_device_data, reshade::api::pipeline pipeline, uint32_t hash, bool a2c, const SpecularHighlights::Params& spec_params)
{
   const uint64_t key = (uint64_t(hash) << 1) | (a2c ? 1u : 0u);
   const std::lock_guard<std::mutex> lock(game_device_data.spec_mutex);
   if (const auto it = game_device_data.spec_patched_shaders.find(key); it != game_device_data.spec_patched_shaders.end())
      return it->second;
   if (game_device_data.spec_no_site_keys.contains(key))
      return nullptr;

   std::vector<uint8_t> code;
   {
      const std::shared_lock lock_pipelines(s_mutex_generic);
      if (const auto it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle); it != device_data.pipeline_cache_by_pipeline_handle.end() && it->second && it->second->subobjects_cache && it->second->subobject_count > 0)
      {
         const auto* desc = static_cast<const reshade::api::shader_desc*>(it->second->subobjects_cache[0].data);
         const auto* original_code = desc ? static_cast<const uint8_t*>(desc->code) : nullptr;
         if (original_code && desc->code_size)
            code.assign(original_code, original_code + desc->code_size);
      }
   }
   if (code.empty())
      return nullptr; // Not cached: the pipeline may show up later

   if (a2c)
   {
      std::vector<uint8_t> a2c_code;
      std::string a2c_error;
      if (AlphaToCoverage::PatchAlphaTestToCoverage(code.data(), code.size(), &a2c_code, &a2c_error))
         code = std::move(a2c_code);
   }

   std::string error;
   std::vector<uint8_t> patched;
   uint32_t sites = 0;
   ComPtr<ID3D11PixelShader> shader;
   if (SpecularHighlights::PatchSpecularPow(code.data(), code.size(), spec_params, &patched, &sites, &error) && sites > 0)
   {
      if (FAILED(native_device->CreatePixelShader(patched.data(), patched.size(), nullptr, shader.put())))
      {
         shader = nullptr;
         error = "CreatePixelShader failed";
      }
   }

   if (shader)
   {
      game_device_data.spec_patched_shaders.emplace(key, shader);
   }
   else
   {
      game_device_data.spec_no_site_keys.insert(key);
      if (error != "no specular pow site")
      {
         game_device_data.spec_patch_failures++;
         reshade::log::message(reshade::log::level::warning, std::format("[BL Spec] PS 0x{:08X} not patched: {}", hash, error).c_str());
      }
   }
   return shader;
}

// Physically based highlights: draws with the game's pixel shader patched by SpecularHighlights::PatchSpecularPow. Returns false when there is
// nothing to patch, in which case the caller lets the original draw run unchanged.
static bool DrawWithSpecularHighlights(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, BrutalLegendDeviceData& game_device_data, reshade::api::pipeline pixel_pipeline, uint32_t pixel_hash, const SpecularHighlights::Params& spec_params, std::function<void()>& original_draw)
{
   ComPtr<ID3D11PixelShader> spec_shader = GetSpecularPixelShader(native_device, device_data, game_device_data, pixel_pipeline, pixel_hash, false, spec_params);
   if (!spec_shader)
      return false;

   ComPtr<ID3D11PixelShader> game_shader;
   native_device_context->PSGetShader(game_shader.put(), nullptr, nullptr);
   native_device_context->PSSetShader(spec_shader.get(), nullptr, 0);
   {
      const std::lock_guard<std::mutex> lock(game_device_data.spec_mutex);
      game_device_data.spec_patched_draws++;
   }
   original_draw();
   native_device_context->PSSetShader(game_shader.get(), nullptr, 0);
   return true;
}

// Foliage Alpha to Coverage: draws the current (opaque, MSAA) draw with a cached clone of its blend state that has AlphaToCoverageEnable set,
// and with the alpha test shader's coverage version when "patch_shader" and it has one (the game's shader is restored after the draw).
// Returns false when the draw doesn't qualify, in which case the caller lets the original draw run unchanged.
static bool DrawWithAlphaToCoverage(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, BrutalLegendDeviceData& game_device_data, reshade::api::pipeline pixel_pipeline, uint32_t pixel_hash, bool patch_shader, bool pbr_highlights, const SpecularHighlights::Params& spec_params, std::function<void()>& original_draw)
{
   // Only MSAA render targets gain anything
   ComPtr<ID3D11RenderTargetView> rtv;
   native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
   if (!rtv)
      return false;
   ComPtr<ID3D11Resource> rt_resource;
   rtv->GetResource(rt_resource.put());
   if (!rt_resource)
      return false;
   ComPtr<ID3D11Texture2D> rt_tex;
   if (FAILED(rt_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)rt_tex.put())) || !rt_tex)
      return false;
   D3D11_TEXTURE2D_DESC rt_desc = {};
   rt_tex->GetDesc(&rt_desc);
   if (rt_desc.SampleDesc.Count <= 1)
      return false;

   ComPtr<ID3D11BlendState> bs;
   FLOAT blend_factor[4] = {};
   UINT sample_mask = 0;
   native_device_context->OMGetBlendState(bs.put(), blend_factor, &sample_mask);

   D3D11_BLEND_DESC desc = {};
   if (bs)
   {
      bs->GetDesc(&desc);
   }
   else // D3D11 default blend state
   {
      for (int i = 0; i < 8; i++)
      {
         desc.RenderTarget[i].BlendEnable = FALSE;
         desc.RenderTarget[i].SrcBlend = D3D11_BLEND_ONE;
         desc.RenderTarget[i].DestBlend = D3D11_BLEND_ZERO;
         desc.RenderTarget[i].BlendOp = D3D11_BLEND_OP_ADD;
         desc.RenderTarget[i].SrcBlendAlpha = D3D11_BLEND_ONE;
         desc.RenderTarget[i].DestBlendAlpha = D3D11_BLEND_ZERO;
         desc.RenderTarget[i].BlendOpAlpha = D3D11_BLEND_OP_ADD;
         desc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      }
   }
   // Only opaque alpha-tested draws
   if (desc.AlphaToCoverageEnable || desc.RenderTarget[0].BlendEnable)
      return false;

   ComPtr<ID3D11BlendState> clone;
   {
      const std::lock_guard<std::mutex> lock(game_device_data.a2c_mutex);
      auto it = game_device_data.a2c_blend_states.find(bs.get());
      if (it != game_device_data.a2c_blend_states.end())
      {
         clone = it->second.clone;
      }
      else
      {
         desc.AlphaToCoverageEnable = TRUE;
         if (FAILED(native_device->CreateBlendState(&desc, clone.put())) || !clone)
            return false;
         BrutalLegendDeviceData::A2CBlendEntry entry;
         entry.original = bs;
         entry.clone = clone;
         game_device_data.a2c_blend_states.emplace(bs.get(), entry);
      }
      game_device_data.a2c_draws++;
   }

   ComPtr<ID3D11PixelShader> a2c_shader;
   if (patch_shader)
   {
      a2c_shader = GetAlphaToCoveragePixelShader(native_device, device_data, game_device_data, pixel_pipeline, pixel_hash);
   }
   // Physically based highlights go on top of the coverage version (same bytecode), or on the original shader when there is none
   ComPtr<ID3D11PixelShader> patched_shader = a2c_shader;
   if (pbr_highlights)
   {
      ComPtr<ID3D11PixelShader> spec_shader = GetSpecularPixelShader(native_device, device_data, game_device_data, pixel_pipeline, pixel_hash, a2c_shader != nullptr, spec_params);
      if (spec_shader)
      {
         patched_shader = spec_shader;
         const std::lock_guard<std::mutex> lock(game_device_data.spec_mutex);
         game_device_data.spec_patched_draws++;
      }
   }
   ComPtr<ID3D11PixelShader> game_shader;
   if (patched_shader)
   {
      native_device_context->PSGetShader(game_shader.put(), nullptr, nullptr);
      native_device_context->PSSetShader(patched_shader.get(), nullptr, 0);
      if (a2c_shader)
      {
         const std::lock_guard<std::mutex> lock(game_device_data.a2c_mutex);
         game_device_data.a2c_patched_draws++;
      }
   }

   native_device_context->OMSetBlendState(clone.get(), blend_factor, sample_mask);
   original_draw();
   native_device_context->OMSetBlendState(bs.get(), blend_factor, sample_mask);
   if (patched_shader)
   {
      native_device_context->PSSetShader(game_shader.get(), nullptr, 0);
   }
   return true;
}

// True if the game exe has the LARGE_ADDRESS_AWARE flag set (4GB patched). Read from the in-memory PE header, cached once.
static bool IsGameLargeAddressAware()
{
   static const bool large_address_aware = []()
   {
      const auto* base = (const uint8_t*)GetModuleHandleW(nullptr);
      if (!base)
         return false;
      const auto* dos = (const IMAGE_DOS_HEADER*)base;
      if (dos->e_magic != IMAGE_DOS_SIGNATURE)
         return false;
      const auto* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
      if (nt->Signature != IMAGE_NT_SIGNATURE)
         return false;
      return (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
   }();
   return large_address_aware;
}

static std::string LAAWinErrorText(DWORD err)
{
   char* buf = nullptr;
   DWORD n = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, nullptr);
   std::string text = (n && buf) ? std::string(buf, n) : std::string("unknown error");
   if (buf)
      LocalFree(buf);
   while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '.' || text.back() == ' '))
      text.pop_back();
   return text + " (error " + std::to_string(err) + ")";
}

// Reads a whole file. Shares read/delete so it works while the game is running.
static bool LAAReadWholeFile(const std::wstring& path, std::vector<uint8_t>& data, DWORD& err)
{
   HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
   if (h == INVALID_HANDLE_VALUE)
   {
      err = GetLastError();
      return false;
   }
   LARGE_INTEGER size{};
   if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (LONGLONG)(512ull * 1024 * 1024))
   {
      err = GetLastError() ? GetLastError() : ERROR_BAD_LENGTH;
      CloseHandle(h);
      return false;
   }
   data.assign((size_t)size.QuadPart, 0);
   size_t done = 0;
   while (done < data.size())
   {
      DWORD want = (DWORD)std::min<size_t>(data.size() - done, 1u << 20);
      DWORD got = 0;
      if (!ReadFile(h, data.data() + done, want, &got, nullptr) || got == 0)
      {
         err = GetLastError() ? GetLastError() : ERROR_READ_FAULT;
         CloseHandle(h);
         return false;
      }
      done += got;
   }
   CloseHandle(h);
   return true;
}

// Validates a PE32 image in memory and returns its NT headers (nullptr + message on failure).
static IMAGE_NT_HEADERS32* LAAValidatePE32(std::vector<uint8_t>& data, std::string& out_message)
{
   if (data.size() < sizeof(IMAGE_DOS_HEADER))
   {
      out_message = "The game exe is too small to be a valid PE file.";
      return nullptr;
   }
   auto* dos = (IMAGE_DOS_HEADER*)data.data();
   if (dos->e_magic != IMAGE_DOS_SIGNATURE)
   {
      out_message = "The game exe has no MZ header.";
      return nullptr;
   }
   if (dos->e_lfanew < 0 || (size_t)dos->e_lfanew > data.size() || data.size() - (size_t)dos->e_lfanew < sizeof(IMAGE_NT_HEADERS32))
   {
      out_message = "The game exe has an invalid PE header offset.";
      return nullptr;
   }
   auto* nt = (IMAGE_NT_HEADERS32*)(data.data() + dos->e_lfanew);
   if (nt->Signature != IMAGE_NT_SIGNATURE)
   {
      out_message = "The game exe has no PE signature.";
      return nullptr;
   }
   if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
   {
      out_message = "The game exe is not a 32-bit (PE32) image.";
      return nullptr;
   }
   return nt;
}

// Sets LARGE_ADDRESS_AWARE in the game exe on disk. Keeps the original as <exe>.luma_bak (or .luma_old if .luma_bak already exists).
// Every failure returns false and leaves the original exe in place.
static bool ApplyLAAPatchToDisk(std::string& out_message)
{
   out_message.clear();

   std::wstring exe_path(32768, L'\0');
   DWORD len = GetModuleFileNameW(nullptr, exe_path.data(), (DWORD)exe_path.size());
   if (len == 0 || len >= exe_path.size())
   {
      out_message = "Could not determine the game exe path.";
      return false;
   }
   exe_path.resize(len);
   const std::wstring tmp_path = exe_path + L".luma_tmp";
   const std::wstring bak_path = exe_path + L".luma_bak";
   const std::wstring old_path = exe_path + L".luma_old";

   auto access_hint = [](DWORD err) -> std::string
   {
      if (err == ERROR_ACCESS_DENIED)
         return " Access denied: run once as admin or move the game out of Program Files.";
      return "";
   };

   // 1. Read and validate the exe on disk.
   std::vector<uint8_t> data;
   DWORD err = 0;
   if (!LAAReadWholeFile(exe_path, data, err))
   {
      out_message = "Could not read the game exe: " + LAAWinErrorText(err) + "." + access_hint(err);
      return false;
   }
   IMAGE_NT_HEADERS32* nt = LAAValidatePE32(data, out_message);
   if (!nt)
      return false;

   // 2. Already patched on disk?
   if (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE)
   {
      out_message = "Already patched on disk, restart the game.";
      return true;
   }

   // 3. Set the flag, and fix the checksum only if the exe carries one.
   nt->FileHeader.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
   if (nt->OptionalHeader.CheckSum != 0)
   {
      DWORD header_sum = 0, checksum = 0;
      if (!CheckSumMappedFile(data.data(), (DWORD)data.size(), &header_sum, &checksum))
      {
         out_message = "Could not compute the new PE checksum: " + LAAWinErrorText(GetLastError()) + ".";
         return false;
      }
      nt->OptionalHeader.CheckSum = checksum;
   }

   // 4. Write the patched copy next to the exe and flush it.
   DeleteFileW(tmp_path.c_str());
   HANDLE hout = CreateFileW(tmp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
   if (hout == INVALID_HANDLE_VALUE)
   {
      err = GetLastError();
      out_message = "Could not create the temporary file: " + LAAWinErrorText(err) + "." + access_hint(err);
      return false;
   }
   bool write_ok = true;
   size_t written = 0;
   while (write_ok && written < data.size())
   {
      DWORD want = (DWORD)std::min<size_t>(data.size() - written, 1u << 20);
      DWORD put = 0;
      if (!WriteFile(hout, data.data() + written, want, &put, nullptr) || put == 0)
      {
         write_ok = false;
         err = GetLastError();
         break;
      }
      written += put;
   }
   if (write_ok && !FlushFileBuffers(hout))
   {
      write_ok = false;
      err = GetLastError();
   }
   CloseHandle(hout);
   if (!write_ok)
   {
      DeleteFileW(tmp_path.c_str());
      out_message = "Could not write the patched exe: " + LAAWinErrorText(err) + "." + access_hint(err);
      return false;
   }

   // 5. Move the original out of the way (renaming a running exe is allowed).
   const bool bak_exists = GetFileAttributesW(bak_path.c_str()) != INVALID_FILE_ATTRIBUTES;
   const std::wstring& original_backup = bak_exists ? old_path : bak_path;
   const DWORD move_flags = bak_exists ? MOVEFILE_REPLACE_EXISTING : 0;
   if (!MoveFileExW(exe_path.c_str(), original_backup.c_str(), move_flags))
   {
      err = GetLastError();
      DeleteFileW(tmp_path.c_str());
      out_message = "Could not move the original exe aside: " + LAAWinErrorText(err) + "." + access_hint(err);
      return false;
   }

   // 6. Put the patched copy in place. On failure, restore the original.
   if (!MoveFileExW(tmp_path.c_str(), exe_path.c_str(), MOVEFILE_WRITE_THROUGH))
   {
      const DWORD install_err = GetLastError();
      const bool restored = MoveFileExW(original_backup.c_str(), exe_path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
      const DWORD restore_err = restored ? 0 : GetLastError();
      DeleteFileW(tmp_path.c_str());
      if (restored)
      {
         out_message = "Could not install the patched exe (original restored): " + LAAWinErrorText(install_err) + "." + access_hint(install_err);
      }
      else
      {
         out_message = "Could not install the patched exe: " + LAAWinErrorText(install_err) + ". Could not restore the original exe: " + LAAWinErrorText(restore_err)
            + ". The original game exe is still in the game folder under its backup name (.luma_old or .luma_bak).";
      }
      return false;
   }

   // 7. Re-open from disk and verify.
   std::vector<uint8_t> verify;
   if (!LAAReadWholeFile(exe_path, verify, err))
   {
      out_message = "Patched, but could not re-read the exe to verify: " + LAAWinErrorText(err) + ".";
      return false;
   }
   IMAGE_NT_HEADERS32* vnt = LAAValidatePE32(verify, out_message);
   if (!vnt)
      return false;
   if (!(vnt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE))
   {
      out_message = "Verification failed: the Large Address Aware flag is not set on disk.";
      return false;
   }

   out_message = "4GB patch applied to the exe on disk. Restart the game for it to take effect.";
   return true;
}

static LRESULT CALLBACK CursorWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
   if (msg == WM_SETCURSOR && cursor_hide_enabled && LOWORD(lp) == HTCLIENT)
   {
      SetCursor(nullptr);
      return TRUE;
   }
   return CallWindowProcW(cursor_prev_wndproc, h, msg, wp, lp);
}

class BrutalLegend final : public Game // Go view "Source/Core/includes/game.h" for all the overridable virtual functions
{
   // Self-gated: runs again at device creation in case d3d9.dll was not loaded at init.
   static void InstallD3D9Hooks()
   {
      TryPatchDgVoodooGetTextureBounds();
      BLInstallD3D9Cs();
      g_cs_owner_present_cb = &BrutalLegend::BLRunPresentModCalls;
   }

   // Optional helper to hide ugly casts
   static BrutalLegendDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<BrutalLegendDeviceData*>(device_data.game);
   }

   // Luma bloom for the tonemap draw: prefilter the scene (t3, bound now) to half-res linear, then core's DrawBloom.
   // Returns the bloom SRV (owned by the device data), or null when the scene is missing.
   static ComPtr<ID3D11ShaderResourceView> BuildLumaBloom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, float radius)
   {
      auto& gd = GetGameDeviceData(device_data);
      gd.srv_luma_bloom.reset(); // DrawBloom AddRef's its mip 0 into this

      ComPtr<ID3D11ShaderResourceView> srv_scene;
      native_device_context->PSGetShaderResources(3, 1, srv_scene.put());
      ComPtr<ID3D11Resource> res_scene;
      ComPtr<ID3D11Texture2D> tex_scene;
      if (!srv_scene)
         return nullptr;
      srv_scene->GetResource(res_scene.put());
      if (FAILED(res_scene->QueryInterface(tex_scene.put())))
         return nullptr;
      D3D11_TEXTURE2D_DESC scene_desc = {};
      tex_scene->GetDesc(&scene_desc);
      const UINT w = (std::max)(1u, scene_desc.Width / 2);
      const UINT h = (std::max)(1u, scene_desc.Height / 2);

      DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
      bloom_state.Cache(native_device_context, device_data.uav_max_count);

      // Half-res prefilter target, recreated on a size change
      if (!gd.tex_luma_bloom_prefilter || gd.luma_bloom_prefilter_w != w || gd.luma_bloom_prefilter_h != h)
      {
         gd.srv_luma_bloom_prefilter.reset();
         gd.rtv_luma_bloom_prefilter.reset();
         gd.tex_luma_bloom_prefilter.reset();
         D3D11_TEXTURE2D_DESC td = {};
         td.Width = w;
         td.Height = h;
         td.MipLevels = 1;
         td.ArraySize = 1;
         td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
         td.SampleDesc.Count = 1;
         td.Usage = D3D11_USAGE_DEFAULT;
         td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         if (FAILED(native_device->CreateTexture2D(&td, nullptr, gd.tex_luma_bloom_prefilter.put())) ||
            FAILED(native_device->CreateRenderTargetView(gd.tex_luma_bloom_prefilter.get(), nullptr, gd.rtv_luma_bloom_prefilter.put())) ||
            FAILED(native_device->CreateShaderResourceView(gd.tex_luma_bloom_prefilter.get(), nullptr, gd.srv_luma_bloom_prefilter.put())))
         {
            gd.tex_luma_bloom_prefilter.reset();
            bloom_state.Restore(native_device_context);
            return nullptr;
         }
         gd.luma_bloom_prefilter_w = w;
         gd.luma_bloom_prefilter_h = h;
      }

      const auto copy_vs_it = device_data.native_vertex_shaders.find("Copy VS"_h);
      const auto prefilter_ps_it = device_data.native_pixel_shaders.find("BL Luma Bloom Prefilter PS"_h);
      if (copy_vs_it == device_data.native_vertex_shaders.end() || !copy_vs_it->second || prefilter_ps_it == device_data.native_pixel_shaders.end() || !prefilter_ps_it->second)
      {
         bloom_state.Restore(native_device_context);
         return nullptr;
      }

      // Prefilter (linear, soft-knee threshold, half res). DrawCustomPixelShader binds the source at t0.
      DrawCustomPixelShader(native_device_context, nullptr, nullptr, nullptr,
         copy_vs_it->second.get(), prefilter_ps_it->second.get(),
         srv_scene.get(), gd.rtv_luma_bloom_prefilter.get(), w, h, false, "BL Luma Bloom Prefilter");

      // The prefilter and DrawBloom's shaders read LumaSettings (b13): push it at this seam, as the other pixel draws do
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);

      float sigmas[std::size(g_luma_bloom_sigmas)] = {};
      for (size_t i = 0; i < std::size(g_luma_bloom_sigmas); i++)
         sigmas[i] = g_luma_bloom_sigmas[i] * radius;
      DrawBloom(native_device, native_device_context, device_data, gd.srv_luma_bloom_prefilter.get(), (int)std::size(sigmas), sigmas, gd.srv_luma_bloom.put());

      bloom_state.Restore(native_device_context);
      return gd.srv_luma_bloom;
   }

public:
   // "Replace FXAA with SMAA" option (config key "SMAAEnable").
   bool replace_fxaa_with_smaa = true;
   // "Foliage Alpha to Coverage" option (config key "AlphaToCoverage"): alpha-tested foliage draws use MSAA alpha-to-coverage (needs MSAA).
   bool alpha_to_coverage = true;
   bool alpha_to_coverage_patch = true; // dev toggle "a2c.patch": alpha test shaders get the coverage alpha (see "alpha_to_coverage_patch.h")
   // Luma bloom (config key "LumaBloomEnable"): mirrored into GameSettings.LumaBloomEnable; the other four are cbuffer fields.
   bool luma_bloom_enable = false;
   bool fog_highlight_limit = false; // "Fog Highlight Limit" (config key "FogHighlightLimit"): fogged HDR highlights fade toward SDR white with the fog amount
   // Shadow options (config keys = names). Release: fixed at these defaults. The cbuffer copies are ShadowPCSS, ShadowCascadeBlend, ShadowReceiverBias, ShadowPCSSLightSize.
   bool shadow_pcss = true;
   bool shadow_cascade_blend = true;
   bool shadow_receiver_bias = false; // Off: it false-shadows lit wall faces
   bool shadow_msaa_fix = true; // binds the MS shadow atlas at t9 for the shadow resolve (dev toggle "shadow.msaa_fix")
   // "Anisotropic Filtering Upgrade" option (config key "AFUpgrade").
   bool af_upgrade = true;
   // "Trilinear to AF Upgrade" option (config key "TrilinearAFUpgrade").
   bool trilinear_af_upgrade = true;
   // "Vanilla AF on Normal Maps" option (config key "NormalMapVanillaAF"): normal-map slots keep the game's own samplers.
   bool normal_map_vanilla_af = false;
   // Normal Map AF (dev values, not persisted): bit N = PS sampler slot N (0..15) gets the game's original sampler back.
   int af_vanilla_slot_mask = 0; // per-slot override (dev)
   // Normal Map AF (dev value, not persisted): also revert any upgraded sampler, any slot, whose original is clamp (AddressU)
   bool af_vanilla_clamp = true; // CLAMP samplers = env cube maps/ramps: 16x AF there over-brightens shiny edges at grazing angles
   // "Replace SSAO with XeGTAO" option (config key "XeGTAOEnable").
   bool replace_ssao_with_xegtao = true;
   // "Hide Cursor" option (config key "HideCursor"): the game runs borderless, so Windows no longer hides the cursor by itself.
   bool hide_cursor = false;
   // Audio output upgrade (fmod_upgrade.h). C++ only, read in OnInit, requires a game restart.
   int audio_output = 1; // "Audio Output" (config key "AudioOutput"): 0 Vanilla, 1 WASAPI
   int audio_resampler = 3; // "Audio Resampler" (config key "AudioResampler"): 0 Vanilla, 1 Linear, 2 Cubic, 3 Spline
   int audio_speaker_mode = 0; // "Audio Speaker Mode" (config key "AudioSpeakerMode"): 0 Auto, 1 Stereo, 2 5.1, 3 7.1
   bool audio_float_output = true; // "Audio Float Output" (config key "AudioFloatOutput"): mix as 32-bit float
   // Audio values as loaded at launch (OnInit): the UI shows "Restart the game to apply." while the current values differ.
   int audio_output_launch = 1;
   int audio_resampler_launch = 3;
   int audio_speaker_mode_launch = 0;
   bool audio_float_output_launch = true;
   // "Animation Interpolation" option (config key "Sim60Hz"): live. Game simulation runs at 60 Hz so animation/motion is smoother. sim_log = dev toggle "sim.log" (sim_tick.h CSV logging).
   bool sim_60hz = true;
   bool sim_log = false;
   // "Engine Frame Pacing Fix" option (config key "FramePacing"): live. Render-side sim interpolation + precise main loop wait (frame_pacing.h).
   bool frame_pacing = true;
   // "Smooth Character Animation" option (config key "AnimInterp"): live. Blends character bone palettes between sim ticks (anim_interp.h). Needs frame_pacing.
   bool smooth_character_animation = true;
   // "Draw Distance Override" / "Draw Distance" (config keys "DrawDistanceOverride", "DrawDistance"): live (draw_distance.h).
   // Engine Detail also includes DetailCullDistance, ShadowDistance (g_engine[6..7]) and ForceTextureHighMips (force_texture_mips).
   bool draw_distance_override = false;
   float draw_distance = 2.f;
   bool force_texture_mips = false;  // "Force High-Res Texture Mips" (config key "ForceTextureHighMips"): draw_distance.h
   // "Physically Based Highlights" option (config key "PBRHighlights"): live. Dev values "spec.*" (see "spec_highlights_patch.h").
   bool pbr_highlights = true;
   SpecularHighlights::Params spec_params;
   // Private cursor state
   bool last_hide_cursor = true;
   // "MSAA (dgVoodoo)" option: dgVoodoo.conf is the source of truth (read at init). Applies after a game restart.
   BLDgVoodooMSAA::Status dgvoodoo_msaa;
   int dgvoodoo_msaa_launch_mode = BLDgVoodooMSAA::Appdriven;

   // You can define any data to initialize once here
   void OnInit(bool async) override
   {
      // Choose the presentation format before the swapchain
      // is created. Restart the game after changing Windows HDR.
      bool hdr_supported = false, hdr_enabled = false;
      Display::IsHDRSupportedAndEnabled(0, hdr_supported, hdr_enabled);
      if (!hdr_enabled)
      {
         swapchain_upgrade_type = SwapchainUpgradeType::None;
         texture_format_upgrades_type = TextureFormatUpgradesType::None;
         force_disable_display_composition = true;
      }

      // One-shot: fix the dgVoodoo out-of-bounds GetTexture stage read (self-gated, see function comment).
      InstallD3D9Hooks();

      InstallSdlRedirect();

      // Audio settings are read before the FMOD hooks are installed (they are fixed for the session).
      {
         reshade::api::effect_runtime* runtime = nullptr;
         reshade::get_config_value(runtime, NAME, "AudioOutput", audio_output);
         reshade::get_config_value(runtime, NAME, "AudioResampler", audio_resampler);
         reshade::get_config_value(runtime, NAME, "AudioSpeakerMode", audio_speaker_mode);
         reshade::get_config_value(runtime, NAME, "AudioFloatOutput", audio_float_output);
         audio_output_launch = audio_output;
         audio_resampler_launch = audio_resampler;
         audio_speaker_mode_launch = audio_speaker_mode;
         audio_float_output_launch = audio_float_output;
      }
      BLFmodUpgrade::Settings fmod_settings;
      fmod_settings.output_mode = audio_output;
      fmod_settings.resampler = audio_resampler;
      fmod_settings.speaker_mode = audio_speaker_mode;
      fmod_settings.float_output = audio_float_output;
      InstallFmodUpgrade(fmod_settings);
      InstallSimTick();
      InstallFramePacing();
      InstallAnimInterp();

      if (IsGameLargeAddressAware())
         reshade::log::message(reshade::log::level::info, "[BL] 4GB patch: applied");
      else
         reshade::log::message(reshade::log::level::warning, "[BL] 4GB patch: NOT applied, out of memory crashes are likely");

#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::init_sampler>(BrutalLegend::OnInitSamplerLog);
#endif

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", /*default value*/ '1', true, false, /*tooltip*/ "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)", /*max value*/ 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES); // Make sure there's room for at least one extra custom define to add for development (this isn't really relevant outside of development)

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // What space are the colors in? Was the swapchain linear (sRGB texture format)? Did we change post processing to store in linear space?
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0'); // Whether we do gamma correction and paper white scaling during post processing or we delay them until the final display composition pass
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); // What SDR transfer curve was the game using? Most modern games used sRGB in SDR
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // What SDR transfer curve to we want to emulate? This is relevant even if we work in linear space, as there can be a gamma mismatch on it
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0'); // How does the UI draw in?
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // The tonemapper expands into BT.2020, late gamma correction and FXAA can push colors beyond it

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // Needed for debugging textures and having custom per pass data (including handling the final pass properly).
      luma_ui_cbuffer_index = -1; // Optional, see "UI_DRAW_TYPE" (this is for type 1)

      default_luma_global_game_settings.HighlightsStrength = cb_luma_global_settings.GameSettings.HighlightsStrength = 1.f;
      default_luma_global_game_settings.VanillaHueRestore = cb_luma_global_settings.GameSettings.VanillaHueRestore = 1.f;
      default_luma_global_game_settings.BloomIntensity = cb_luma_global_settings.GameSettings.BloomIntensity = 1.f;
      default_luma_global_game_settings.GamutExpansion = cb_luma_global_settings.GameSettings.GamutExpansion = 0.5f;
      default_luma_global_game_settings.XeGTAOIntensity = cb_luma_global_settings.GameSettings.XeGTAOIntensity = 0.75f;
      default_luma_global_game_settings.XeGTAODarkness = cb_luma_global_settings.GameSettings.XeGTAODarkness = 0.5f;
      default_luma_global_game_settings.XeGTAORadius = cb_luma_global_settings.GameSettings.XeGTAORadius = 10.f; // fixed, no user setting
      default_luma_global_game_settings.ShadowSoftness = cb_luma_global_settings.GameSettings.ShadowSoftness = 1.5f;
      default_luma_global_game_settings.ShadowPCSS = cb_luma_global_settings.GameSettings.ShadowPCSS = shadow_pcss ? 1.f : 0.f;
      default_luma_global_game_settings.ShadowPCSSLightSize = cb_luma_global_settings.GameSettings.ShadowPCSSLightSize = 2.f;
      default_luma_global_game_settings.ShadowPCSSPenumbraScale = cb_luma_global_settings.GameSettings.ShadowPCSSPenumbraScale = 2.5f;
      default_luma_global_game_settings.ShadowPCSSMaxPenumbra = cb_luma_global_settings.GameSettings.ShadowPCSSMaxPenumbra = 8.f;
      default_luma_global_game_settings.ShadowPCSSMinPenumbra = cb_luma_global_settings.GameSettings.ShadowPCSSMinPenumbra = 2.f;
      default_luma_global_game_settings.ShadowCascadeBlend = cb_luma_global_settings.GameSettings.ShadowCascadeBlend = shadow_cascade_blend ? 1.f : 0.f;
      default_luma_global_game_settings.ShadowReceiverBias = cb_luma_global_settings.GameSettings.ShadowReceiverBias = shadow_receiver_bias ? 1.f : 0.f;
      cb_luma_global_settings.GameSettings.ShadowAtlasMSBound = 0.f; // Set per shadow resolve draw, never saved
      default_luma_global_game_settings.VideoAutoHDREnable = cb_luma_global_settings.GameSettings.VideoAutoHDREnable = 1.f;
      default_luma_global_game_settings.VideoAutoHDR = cb_luma_global_settings.GameSettings.VideoAutoHDR = 0.5f;
      cb_luma_global_settings.GameSettings.VideoDrawActive = 0.f; // Set per draw by the sprite draw, never saved
      cb_luma_global_settings.GameSettings.XeGTAOActive = 0.f; // Set per frame by the AO upsample draw, never saved
      default_luma_global_game_settings.SkyBrightness = cb_luma_global_settings.GameSettings.SkyBrightness = 0.6f;
      default_luma_global_game_settings.BlackFloor = cb_luma_global_settings.GameSettings.BlackFloor = 0.f;
      default_luma_global_game_settings.Tonemapper = cb_luma_global_settings.GameSettings.Tonemapper = 1.f;
      default_luma_global_game_settings.PsychoVShoulder = cb_luma_global_settings.GameSettings.PsychoVShoulder = 0.f;
      default_luma_global_game_settings.PsychoVGamutFit = cb_luma_global_settings.GameSettings.PsychoVGamutFit = 0.f;
      default_luma_global_game_settings.PsychoVHueLock = cb_luma_global_settings.GameSettings.PsychoVHueLock = 0.5f;
      default_luma_global_game_settings.PsychoVHighlightSaturation = cb_luma_global_settings.GameSettings.PsychoVHighlightSaturation = 1.f;
      default_luma_global_game_settings.PsychoVHueClip = cb_luma_global_settings.GameSettings.PsychoVHueClip = 0.f;
      default_luma_global_game_settings.FilmGrainStrength = cb_luma_global_settings.GameSettings.FilmGrainStrength = 0.f;
      cb_luma_global_settings.GameSettings.SkyDepthActive = 0.f; // Set per tonemap draw, never saved
      cb_luma_global_settings.GameSettings.FogDataActive = 0.f; // Set per tonemap draw, never saved
      default_luma_global_game_settings.FogStart = cb_luma_global_settings.GameSettings.FogStart = 0.f;
      default_luma_global_game_settings.FogDensity = cb_luma_global_settings.GameSettings.FogDensity = 1.f;
      default_luma_global_game_settings.FogColorSaturation = cb_luma_global_settings.GameSettings.FogColorSaturation = 1.f;
      default_luma_global_game_settings.FogColorBrightness = cb_luma_global_settings.GameSettings.FogColorBrightness = 1.f; // 1 = vanilla. Below 1, thick fog (f >= 0.95, skipped by the tonemap) keeps the vanilla color: seams on distant mountains
      default_luma_global_game_settings.FogKeepColor = cb_luma_global_settings.GameSettings.FogKeepColor = 1.f;
      default_luma_global_game_settings.FogHighlightLimit = cb_luma_global_settings.GameSettings.FogHighlightLimit = fog_highlight_limit ? 1.f : 0.f;
      default_luma_global_game_settings.LumaBloomEnable = cb_luma_global_settings.GameSettings.LumaBloomEnable = luma_bloom_enable ? 1.f : 0.f;
      default_luma_global_game_settings.LumaBloomIntensity = cb_luma_global_settings.GameSettings.LumaBloomIntensity = 1.f;
      default_luma_global_game_settings.LumaBloomThreshold = cb_luma_global_settings.GameSettings.LumaBloomThreshold = 1.f;
      default_luma_global_game_settings.LumaBloomKnee = cb_luma_global_settings.GameSettings.LumaBloomKnee = 0.5f;
      default_luma_global_game_settings.LumaBloomRadius = cb_luma_global_settings.GameSettings.LumaBloomRadius = 1.f;

      native_shaders_definitions.emplace(CompileTimeStringHash("BL SMAA Decode CS"), ShaderDefinition{ "Luma_SMAA_Decode", reshade::api::pipeline_subobject_type::compute_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Prefilter CS"), ShaderDefinition{ "Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Main CS"), ShaderDefinition{ "Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Denoise CS"), ShaderDefinition{ "Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("BL XeGTAO Denoise Final CS"), ShaderDefinition{ "Luma_BL_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_final_cs" });
      native_shaders_definitions.emplace(CompileTimeStringHash("BL Luma Bloom Prefilter PS"), ShaderDefinition{ "Luma_BL_BloomPrefilter", reshade::api::pipeline_subobject_type::pixel_shader });

   }

    // This needs to be overridden with your own "GameDeviceData" sub-class (destruction is automatically handled)
    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
       // Defensive retry of the dgVoodoo bounds patch (self-gated; no-op once applied or given up).
       InstallD3D9Hooks();

       device_data.game = new BrutalLegendDeviceData;
#if DEVELOPMENT
      Mcp::RegisterCounter("af.vanilla_swaps", &GetGameDeviceData(device_data).af_vanilla_swaps_last, &device_data);
      Mcp::RegisterInts({{"af.vanilla_slot_mask", &af_vanilla_slot_mask, 0, 65535}});
      Mcp::RegisterToggles({{"af.vanilla_clamp", &af_vanilla_clamp}, {"af.normal_map_vanilla", &normal_map_vanilla_af}});
      Mcp::RegisterCounter("a2c.draws", &GetGameDeviceData(device_data).a2c_draws_last, &device_data);
      Mcp::RegisterToggles({{"a2c.enable", &alpha_to_coverage}, {"a2c.patch", &alpha_to_coverage_patch}});
      Mcp::RegisterCounter("a2c.patched_draws", &GetGameDeviceData(device_data).a2c_patched_draws_last, &device_data);
      Mcp::RegisterCounter("a2c.patch_failures", &GetGameDeviceData(device_data).a2c_patch_failures_last, &device_data);
      Mcp::RegisterToggles({{"spec.enable", &pbr_highlights}});
      Mcp::RegisterValues({{"spec.strength", &spec_params.strength, 0.f, 1.f},
                           {"spec.ref_exponent", &spec_params.ref_exponent, 1.f, 256.f},
                           {"spec.min_gain", &spec_params.min_gain, 0.f, 1.f},
                           {"spec.max_gain", &spec_params.max_gain, 1.f, 32.f},
                           {"spec.gamma", &spec_params.gamma, 1.f, 3.f}});
      Mcp::RegisterCounter("spec.patched_draws", &GetGameDeviceData(device_data).spec_patched_draws_last, &device_data);
      Mcp::RegisterCounter("spec.shaders", &GetGameDeviceData(device_data).spec_shaders_last, &device_data);
      Mcp::RegisterCounter("spec.patch_failures", &GetGameDeviceData(device_data).spec_patch_failures_last, &device_data);
      Mcp::RegisterToggles({{"sim.log", &sim_log}});
      Mcp::RegisterToggles({{"d3d9.filter", &g_d3d9_filter}});
      Mcp::RegisterInts({{"d3d9.cs", &g_cs_dev_mode, 0, 2},
                         {"d3d9.cs_max_frames", &g_cs_dev_max_frames, 1, 3},
                         {"d3d9.cs_staging", &g_cs_dev_staging, 0, 1},
                         {"d3d9.cs_coalesce", &g_cs_dev_coalesce, 0, 1},
                         {"d3d9.cs_writewatch", &g_cs_dev_writewatch, 0, 1},
                         {"d3d9.cs_shadow", &g_cs_dev_shadow, 0, 1},
                         {"d3d9.cs_async_refs", &g_cs_dev_async_refs, 0, 1},
                         {"d3d9.cs_async_sb", &g_cs_dev_async_sb, 0, 1}});
      Mcp::RegisterCounter("sim.ticks_last", &g_sim_ticks_last, &device_data);
      Mcp::RegisterCounter("fp.hold", &BLFramePacing::g_dev_hold, &device_data);
      Mcp::RegisterCounter("fp.under", &BLFramePacing::g_dev_under, &device_data);
      Mcp::RegisterCounter("fp.resync", &BLFramePacing::g_dev_resync, &device_data);
      Mcp::RegisterCounter("fp.passthrough", &BLFramePacing::g_dev_passthrough, &device_data);
      Mcp::RegisterCounter("fp.render_nowait", &BLFramePacing::g_dev_render_nowait, &device_data);
      Mcp::RegisterValues({{"fp.latency_ticks", &BLFramePacing::g_latency_ticks, 0.f, 6.f}});
      Mcp::RegisterCounter("ai.blended", &BLAnimInterp::g_dev_blended, &device_data);
      Mcp::RegisterCounter("ai.pass", &BLAnimInterp::g_dev_pass, &device_data);
      Mcp::RegisterCounter("ai.new_entries", &BLAnimInterp::g_dev_new_entries, &device_data);
      Mcp::RegisterCounter("ai.collision", &BLAnimInterp::g_dev_collision, &device_data);
      Mcp::RegisterCounter("ai.bone_snap", &BLAnimInterp::g_dev_bone_snap, &device_data);
      Mcp::RegisterInts({{"ai.mode", &BLAnimInterp::g_mode, 0, 3}});
      Mcp::RegisterCounter("dd.valid", &BLDrawDistance::g_dev_valid, &device_data);
      Mcp::RegisterValues({{"dd.meshLOD", &BLDrawDistance::g_engine[0].value, 0.f, 16.f},
                           {"dd.detailCulling", &BLDrawDistance::g_engine[1].value, 0.f, 16.f},
                           {"dd.shaderLOD", &BLDrawDistance::g_engine[2].value, 0.f, 16.f},
                           {"dd.shadowRange", &BLDrawDistance::g_engine[3].value, 0.f, 16.f},
                           {"dd.ambmeshDensity", &BLDrawDistance::g_engine[4].value, 0.f, 1.f},
                           {"dd.ambmeshCulling", &BLDrawDistance::g_engine[5].value, 0.f, 16.f},
                           {"dd.detailCullDist", &BLDrawDistance::g_engine[6].value, 0.f, 16.f},
                           {"dd.shadowDist", &BLDrawDistance::g_engine[7].value, 0.f, 16.f}});
      Mcp::RegisterToggles({{"dd.meshLOD.on", &BLDrawDistance::g_engine[0].override_on},
                            {"dd.detailCulling.on", &BLDrawDistance::g_engine[1].override_on},
                            {"dd.shaderLOD.on", &BLDrawDistance::g_engine[2].override_on},
                            {"dd.shadowRange.on", &BLDrawDistance::g_engine[3].override_on},
                            {"dd.ambmeshDensity.on", &BLDrawDistance::g_engine[4].override_on},
                            {"dd.ambmeshCulling.on", &BLDrawDistance::g_engine[5].override_on},
                            {"dd.detailCullDist.on", &BLDrawDistance::g_engine[6].override_on},
                            {"dd.shadowDist.on", &BLDrawDistance::g_engine[7].override_on},
                            {"dd.texMips.on", &force_texture_mips}});
      Mcp::RegisterMirroredToggle("fog.highlight_limit", &fog_highlight_limit, &cb_luma_global_settings.GameSettings.FogHighlightLimit);
      Mcp::RegisterMirroredToggle("shadow.pcss", &shadow_pcss, &cb_luma_global_settings.GameSettings.ShadowPCSS);
      Mcp::RegisterMirroredToggle("shadow.cascade_blend", &shadow_cascade_blend, &cb_luma_global_settings.GameSettings.ShadowCascadeBlend);
      Mcp::RegisterMirroredToggle("shadow.receiver_bias", &shadow_receiver_bias, &cb_luma_global_settings.GameSettings.ShadowReceiverBias);
      Mcp::RegisterToggles({{"shadow.msaa_fix", &shadow_msaa_fix}});
      Mcp::RegisterValues({{"shadow.pcss_light_size", &cb_luma_global_settings.GameSettings.ShadowPCSSLightSize, 0.25f, 4.f}});
      Mcp::RegisterValues({{"shadow.pcss_penumbra_scale", &cb_luma_global_settings.GameSettings.ShadowPCSSPenumbraScale, 0.25f, 4.f}});
      Mcp::RegisterValues({{"shadow.pcss_max_penumbra", &cb_luma_global_settings.GameSettings.ShadowPCSSMaxPenumbra, 2.f, 32.f}});
      Mcp::RegisterValues({{"shadow.pcss_min_penumbra", &cb_luma_global_settings.GameSettings.ShadowPCSSMinPenumbra, 0.5f, 4.f}});
      Mcp::RegisterCounter("shadow.ms_bound", &GetGameDeviceData(device_data).shadow_ms_bound_last, &device_data);
      Mcp::RegisterMirroredToggle("bloom.luma", &luma_bloom_enable, &cb_luma_global_settings.GameSettings.LumaBloomEnable);
      Mcp::RegisterValues({{"bloom.intensity", &cb_luma_global_settings.GameSettings.LumaBloomIntensity, 0.f, 3.f},
                           {"bloom.threshold", &cb_luma_global_settings.GameSettings.LumaBloomThreshold, 0.f, 4.f},
                           {"bloom.knee", &cb_luma_global_settings.GameSettings.LumaBloomKnee, 0.f, 1.f},
                           {"bloom.radius", &cb_luma_global_settings.GameSettings.LumaBloomRadius, 0.25f, 3.f}});
#endif
    }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Normal Map AF: the samplers of the slots in af_vanilla_slot_mask (and, with af_vanilla_clamp, any clamp sampler) get the game's
      // own samplers back instead of the 16x AF upgrades. Core re-upgrades them on the game's next bind, so nothing needs restoring afterwards.
      if (af_upgrade && normal_map_vanilla_af && !ignore_upgraded_samplers) // Note: "stages" only flags stages with custom shaders, so it can't gate this
      {
         ID3D11SamplerState* ps_samplers[16] = {}; // owned references, released below
         native_device_context->PSGetSamplers(0, 16, ps_samplers);
         ID3D11SamplerState* swap_to[16] = {};
         {
            const std::shared_lock lock_samplers(s_mutex_samplers); // reads Core's list, which is only written under the unique lock
            std::lock_guard<std::mutex> lock_map(game_device_data.custom_to_original_mutex);
            // Rebuilds the mod-side map from Core's list (the caller holds both locks)
            auto rebuild = [&]()
            {
               game_device_data.custom_to_original_samplers.clear();
               for (const auto& original_entry : device_data.custom_sampler_by_original_sampler)
               {
                  for (const auto& bias_entry : original_entry.second)
                  {
                     if (bias_entry.second.get())
                     {
                        auto& entry = game_device_data.custom_to_original_samplers[bias_entry.second.get()];
                        entry.original = reinterpret_cast<ID3D11SamplerState*>(original_entry.first);
                        D3D11_SAMPLER_DESC desc = {};
                        if (entry.original)
                           entry.original->GetDesc(&desc);
                        entry.is_clamp = desc.AddressU == D3D11_TEXTURE_ADDRESS_CLAMP;
                     }
                  }
               }
               game_device_data.custom_to_original_source_size = device_data.custom_sampler_by_original_sampler.size();
            };
            if (game_device_data.custom_to_original_source_size != device_data.custom_sampler_by_original_sampler.size())
               rebuild();
            for (int i = 0; i < 16; i++)
            {
               if (!ps_samplers[i])
                  continue;
               const bool in_mask = ((static_cast<uint32_t>(af_vanilla_slot_mask) >> i) & 1) != 0;
               if (!in_mask && !af_vanilla_clamp)
                  continue;
               // Miss: rebuild at most once per frame (a new sampler may have been created since the last rebuild)
               if (game_device_data.custom_to_original_samplers.find(ps_samplers[i]) == game_device_data.custom_to_original_samplers.end() && !game_device_data.custom_to_original_rebuilt_this_frame)
               {
                  game_device_data.custom_to_original_rebuilt_this_frame = true;
                  rebuild();
               }
               const auto it = game_device_data.custom_to_original_samplers.find(ps_samplers[i]);
               if (it != game_device_data.custom_to_original_samplers.end() && it->second.original && (in_mask || it->second.is_clamp))
               {
                  swap_to[i] = it->second.original;
                  game_device_data.af_vanilla_swaps++;
               }
            }
         }
         for (int i = 0; i < 16; i++)
         {
            if (swap_to[i])
               native_device_context->PSSetSamplers(i, 1, &swap_to[i]);
         }
         for (int i = 0; i < 16; i++)
         {
            if (ps_samplers[i])
               ps_samplers[i]->Release();
         }
      }

      // Foliage Alpha to Coverage: alpha-tested (cut-out) opaque draws on an MSAA target. Not gated on "stages" (it only flags custom shaders).
      if (alpha_to_coverage && original_draw_dispatch_func && std::binary_search(std::begin(kAlphaTestPixelShaders), std::end(kAlphaTestPixelShaders), original_shader_hashes.pixel_shaders[0]))
      {
         if (DrawWithAlphaToCoverage(native_device, native_device_context, device_data, game_device_data, cmd_list_data.pipeline_state_original_pixel_shader, original_shader_hashes.pixel_shaders[0], alpha_to_coverage_patch, pbr_highlights && cb_luma_global_settings.DisplayMode != DisplayModeType::SDR, spec_params, *original_draw_dispatch_func))
            return DrawOrDispatchOverrideType::Skip;
      }

      // Physically based highlights: other pixel shaders drawn with the game's own shader (not a Luma replacement; "stages" flags any bound PS)
      if (pbr_highlights && cb_luma_global_settings.DisplayMode != DisplayModeType::SDR && original_draw_dispatch_func && !cmd_list_data.pipeline_state_has_custom_pixel_shader && original_shader_hashes.pixel_shaders[0] != 0)
      {
         if (DrawWithSpecularHighlights(native_device, native_device_context, device_data, game_device_data, cmd_list_data.pipeline_state_original_pixel_shader, original_shader_hashes.pixel_shaders[0], spec_params, *original_draw_dispatch_func))
            return DrawOrDispatchOverrideType::Skip;
      }

      // Fog: copy the game's fog constant buffers (VS and PS slot 4) once per frame, on a draw of the fog vertex shader. Bound to the tonemap draw below.
      static const uint32_t vertex_shader_hash_fog = Shader::Hash_StrToNum("29550E3E");
      if (!game_device_data.fog_cb_copied_this_frame && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(vertex_shader_hash_fog, reshade::api::shader_stage::vertex))
      {
         ComPtr<ID3D11Buffer> fog_vs_src;
         ComPtr<ID3D11Buffer> fog_ps_src;
         native_device_context->VSGetConstantBuffers(4, 1, fog_vs_src.put());
         native_device_context->PSGetConstantBuffers(4, 1, fog_ps_src.put());
         if (fog_vs_src && fog_ps_src)
         {
            CopyConstantBufferForFog(native_device, native_device_context, game_device_data.fog_vs_cb, fog_vs_src.get());
            CopyConstantBufferForFog(native_device, native_device_context, game_device_data.fog_ps_cb, fog_ps_src.get());
            game_device_data.fog_cb_copied_this_frame = true;
         }
      }

      // The game's FXAA draw becomes Luma's SMAA (only drawn while FXAA is on in the game's options). On failure the original draw runs.
      static const uint32_t pixel_shader_hash_fxaa = Shader::Hash_StrToNum("B78D5AEC");
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && original_shader_hashes.Contains(pixel_shader_hash_fxaa, reshade::api::shader_stage::pixel))
      {
         if (replace_fxaa_with_smaa && DrawSMAAForFXAA(native_device, native_device_context, device_data))
            return DrawOrDispatchOverrideType::Replaced;
      }

      // Video AutoHDR: the generic sprite PS draws the FMV frames (a large RENDER_TARGET texture) and UI sprites. Flag the video draws so the shader only expands those.
      static const uint32_t pixel_shader_hash_sprite = Shader::Hash_StrToNum("D5C6F8E3");
      if (original_shader_hashes.Contains(pixel_shader_hash_sprite, reshade::api::shader_stage::pixel))
      {
         bool video_draw = false;
         ComPtr<ID3D11ShaderResourceView> srv_video;
         native_device_context->PSGetShaderResources(0, 1, srv_video.put());
         if (srv_video)
         {
            ComPtr<ID3D11Resource> video_resource;
            ComPtr<ID3D11Texture2D> video_tex;
            srv_video->GetResource(video_resource.put());
            if (video_resource && SUCCEEDED(video_resource->QueryInterface(video_tex.put())))
            {
               D3D11_TEXTURE2D_DESC video_desc = {};
               video_tex->GetDesc(&video_desc);
               video_draw = (video_desc.BindFlags & D3D11_BIND_RENDER_TARGET) && video_desc.Width >= 640 && video_desc.Height >= 360;
            }
         }
         const float video_draw_active = video_draw ? 1.f : 0.f;
         if (cb_luma_global_settings.GameSettings.VideoDrawActive != video_draw_active)
         {
            cb_luma_global_settings.GameSettings.VideoDrawActive = video_draw_active;
            device_data.cb_luma_global_settings_dirty = true;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;
         }
      }

      // XeGTAO: replaces the game's AO generation (907DFA1C), blur (ABF4B0D7) and upsample (51BA9E06). The shadow resolve (203D9FAF) only caches the projection.
      static const uint32_t pixel_shader_hash_shadow_resolve = Shader::Hash_StrToNum("203D9FAF");
      static const uint32_t pixel_shader_hash_ao_upsample = Shader::Hash_StrToNum("51BA9E06");
      static const uint32_t pixel_shader_hash_ao_generation = Shader::Hash_StrToNum("907DFA1C");
      static const uint32_t pixel_shader_hash_ao_blur = Shader::Hash_StrToNum("ABF4B0D7");
      if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
      {
         // Sky depth: the half-res particles draw (runs before the tonemap) reads the scene depth at t2. Capture it for the tonemap's t8 binding.
         static const uint32_t pixel_shader_hash_particles_half = Shader::Hash_StrToNum("772DEB94");
         if (original_shader_hashes.Contains(pixel_shader_hash_particles_half, reshade::api::shader_stage::pixel))
         {
            game_device_data.sky_depth_srv = nullptr;
            ComPtr<ID3D11ShaderResourceView> srv_sky_depth;
            native_device_context->PSGetShaderResources(2, 1, srv_sky_depth.put());
            if (srv_sky_depth)
            {
               ComPtr<ID3D11Resource> sky_depth_resource;
               ComPtr<ID3D11Texture2D> sky_depth_tex;
               srv_sky_depth->GetResource(sky_depth_resource.put());
               if (sky_depth_resource && SUCCEEDED(sky_depth_resource->QueryInterface(sky_depth_tex.put())))
               {
                  D3D11_TEXTURE2D_DESC sky_depth_desc = {};
                  sky_depth_tex->GetDesc(&sky_depth_desc);
                  if (sky_depth_desc.Format == DXGI_FORMAT_R32_FLOAT && sky_depth_desc.SampleDesc.Count == 1)
                     game_device_data.sky_depth_srv = srv_sky_depth;
               }
            }
         }

         // Shadow MSAA fix: remember the MS shadow atlas (1024x4096 R32 float caster target with MSAA) for the shadow resolve draw below.
         if (shadow_msaa_fix)
         {
            ComPtr<ID3D11RenderTargetView> rtv_caster;
            native_device_context->OMGetRenderTargets(1, rtv_caster.put(), nullptr);
            if (rtv_caster)
            {
               ComPtr<ID3D11Resource> rtv_resource;
               ComPtr<ID3D11Texture2D> rtv_tex;
               rtv_caster->GetResource(rtv_resource.put());
               if (rtv_resource && SUCCEEDED(rtv_resource->QueryInterface(rtv_tex.put())))
               {
                  D3D11_TEXTURE2D_DESC rtv_desc = {};
                  rtv_tex->GetDesc(&rtv_desc);
                  if ((rtv_desc.Format == DXGI_FORMAT_R32_FLOAT || rtv_desc.Format == DXGI_FORMAT_R32_TYPELESS) && rtv_desc.Width == 1024 && rtv_desc.Height == 4096 && rtv_desc.SampleDesc.Count > 1)
                     game_device_data.ms_shadow_atlas = rtv_tex;
               }
            }
         }

         if (original_shader_hashes.Contains(pixel_shader_hash_shadow_resolve, reshade::api::shader_stage::pixel))
         {
            if (replace_ssao_with_xegtao)
            {
               ComPtr<ID3D11Buffer> game_cb4_fov;
               native_device_context->PSGetConstantBuffers(4, 1, game_cb4_fov.put());
               BLXeGTAO::CaptureFov(native_device, native_device_context, game_device_data.gtao_resources, game_cb4_fov.get());
            }

            // Shadow MSAA fix: bind the MS atlas at t9 (same size as the atlas at t1) for this draw only. The shader taps it with Load(sample 0).
            ComPtr<ID3D11ShaderResourceView> srv_atlas;
            native_device_context->PSGetShaderResources(1, 1, srv_atlas.put());
            ComPtr<ID3D11Texture2D> ms_atlas_to_bind;
            if (shadow_msaa_fix && original_draw_dispatch_func && game_device_data.ms_shadow_atlas && srv_atlas)
            {
               ComPtr<ID3D11Resource> srv_atlas_resource;
               ComPtr<ID3D11Texture2D> srv_atlas_tex;
               srv_atlas->GetResource(srv_atlas_resource.put());
               if (srv_atlas_resource && SUCCEEDED(srv_atlas_resource->QueryInterface(srv_atlas_tex.put())))
               {
                  D3D11_TEXTURE2D_DESC srv_atlas_desc = {};
                  srv_atlas_tex->GetDesc(&srv_atlas_desc);
                  D3D11_TEXTURE2D_DESC ms_atlas_desc = {};
                  game_device_data.ms_shadow_atlas->GetDesc(&ms_atlas_desc);
                  if (!(ms_atlas_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
                  {
                     if (!game_device_data.ms_shadow_atlas_no_srv_flag_logged)
                     {
                        game_device_data.ms_shadow_atlas_no_srv_flag_logged = true;
                        reshade::log::message(reshade::log::level::warning, "[BL Shadows] MS atlas has no SRV bind flag");
                     }
                  }
                  else if (ms_atlas_desc.Width == srv_atlas_desc.Width && ms_atlas_desc.Height == srv_atlas_desc.Height)
                  {
                     ms_atlas_to_bind = game_device_data.ms_shadow_atlas;
                  }
               }
            }
            if (ms_atlas_to_bind)
            {
               // Create (or reuse) the Texture2DMS SRV for this atlas
               if (game_device_data.ms_shadow_atlas_srv_source.get() != ms_atlas_to_bind.get() || !game_device_data.ms_shadow_atlas_srv)
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC ms_srv_desc = {};
                  ms_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
                  ms_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                  game_device_data.ms_shadow_atlas_srv = nullptr;
                  game_device_data.ms_shadow_atlas_srv_source = nullptr;
                  if (SUCCEEDED(native_device->CreateShaderResourceView(ms_atlas_to_bind.get(), &ms_srv_desc, game_device_data.ms_shadow_atlas_srv.put())))
                     game_device_data.ms_shadow_atlas_srv_source = ms_atlas_to_bind;
                  else
                     game_device_data.ms_shadow_atlas_srv = nullptr;
               }
               if (game_device_data.ms_shadow_atlas_srv)
               {
                  ComPtr<ID3D11ShaderResourceView> saved_t9;
                  native_device_context->PSGetShaderResources(9, 1, saved_t9.put());
                  ID3D11ShaderResourceView* ms_srv_ptr = game_device_data.ms_shadow_atlas_srv.get();
                  native_device_context->PSSetShaderResources(9, 1, &ms_srv_ptr);
                  cb_luma_global_settings.GameSettings.ShadowAtlasMSBound = 1.f;
                  device_data.cb_luma_global_settings_dirty = true;
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
                  updated_cbuffers = true;

                  (*original_draw_dispatch_func)();

                  ID3D11ShaderResourceView* t9_restore = saved_t9.get();
                  native_device_context->PSSetShaderResources(9, 1, &t9_restore);
                  cb_luma_global_settings.GameSettings.ShadowAtlasMSBound = 0.f;
                  device_data.cb_luma_global_settings_dirty = true;
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
                  updated_cbuffers = true;
                  game_device_data.shadow_ms_bound = 1;
                  return DrawOrDispatchOverrideType::Skip;
               }
            }
         }
         else if (original_shader_hashes.Contains(pixel_shader_hash_ao_upsample, reshade::api::shader_stage::pixel))
         {
            bool gtao_drawn = false;
            ID3D11ShaderResourceView* gtao_ao_srv = nullptr;
            // Holds the last successful XeGTAO output alive across the Draw below (a size change inside Draw replaces resources.final_srv)
            ComPtr<ID3D11ShaderResourceView> gtao_prev_srv = game_device_data.gtao_resources.final_srv;
            if (replace_ssao_with_xegtao && original_draw_dispatch_func)
            {
               ComPtr<ID3D11ShaderResourceView> srv_depth;
               native_device_context->PSGetShaderResources(2, 1, srv_depth.put());
               ComPtr<ID3D11Resource> depth_resource;
               ComPtr<ID3D11Texture2D> depth_tex;
               D3D11_TEXTURE2D_DESC depth_desc = {};
               if (srv_depth)
                  srv_depth->GetResource(depth_resource.put());
               if (depth_resource && SUCCEEDED(depth_resource->QueryInterface(depth_tex.put())))
                  depth_tex->GetDesc(&depth_desc);
               ComPtr<ID3D11Buffer> game_cb4;
               native_device_context->PSGetConstantBuffers(4, 1, game_cb4.put());
               if (srv_depth && depth_desc.Format == DXGI_FORMAT_R32_FLOAT && depth_desc.SampleDesc.Count == 1 && game_cb4)
               {
                  BLXeGTAO::Inputs gtao_inputs;
                  gtao_inputs.depth_srv = srv_depth.get();
                  gtao_inputs.game_cb = game_cb4.get();
                  const float fallback_tan_y = 0.630023f;
                  gtao_inputs.tan_half_fov_y = fallback_tan_y;
                  gtao_inputs.tan_half_fov_x = fallback_tan_y * float(depth_desc.Width) / float(depth_desc.Height);
                  gtao_inputs.radius = cb_luma_global_settings.GameSettings.XeGTAORadius;
                  gtao_inputs.intensity = cb_luma_global_settings.GameSettings.XeGTAOIntensity;
                  gtao_inputs.darkness = cb_luma_global_settings.GameSettings.XeGTAODarkness;
                  gtao_inputs.width = depth_desc.Width;
                  gtao_inputs.height = depth_desc.Height;
                  if (BLXeGTAO::Draw(native_device, native_device_context, device_data, game_device_data.gtao_resources, gtao_inputs, &gtao_ao_srv) && gtao_ao_srv)
                     gtao_drawn = true;
               }
            }
            game_device_data.gtao_ok_last_frame = gtao_drawn;

            // The game's AO generation/blur were skipped this frame and XeGTAO failed: the last successful output is the AO this frame has
            const bool gtao_use_prev = !gtao_drawn && game_device_data.gtao_generation_skipped_this_frame && gtao_prev_srv;
            if (gtao_use_prev)
               gtao_ao_srv = gtao_prev_srv.get();
            const bool gtao_bind = gtao_drawn || gtao_use_prev;

            // Re-upload the settings cbuffer with the new XeGTAOActive value for this draw.
            cb_luma_global_settings.GameSettings.XeGTAOActive = gtao_bind ? 1.f : 0.f;
            device_data.cb_luma_global_settings_dirty = true;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;

            if (!gtao_bind)
               return DrawOrDispatchOverrideType::None; // The game's AO upsample runs untouched

            // Bind the XeGTAO visibility at t5 for the game's upsample draw, then restore slot 5.
            ComPtr<ID3D11ShaderResourceView> saved_t5;
            native_device_context->PSGetShaderResources(5, 1, saved_t5.put());
            ID3D11ShaderResourceView* gtao_srv_ptr = gtao_ao_srv;
            native_device_context->PSSetShaderResources(5, 1, &gtao_srv_ptr);
            (*original_draw_dispatch_func)();
            ID3D11ShaderResourceView* t5_restore = saved_t5.get();
            native_device_context->PSSetShaderResources(5, 1, &t5_restore);
            return DrawOrDispatchOverrideType::Skip;
         }
         else if ((original_shader_hashes.Contains(pixel_shader_hash_ao_generation, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(pixel_shader_hash_ao_blur, reshade::api::shader_stage::pixel))
            && replace_ssao_with_xegtao && game_device_data.gtao_ok_last_frame)
         {
            game_device_data.gtao_generation_skipped_this_frame = true;
            return DrawOrDispatchOverrideType::Skip; // XeGTAO replaces the game's AO generation and blur
         }
      }

      // The tonemap pass (A2C20279): HDR tonemap, sky depth, fog and bloom inputs are bound for this draw
      static uint32_t pixel_shader_hash_tonemap = Shader::Hash_StrToNum("A2C20279");

      if (original_shader_hashes.Contains(pixel_shader_hash_tonemap, reshade::api::shader_stage::pixel))
      {
         if (!game_device_data.has_drawn_tonemap)
         {
            game_device_data.has_drawn_tonemap = true;

            device_data.has_drawn_main_post_processing = true;
         }

         // Sky depth (t8) and fog (PS b6 = fog VS cb, PS b7 = fog PS cb): bound for the tonemap draw only, in one draw call.
         // Luma bloom (LumaBloomEnable): built now, before the replayed draw, from the scene bound at t3. Bound at t10 for this draw only.
         ComPtr<ID3D11ShaderResourceView> srv_luma_bloom_draw;
         if (original_draw_dispatch_func && cb_luma_global_settings.DisplayMode != DisplayModeType::SDR && cb_luma_global_settings.GameSettings.LumaBloomEnable > 0.5f)
            srv_luma_bloom_draw = BuildLumaBloom(native_device, native_device_context, cmd_list_data, device_data, cb_luma_global_settings.GameSettings.LumaBloomRadius);
         const bool bind_luma_bloom = srv_luma_bloom_draw != nullptr;

         const bool bind_sky_depth = original_draw_dispatch_func && game_device_data.sky_depth_srv;
         const bool bind_fog = original_draw_dispatch_func && game_device_data.fog_cb_copied_this_frame && game_device_data.fog_vs_cb && game_device_data.fog_ps_cb;
         if (bind_sky_depth || bind_fog || bind_luma_bloom)
         {
            cb_luma_global_settings.GameSettings.SkyDepthActive = bind_sky_depth ? 1.f : 0.f;
            cb_luma_global_settings.GameSettings.FogDataActive = bind_fog ? 1.f : 0.f;
            device_data.cb_luma_global_settings_dirty = true;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;

            ComPtr<ID3D11ShaderResourceView> saved_t10;
            if (bind_luma_bloom)
            {
               native_device_context->PSGetShaderResources(10, 1, saved_t10.put());
               ID3D11ShaderResourceView* luma_bloom_ptr = srv_luma_bloom_draw.get();
               native_device_context->PSSetShaderResources(10, 1, &luma_bloom_ptr);
            }
            ComPtr<ID3D11ShaderResourceView> saved_t8;
            if (bind_sky_depth)
            {
               native_device_context->PSGetShaderResources(8, 1, saved_t8.put());
               ID3D11ShaderResourceView* sky_depth_srv_ptr = game_device_data.sky_depth_srv.get();
               native_device_context->PSSetShaderResources(8, 1, &sky_depth_srv_ptr);
            }
            ComPtr<ID3D11Buffer> saved_b6;
            ComPtr<ID3D11Buffer> saved_b7;
            if (bind_fog)
            {
               native_device_context->PSGetConstantBuffers(6, 1, saved_b6.put());
               native_device_context->PSGetConstantBuffers(7, 1, saved_b7.put());
               ID3D11Buffer* fog_vs_cb_ptr = game_device_data.fog_vs_cb.get();
               ID3D11Buffer* fog_ps_cb_ptr = game_device_data.fog_ps_cb.get();
               native_device_context->PSSetConstantBuffers(6, 1, &fog_vs_cb_ptr);
               native_device_context->PSSetConstantBuffers(7, 1, &fog_ps_cb_ptr);
            }

            (*original_draw_dispatch_func)();

            if (bind_luma_bloom)
            {
               ID3D11ShaderResourceView* t10_restore = saved_t10.get();
               native_device_context->PSSetShaderResources(10, 1, &t10_restore);
            }
            if (bind_sky_depth)
            {
               ID3D11ShaderResourceView* t8_restore = saved_t8.get();
               native_device_context->PSSetShaderResources(8, 1, &t8_restore);
            }
            if (bind_fog)
            {
               ID3D11Buffer* b6_restore = saved_b6.get();
               ID3D11Buffer* b7_restore = saved_b7.get();
               native_device_context->PSSetConstantBuffers(6, 1, &b6_restore);
               native_device_context->PSSetConstantBuffers(7, 1, &b7_restore);
            }

            cb_luma_global_settings.GameSettings.SkyDepthActive = 0.f;
            cb_luma_global_settings.GameSettings.FogDataActive = 0.f;
            device_data.cb_luma_global_settings_dirty = true;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;
            return DrawOrDispatchOverrideType::Skip;
         }
         if (cb_luma_global_settings.GameSettings.SkyDepthActive != 0.f || cb_luma_global_settings.GameSettings.FogDataActive != 0.f)
         {
            cb_luma_global_settings.GameSettings.SkyDepthActive = 0.f;
            cb_luma_global_settings.GameSettings.FogDataActive = 0.f;
            device_data.cb_luma_global_settings_dirty = true;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;
         }
      }

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }
   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      CsShutdown(); // drains and stops the CS worker (owner thread; not from DllMain)
      GetGameDeviceData(device_data).gtao_resources.Release();
      GetGameDeviceData(device_data).sky_depth_srv = nullptr;
      GetGameDeviceData(device_data).ms_shadow_atlas = nullptr;
      GetGameDeviceData(device_data).ms_shadow_atlas_srv = nullptr;
      GetGameDeviceData(device_data).ms_shadow_atlas_srv_source = nullptr;
      GetGameDeviceData(device_data).custom_to_original_samplers.clear();
      {
         const std::lock_guard<std::mutex> lock_a2c(GetGameDeviceData(device_data).a2c_mutex);
         GetGameDeviceData(device_data).a2c_blend_states.clear();
      }
      Game::OnDestroyDeviceData(device_data); // Deletes device_data.game
   }

   void UpdateCursorState()
   {
      cursor_hide_enabled = hide_cursor;

      HWND w = game_window;
      if (w && w != cursor_hook_window)
      {
         if (cursor_hook_window && GetWindowLongPtrW(cursor_hook_window, GWLP_WNDPROC) == (LONG_PTR)&CursorWndProc)
            SetWindowLongPtrW(cursor_hook_window, GWLP_WNDPROC, (LONG_PTR)cursor_prev_wndproc);
         cursor_prev_wndproc = (WNDPROC)SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)&CursorWndProc);
         cursor_hook_window = w;
      }

      // Refresh the cursor immediately when the option was toggled, without moving the mouse
      if (hide_cursor != last_hide_cursor)
      {
         last_hide_cursor = hide_cursor;
         if (cursor_hook_window)
            PostMessageW(cursor_hook_window, WM_SETCURSOR, (WPARAM)cursor_hook_window, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
      }
   }

   // Mod calls that used to run inline in OnPresent. With CS mode != 0 they run from g_cs_owner_present_cb (owner thread, inside Present).
   static void BLRunPresentModCalls()
   {
      BrutalLegend* self = static_cast<BrutalLegend*>(game);
      self->UpdateCursorState();
      SimTick_OnPresent(self->sim_60hz, self->sim_log);
      FramePacing_OnPresent(self->frame_pacing);
      AnimInterp_SetEnabled(self->smooth_character_animation);
      DrawDistance_OnPresent(self->draw_distance_override, self->draw_distance, self->force_texture_mips);
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      BLCsSyncDevValues(); // before the threading check: the dev values must stay reachable in every mode
      // Not in OnCreateDevice: ReShade fires init_device before the proxy immediate context exists (d3d11.cpp:167).
      if (!CsThreadingActive())
         BLRunPresentModCalls();

      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.has_drawn_tonemap = false;
      game_device_data.gtao_generation_skipped_this_frame = false;
      game_device_data.custom_to_original_rebuilt_this_frame = false;
      {
         const std::lock_guard<std::mutex> lock_map(game_device_data.custom_to_original_mutex);
#if DEVELOPMENT
         game_device_data.af_vanilla_swaps_last = game_device_data.af_vanilla_swaps;
#endif
         game_device_data.af_vanilla_swaps = 0;
      }
      {
         const std::lock_guard<std::mutex> lock_a2c(game_device_data.a2c_mutex);
#if DEVELOPMENT
         game_device_data.a2c_draws_last = game_device_data.a2c_draws;
#endif
         game_device_data.a2c_draws = 0;
#if DEVELOPMENT
         game_device_data.a2c_patched_draws_last = game_device_data.a2c_patched_draws;
#endif
         game_device_data.a2c_patched_draws = 0;
      }
      {
         const std::lock_guard<std::mutex> lock_a2c_patch(game_device_data.a2c_patch_mutex);
#if DEVELOPMENT
         game_device_data.a2c_patch_failures_last = game_device_data.a2c_patch_failures;
#endif
         game_device_data.a2c_patch_failures = 0;
      }
      {
         const std::lock_guard<std::mutex> lock_spec(game_device_data.spec_mutex);
         const auto& applied = game_device_data.spec_params_applied;
         if (applied.strength != spec_params.strength || applied.ref_exponent != spec_params.ref_exponent || applied.min_gain != spec_params.min_gain || applied.max_gain != spec_params.max_gain || applied.gamma != spec_params.gamma)
         {
            game_device_data.spec_patched_shaders.clear();
            game_device_data.spec_no_site_keys.clear();
            game_device_data.spec_params_applied = spec_params;
         }
#if DEVELOPMENT
         game_device_data.spec_patched_draws_last = game_device_data.spec_patched_draws;
#endif
         game_device_data.spec_patched_draws = 0;
#if DEVELOPMENT
         game_device_data.spec_shaders_last = uint32_t(game_device_data.spec_patched_shaders.size());
         game_device_data.spec_patch_failures_last = game_device_data.spec_patch_failures;
#endif
         game_device_data.spec_patch_failures = 0;
      }
      game_device_data.fog_cb_copied_this_frame = false;
      game_device_data.sky_depth_srv = nullptr;
      game_device_data.ms_shadow_atlas = nullptr;
      game_device_data.ms_shadow_atlas_srv = nullptr;
      game_device_data.ms_shadow_atlas_srv_source = nullptr;
#if DEVELOPMENT
      game_device_data.shadow_ms_bound_last = game_device_data.shadow_ms_bound;
#endif
      game_device_data.shadow_ms_bound = 0;
      device_data.has_drawn_main_post_processing = true;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      // ReShade.ini is ready here, so load its values.
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "HighlightsStrength", cb_luma_global_settings.GameSettings.HighlightsStrength);
      reshade::get_config_value(runtime, NAME, "VanillaHueRestore", cb_luma_global_settings.GameSettings.VanillaHueRestore);
      reshade::get_config_value(runtime, NAME, "SkyBrightness", cb_luma_global_settings.GameSettings.SkyBrightness);
      reshade::get_config_value(runtime, NAME, "BlackFloor", cb_luma_global_settings.GameSettings.BlackFloor);
#endif
#if DEVELOPMENT // Release always uses PsychoV35 (the default)
      reshade::get_config_value(runtime, NAME, "Tonemapper", cb_luma_global_settings.GameSettings.Tonemapper);
#endif
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "PsychoVShoulder", cb_luma_global_settings.GameSettings.PsychoVShoulder);
#endif
      reshade::get_config_value(runtime, NAME, "PsychoVGamutFit", cb_luma_global_settings.GameSettings.PsychoVGamutFit);
      reshade::get_config_value(runtime, NAME, "PsychoVHueLock", cb_luma_global_settings.GameSettings.PsychoVHueLock);
      reshade::get_config_value(runtime, NAME, "PsychoVHighlightSaturation", cb_luma_global_settings.GameSettings.PsychoVHighlightSaturation);
      reshade::get_config_value(runtime, NAME, "PsychoVHueClip", cb_luma_global_settings.GameSettings.PsychoVHueClip);
      reshade::get_config_value(runtime, NAME, "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength);
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      reshade::get_config_value(runtime, NAME, "LumaBloomEnable", luma_bloom_enable);
      cb_luma_global_settings.GameSettings.LumaBloomEnable = luma_bloom_enable ? 1.f : 0.f;
      reshade::get_config_value(runtime, NAME, "LumaBloomIntensity", cb_luma_global_settings.GameSettings.LumaBloomIntensity);
      reshade::get_config_value(runtime, NAME, "LumaBloomThreshold", cb_luma_global_settings.GameSettings.LumaBloomThreshold);
      reshade::get_config_value(runtime, NAME, "LumaBloomKnee", cb_luma_global_settings.GameSettings.LumaBloomKnee);
      reshade::get_config_value(runtime, NAME, "LumaBloomRadius", cb_luma_global_settings.GameSettings.LumaBloomRadius);
#endif
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "GamutExpansion", cb_luma_global_settings.GameSettings.GamutExpansion);
#endif
      reshade::get_config_value(runtime, NAME, "SMAAEnable", replace_fxaa_with_smaa);
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "AlphaToCoverage", alpha_to_coverage); // Release: always on (it only applies while MSAA is on)
      reshade::get_config_value(runtime, NAME, "AFUpgrade", af_upgrade);
      reshade::get_config_value(runtime, NAME, "TrilinearAFUpgrade", trilinear_af_upgrade);
      reshade::get_config_value(runtime, NAME, "NormalMapVanillaAF", normal_map_vanilla_af);
#endif
      ignore_upgraded_samplers = !af_upgrade;
      force_upgrade_linear_samplers = trilinear_af_upgrade;
      reshade::get_config_value(runtime, NAME, "HideCursor", hide_cursor);
      reshade::get_config_value(runtime, NAME, "Sim60Hz", sim_60hz);
      reshade::get_config_value(runtime, NAME, "FramePacing", frame_pacing);
      reshade::get_config_value(runtime, NAME, "CsMultithreaded", g_cs_user_threaded);
      reshade::get_config_value(runtime, NAME, "AnimInterp", smooth_character_animation);
      reshade::get_config_value(runtime, NAME, "DrawDistanceOverride", draw_distance_override);
      reshade::get_config_value(runtime, NAME, "DrawDistance", draw_distance);
      for (BLDrawDistance::EngineDetail& e : BLDrawDistance::g_engine)
      {
         reshade::get_config_value(runtime, NAME, e.override_key, e.override_on);
         reshade::get_config_value(runtime, NAME, e.key, e.value);
         e.value = BLDrawDistance::ClampEngine(e, e.value);
      }
      reshade::get_config_value(runtime, NAME, "ForceTextureHighMips", force_texture_mips);
      reshade::get_config_value(runtime, NAME, "PBRHighlights", pbr_highlights);
      reshade::get_config_value(runtime, NAME, "XeGTAOEnable", replace_ssao_with_xegtao);
      dgvoodoo_msaa = BLDgVoodooMSAA::ReadStatus();
      dgvoodoo_msaa_launch_mode = dgvoodoo_msaa.mode;
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "XeGTAOIntensity", cb_luma_global_settings.GameSettings.XeGTAOIntensity);
      reshade::get_config_value(runtime, NAME, "XeGTAODarkness", cb_luma_global_settings.GameSettings.XeGTAODarkness);
#endif
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "ShadowSoftness", cb_luma_global_settings.GameSettings.ShadowSoftness);
#endif
      reshade::get_config_value(runtime, NAME, "ShadowPCSS", shadow_pcss);
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "ShadowCascadeBlend", shadow_cascade_blend);
      reshade::get_config_value(runtime, NAME, "ShadowReceiverBias", shadow_receiver_bias);
      reshade::get_config_value(runtime, NAME, "ShadowMSAAFix", shadow_msaa_fix);
      reshade::get_config_value(runtime, NAME, "ShadowPCSSLightSize", cb_luma_global_settings.GameSettings.ShadowPCSSLightSize);
      reshade::get_config_value(runtime, NAME, "ShadowPCSSPenumbraScale", cb_luma_global_settings.GameSettings.ShadowPCSSPenumbraScale);
      reshade::get_config_value(runtime, NAME, "ShadowPCSSMaxPenumbra", cb_luma_global_settings.GameSettings.ShadowPCSSMaxPenumbra);
      reshade::get_config_value(runtime, NAME, "ShadowPCSSMinPenumbra", cb_luma_global_settings.GameSettings.ShadowPCSSMinPenumbra);
#endif
      cb_luma_global_settings.GameSettings.ShadowPCSS = shadow_pcss ? 1.f : 0.f;
      cb_luma_global_settings.GameSettings.ShadowCascadeBlend = shadow_cascade_blend ? 1.f : 0.f;
      cb_luma_global_settings.GameSettings.ShadowReceiverBias = shadow_receiver_bias ? 1.f : 0.f;
      reshade::get_config_value(runtime, NAME, "VideoAutoHDREnable", cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      reshade::get_config_value(runtime, NAME, "VideoAutoHDR", cb_luma_global_settings.GameSettings.VideoAutoHDR);
#if DEVELOPMENT
      reshade::get_config_value(runtime, NAME, "FogStart", cb_luma_global_settings.GameSettings.FogStart);
      reshade::get_config_value(runtime, NAME, "FogDensity", cb_luma_global_settings.GameSettings.FogDensity);
      reshade::get_config_value(runtime, NAME, "FogColorSaturation", cb_luma_global_settings.GameSettings.FogColorSaturation);
      reshade::get_config_value(runtime, NAME, "FogColorBrightness", cb_luma_global_settings.GameSettings.FogColorBrightness);
      reshade::get_config_value(runtime, NAME, "FogKeepColor", cb_luma_global_settings.GameSettings.FogKeepColor);
      reshade::get_config_value(runtime, NAME, "FogHighlightLimit", fog_highlight_limit);
#endif
      cb_luma_global_settings.GameSettings.FogHighlightLimit = fog_highlight_limit ? 1.f : 0.f;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);

      // 4GB (Large Address Aware) patch status.
      if (IsGameLargeAddressAware())
      {
         ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.f), "4GB patch applied");
      }
      else
      {
         ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "Warning: the game is not 4GB patched");
         ImGui::TextWrapped("BrutalLegend.exe is not Large Address Aware, so the game can only use 2 GB of memory. Out of memory crashes are likely, especially with HDR, MSAA and the mod's features enabled.\nClick the button below to patch it (one click, keeps a backup), then restart the game. Steam/GOG \"Verify files\" undoes the patch; click it again if this warning comes back.");

         static std::string laa_patch_message;
         static bool laa_patch_ok = false;
         if (ImGui::Button("Apply 4GB patch (restart required)"))
         {
            laa_patch_ok = ApplyLAAPatchToDisk(laa_patch_message);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Sets the Large Address Aware flag in BrutalLegend.exe on disk (one header bit, nothing else changes).\nThe original exe is kept as BrutalLegend.exe.luma_bak. Restart the game to apply.\nIf it says access denied, run the game once as administrator or move it out of Program Files.");
         if (!laa_patch_message.empty())
         {
            if (laa_patch_ok)
               ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.f), "%s", laa_patch_message.c_str());
            else
               ImGui::TextColored(ImVec4(1.f, 0.6f, 0.1f, 1.f), "%s", laa_patch_message.c_str());
         }
      }

      // Draw whatever ImGui to let users modify settings.
      ImGui::SeparatorText("Image");
#if DEVELOPMENT
      if (ImGui::SliderFloat("Highlights Strength", &cb_luma_global_settings.GameSettings.HighlightsStrength, 0.f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "HighlightsStrength", cb_luma_global_settings.GameSettings.HighlightsStrength);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Intensity of the overbright range the game clips in SDR. 0 is vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.HighlightsStrength, default_luma_global_game_settings.HighlightsStrength, "HighlightsStrength", runtime);

      if (ImGui::SliderFloat("Sky Brightness", &cb_luma_global_settings.GameSettings.SkyBrightness, 0.f, 3.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "SkyBrightness", cb_luma_global_settings.GameSettings.SkyBrightness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("How far the sky can go above SDR white, in paper white units. 0 = vanilla clip.");
      DrawResetButton(cb_luma_global_settings.GameSettings.SkyBrightness, default_luma_global_game_settings.SkyBrightness, "SkyBrightness", runtime);

      if (ImGui::SliderFloat("Black Floor", &cb_luma_global_settings.GameSettings.BlackFloor, 0.f, 1.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "BlackFloor", cb_luma_global_settings.GameSettings.BlackFloor);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Lowers the black level the game's distance fog lifts. 0 is vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.BlackFloor, default_luma_global_game_settings.BlackFloor, "BlackFloor", runtime);

      {
         static const char* tonemapper_items[] = { "DICE", "PsychoV35" };
         int tonemapper_index = cb_luma_global_settings.GameSettings.Tonemapper >= 0.5f ? 1 : 0;
         if (ImGui::Combo("Tonemapper", &tonemapper_index, tonemapper_items, IM_ARRAYSIZE(tonemapper_items)))
         {
            cb_luma_global_settings.GameSettings.Tonemapper = (float)tonemapper_index;
            reshade::set_config_value(runtime, NAME, "Tonemapper", cb_luma_global_settings.GameSettings.Tonemapper);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("HDR display mapping. Cone-response tonemapper.");
         DrawResetButton(cb_luma_global_settings.GameSettings.Tonemapper, default_luma_global_game_settings.Tonemapper, "Tonemapper", runtime);
      }
#endif

      if (cb_luma_global_settings.GameSettings.Tonemapper >= 0.5f)
      {
#if DEVELOPMENT
         if (ImGui::SliderFloat("PsychoV Shoulder", &cb_luma_global_settings.GameSettings.PsychoVShoulder, 0.f, 3.f, "%.2f"))
            reshade::set_config_value(runtime, NAME, "PsychoVShoulder", cb_luma_global_settings.GameSettings.PsychoVShoulder);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("0 = auto (recommended). Higher keeps mid-tones brighter but compresses highlights harder.");
         DrawResetButton(cb_luma_global_settings.GameSettings.PsychoVShoulder, default_luma_global_game_settings.PsychoVShoulder, "PsychoVShoulder", runtime);
#endif

         {
            static const char* gamut_fit_items[] = { "Exact", "Smooth" };
            int gamut_fit_index = cb_luma_global_settings.GameSettings.PsychoVGamutFit >= 0.5f ? 1 : 0;
            if (ImGui::Combo("Gamut Fit", &gamut_fit_index, gamut_fit_items, IM_ARRAYSIZE(gamut_fit_items)))
            {
               cb_luma_global_settings.GameSettings.PsychoVGamutFit = (float)gamut_fit_index;
               reshade::set_config_value(runtime, NAME, "PsychoVGamutFit", cb_luma_global_settings.GameSettings.PsychoVGamutFit);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               ImGui::SetTooltip("How saturated colors are fitted into the display gamut near peak. PsychoV tonemapper only. Applies live.");
            DrawResetButton(cb_luma_global_settings.GameSettings.PsychoVGamutFit, default_luma_global_game_settings.PsychoVGamutFit, "PsychoVGamutFit", runtime);
         }

         DrawSettingSlider("Hue Lock", "PsychoVHueLock", cb_luma_global_settings.GameSettings.PsychoVHueLock, default_luma_global_game_settings.PsychoVHueLock, 0.f, 1.f,
            "Restores the hue PsychoV shifts in bright saturated colors (MacLeod-Boynton, hue only). PsychoV tonemapper only. Applies live.");
         DrawSettingSlider("Highlight Saturation", "PsychoVHighlightSaturation", cb_luma_global_settings.GameSettings.PsychoVHighlightSaturation, default_luma_global_game_settings.PsychoVHighlightSaturation, 0.f, 1.f,
            "Restores the color purity PsychoV removes from very bright highlights (keeps bright reds red instead of pink). 0 = PsychoV default. PsychoV tonemapper only. Applies live.");
         DrawSettingSlider("Hue Clip", "PsychoVHueClip", cb_luma_global_settings.GameSettings.PsychoVHueClip, default_luma_global_game_settings.PsychoVHueClip, 0.f, 1.f,
            "Emulates the hue shift SDR clipping gives very bright colours (fire turns yellow, bright blue turns cyan), like RenoDX's clip hue shift. 0 = off (PsychoV keeps the scene's hue).");
         DrawSettingSlider("Film Grain Strength", "FilmGrainStrength", cb_luma_global_settings.GameSettings.FilmGrainStrength, default_luma_global_game_settings.FilmGrainStrength, 0.f, 1.f,
            "Adds film grain (HDR tonemap only). 0 = off. Applies live.");
      }

#if DEVELOPMENT
      ImGui::Text("Fog");
      if (ImGui::SliderFloat("Fog Start", &cb_luma_global_settings.GameSettings.FogStart, 0.f, 300.f, "%.0f"))
         reshade::set_config_value(runtime, NAME, "FogStart", cb_luma_global_settings.GameSettings.FogStart);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Pushes the game's fog away from the camera (world units). 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.FogStart, default_luma_global_game_settings.FogStart, "FogStart", runtime);

      if (ImGui::SliderFloat("Fog Density", &cb_luma_global_settings.GameSettings.FogDensity, 0.f, 1.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "FogDensity", cb_luma_global_settings.GameSettings.FogDensity);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Multiplier on the fog density. Fog is only ever reduced.");
      DrawResetButton(cb_luma_global_settings.GameSettings.FogDensity, default_luma_global_game_settings.FogDensity, "FogDensity", runtime);

      if (ImGui::SliderFloat("Fog Color Saturation", &cb_luma_global_settings.GameSettings.FogColorSaturation, 0.f, 2.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "FogColorSaturation", cb_luma_global_settings.GameSettings.FogColorSaturation);
      DrawResetButton(cb_luma_global_settings.GameSettings.FogColorSaturation, default_luma_global_game_settings.FogColorSaturation, "FogColorSaturation", runtime);

      if (ImGui::SliderFloat("Fog Color Brightness", &cb_luma_global_settings.GameSettings.FogColorBrightness, 0.f, 1.5f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "FogColorBrightness", cb_luma_global_settings.GameSettings.FogColorBrightness);
      DrawResetButton(cb_luma_global_settings.GameSettings.FogColorBrightness, default_luma_global_game_settings.FogColorBrightness, "FogColorBrightness", runtime);

      if (ImGui::SliderFloat("Fog Keeps Surface Color", &cb_luma_global_settings.GameSettings.FogKeepColor, 0.f, 1.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "FogKeepColor", cb_luma_global_settings.GameSettings.FogKeepColor);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Fogged surfaces keep part of their own saturation.");
      DrawResetButton(cb_luma_global_settings.GameSettings.FogKeepColor, default_luma_global_game_settings.FogKeepColor, "FogKeepColor", runtime);

      if (ImGui::Checkbox("Fog Highlight Limit", &fog_highlight_limit))
      {
         cb_luma_global_settings.GameSettings.FogHighlightLimit = fog_highlight_limit ? 1.f : 0.f;
         reshade::set_config_value(runtime, NAME, "FogHighlightLimit", fog_highlight_limit);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Stops HDR highlights glowing through fog: the part above SDR white fades with the fog amount (thick fog matches vanilla's clip).");

      if (ImGui::SliderFloat("Vanilla Hue Restore", &cb_luma_global_settings.GameSettings.VanillaHueRestore, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "VanillaHueRestore", cb_luma_global_settings.GameSettings.VanillaHueRestore);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Restores the hue and saturation of the SDR image on highlights.");
      DrawResetButton(cb_luma_global_settings.GameSettings.VanillaHueRestore, default_luma_global_game_settings.VanillaHueRestore, "VanillaHueRestore", runtime);
#endif

#if DEVELOPMENT
      DrawSettingSlider("Bloom Intensity", "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity, default_luma_global_game_settings.BloomIntensity, 0.f, 2.f,
         "Strength of the game's own bloom glow. Applies live.");

      // Luma bloom: replaces the game's bloom in the tonemap when on. Labels carry "##luma" so they do not clash with the game bloom slider above.
      if (ImGui::Checkbox("Luma Bloom", &luma_bloom_enable))
      {
         cb_luma_global_settings.GameSettings.LumaBloomEnable = luma_bloom_enable ? 1.f : 0.f;
         reshade::set_config_value(runtime, NAME, "LumaBloomEnable", luma_bloom_enable);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's bloom with Luma's HDR bloom (wider, and it can exceed SDR white).");

      ImGui::BeginDisabled(!luma_bloom_enable);
      if (ImGui::SliderFloat("Bloom Intensity##luma", &cb_luma_global_settings.GameSettings.LumaBloomIntensity, 0.f, 3.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "LumaBloomIntensity", cb_luma_global_settings.GameSettings.LumaBloomIntensity);
      DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloomIntensity, default_luma_global_game_settings.LumaBloomIntensity, "LumaBloomIntensity", runtime);

      if (ImGui::SliderFloat("Bloom Threshold##luma", &cb_luma_global_settings.GameSettings.LumaBloomThreshold, 0.f, 4.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "LumaBloomThreshold", cb_luma_global_settings.GameSettings.LumaBloomThreshold);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Linear scene brightness where bloom starts. 1.0 = SDR white. 0 = no threshold.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloomThreshold, default_luma_global_game_settings.LumaBloomThreshold, "LumaBloomThreshold", runtime);

      if (ImGui::SliderFloat("Bloom Knee##luma", &cb_luma_global_settings.GameSettings.LumaBloomKnee, 0.f, 1.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "LumaBloomKnee", cb_luma_global_settings.GameSettings.LumaBloomKnee);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Soft knee width as a fraction of the threshold.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloomKnee, default_luma_global_game_settings.LumaBloomKnee, "LumaBloomKnee", runtime);

      if (ImGui::SliderFloat("Bloom Radius##luma", &cb_luma_global_settings.GameSettings.LumaBloomRadius, 0.25f, 3.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "LumaBloomRadius", cb_luma_global_settings.GameSettings.LumaBloomRadius);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Scales the blur radius of the bloom.");
      DrawResetButton(cb_luma_global_settings.GameSettings.LumaBloomRadius, default_luma_global_game_settings.LumaBloomRadius, "LumaBloomRadius", runtime);
      ImGui::EndDisabled();

      if (ImGui::SliderFloat("Gamut Expansion", &cb_luma_global_settings.GameSettings.GamutExpansion, 0.f, 1.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "GamutExpansion", cb_luma_global_settings.GameSettings.GamutExpansion);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Expands already saturated colors beyond BT.709 into wide gamut, keeping hue and brightness. Neutrals and skin tones are protected. 0 = vanilla.");
      DrawResetButton(cb_luma_global_settings.GameSettings.GamutExpansion, default_luma_global_game_settings.GamutExpansion, "GamutExpansion", runtime);
#endif

      ImGui::SeparatorText("Anti-Aliasing");
      DrawSettingCheckbox("SMAA", "SMAAEnable", replace_fxaa_with_smaa, true,
         "Uses Luma's SMAA instead of the game's FXAA. Only applies while FXAA is enabled in the game's options. Applies live.");

      {
         // Forced MSAA through dgVoodoo: rewrites dgVoodoo.conf, applies after a game restart.
         ImGui::BeginDisabled(!dgvoodoo_msaa.available);
         int msaa_index = dgvoodoo_msaa.mode;
         if (ImGui::Combo("MSAA (restart)", &msaa_index, BLDgVoodooMSAA::kLabels, BLDgVoodooMSAA::Count))
         {
            std::string msaa_error;
            if (BLDgVoodooMSAA::WriteMode(msaa_index, msaa_error))
               dgvoodoo_msaa.mode = msaa_index;
            else
               reshade::log::message(reshade::log::level::error, ("[BL] dgVoodoo MSAA: " + msaa_error).c_str());
         }
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", dgvoodoo_msaa.available ? "Forces MSAA through dgVoodoo (writes dgVoodoo.conf). Requires a game restart. May not work with every effect." : dgvoodoo_msaa.reason.c_str());
         if (dgvoodoo_msaa.mode != dgvoodoo_msaa_launch_mode)
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "Restart the game to apply.");
      }

#if DEVELOPMENT
      if (ImGui::Checkbox("Foliage Alpha to Coverage", &alpha_to_coverage))
         reshade::set_config_value(runtime, NAME, "AlphaToCoverage", alpha_to_coverage);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Antialiases cut-out foliage, grass and fences using MSAA. Needs MSAA enabled above.");

      ImGui::SeparatorText("Texture Filtering");
      if (ImGui::Checkbox("Anisotropic Filtering Upgrade", &af_upgrade))
      {
         ignore_upgraded_samplers = !af_upgrade;
         reshade::set_config_value(runtime, NAME, "AFUpgrade", af_upgrade);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Forces 16x anisotropic filtering on the game's textures. Applies immediately.");

      ImGui::BeginDisabled(!af_upgrade);
      if (ImGui::Checkbox("Vanilla AF on Reflections", &normal_map_vanilla_af))
      {
         reshade::set_config_value(runtime, NAME, "NormalMapVanillaAF", normal_map_vanilla_af);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Keeps the game's own filtering on reflection (environment) maps. 16x AF there makes metal and other shiny edges over-bright and aliased at grazing angles. Surface textures keep 16x.");
      ImGui::EndDisabled();

      ImGui::BeginDisabled(!af_upgrade);
      if (ImGui::Checkbox("Trilinear to AF Upgrade", &trilinear_af_upgrade))
      {
         force_upgrade_linear_samplers = trilinear_af_upgrade;
         reshade::set_config_value(runtime, NAME, "TrilinearAFUpgrade", trilinear_af_upgrade);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Also upgrades trilinear-filtered samplers (terrain, detail layers) to 16x AF. Off = only the game's own anisotropic samplers are upgraded. Takes effect after a level load or game restart.");
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Lighting & Shadows");
      DrawSettingCheckbox("Physically Based Highlights", "PBRHighlights", pbr_highlights, true,
         "Energy-normalized specular: shiny surfaces get brighter, tighter highlights, rough ones softer. Applies live.");
      DrawSettingCheckbox("XeGTAO", "XeGTAOEnable", replace_ssao_with_xegtao, true,
         "Uses XeGTAO instead of the game's ambient occlusion. Only applies while ambient occlusion is enabled in the game's options. Applies live.");

#if DEVELOPMENT
      if (ImGui::SliderFloat("XeGTAO Intensity", &cb_luma_global_settings.GameSettings.XeGTAOIntensity, 0.5f, 3.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "XeGTAOIntensity", cb_luma_global_settings.GameSettings.XeGTAOIntensity);
      DrawResetButton(cb_luma_global_settings.GameSettings.XeGTAOIntensity, default_luma_global_game_settings.XeGTAOIntensity, "XeGTAOIntensity", runtime);

      if (ImGui::SliderFloat("XeGTAO Darkness", &cb_luma_global_settings.GameSettings.XeGTAODarkness, 0.f, 2.f, "%.2f"))
         reshade::set_config_value(runtime, NAME, "XeGTAODarkness", cb_luma_global_settings.GameSettings.XeGTAODarkness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("How much ambient occlusion darkens. 0.5 is the default, 0 disables it, 2 is twice as dark.");
      DrawResetButton(cb_luma_global_settings.GameSettings.XeGTAODarkness, default_luma_global_game_settings.XeGTAODarkness, "XeGTAODarkness", runtime);
#endif

      {
         if (ImGui::Checkbox("Contact-Hardening Shadows (PCSS)", &shadow_pcss))
         {
            cb_luma_global_settings.GameSettings.ShadowPCSS = shadow_pcss ? 1.f : 0.f;
            reshade::set_config_value(runtime, NAME, "ShadowPCSS", shadow_pcss);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Shadows sharpen where they meet their caster and soften with distance (PCSS). Applies live.");
      }

#if DEVELOPMENT
      if (ImGui::SliderFloat("Shadow Softness", &cb_luma_global_settings.GameSettings.ShadowSoftness, 0.5f, 2.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowSoftness", cb_luma_global_settings.GameSettings.ShadowSoftness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Width of the shadow edge filter. 1 is the vanilla width.");
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowSoftness, default_luma_global_game_settings.ShadowSoftness, "ShadowSoftness", runtime);

      if (ImGui::Checkbox("Shadow Cascade Blending", &shadow_cascade_blend))
      {
         cb_luma_global_settings.GameSettings.ShadowCascadeBlend = shadow_cascade_blend ? 1.f : 0.f;
         reshade::set_config_value(runtime, NAME, "ShadowCascadeBlend", shadow_cascade_blend);
      }
      if (ImGui::Checkbox("Shadow Receiver Bias", &shadow_receiver_bias))
      {
         cb_luma_global_settings.GameSettings.ShadowReceiverBias = shadow_receiver_bias ? 1.f : 0.f;
         reshade::set_config_value(runtime, NAME, "ShadowReceiverBias", shadow_receiver_bias);
      }
      if (ImGui::Checkbox("Shadow MSAA Fix", &shadow_msaa_fix))
         reshade::set_config_value(runtime, NAME, "ShadowMSAAFix", shadow_msaa_fix);
      if (ImGui::SliderFloat("PCSS Light Size", &cb_luma_global_settings.GameSettings.ShadowPCSSLightSize, 0.25f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowPCSSLightSize", cb_luma_global_settings.GameSettings.ShadowPCSSLightSize);
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowPCSSLightSize, default_luma_global_game_settings.ShadowPCSSLightSize, "ShadowPCSSLightSize", runtime);
      if (ImGui::SliderFloat("PCSS Penumbra Scale", &cb_luma_global_settings.GameSettings.ShadowPCSSPenumbraScale, 0.25f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowPCSSPenumbraScale", cb_luma_global_settings.GameSettings.ShadowPCSSPenumbraScale);
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowPCSSPenumbraScale, default_luma_global_game_settings.ShadowPCSSPenumbraScale, "ShadowPCSSPenumbraScale", runtime);
      if (ImGui::SliderFloat("PCSS Max Softness", &cb_luma_global_settings.GameSettings.ShadowPCSSMaxPenumbra, 2.f, 32.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowPCSSMaxPenumbra", cb_luma_global_settings.GameSettings.ShadowPCSSMaxPenumbra);
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowPCSSMaxPenumbra, default_luma_global_game_settings.ShadowPCSSMaxPenumbra, "ShadowPCSSMaxPenumbra", runtime);
      if (ImGui::SliderFloat("PCSS Contact Hardness", &cb_luma_global_settings.GameSettings.ShadowPCSSMinPenumbra, 0.5f, 4.f, "%.3f"))
         reshade::set_config_value(runtime, NAME, "ShadowPCSSMinPenumbra", cb_luma_global_settings.GameSettings.ShadowPCSSMinPenumbra);
      DrawResetButton(cb_luma_global_settings.GameSettings.ShadowPCSSMinPenumbra, default_luma_global_game_settings.ShadowPCSSMinPenumbra, "ShadowPCSSMinPenumbra", runtime);
#endif

      ImGui::SeparatorText("Performance & Timing");
      DrawSettingCheckbox("Animation Interpolation", "Sim60Hz", sim_60hz, true,
         "Runs the game simulation at 60 Hz (live) so animation and motion are smoother. Off = the game's own rate. Applies live.");
      DrawSettingCheckbox("Engine Frame Pacing Fix", "FramePacing", frame_pacing, true,
         "Smooth sim interpolation, any fps/refresh. Applies live. The animation blend fix stays active when this is off.");
      DrawSettingCheckbox("Smooth Character Animation", "AnimInterp", smooth_character_animation, true,
         "Interpolates character skeletons between simulation ticks. Needs Engine Frame Pacing Fix.");
      DrawSettingCheckbox("Multithreaded D3D9 (experimental)", "CsMultithreaded", g_cs_user_threaded, false,
         "Runs dgVoodoo, ReShade and Luma on a separate thread so the game's render thread runs in parallel. Restart not required.");

      if (ImGui::CollapsingHeader("Engine Detail (advanced)"))
      {
         DrawSettingCheckbox("Draw Distance Override", "DrawDistanceOverride", draw_distance_override, false,
            "Applies the Draw Distance value below, live. Off = the game's own value, which returns the next time you change the in-game option.");

         ImGui::BeginDisabled(!draw_distance_override);
         DrawSettingSlider("Draw Distance", "DrawDistance", draw_distance, 2.f, 0.5f, 8.f,
            "Game menu range is 0.5-2.0. Higher draws farther at a CPU cost. Applies live.");
         ImGui::EndDisabled();

         static const char* const kEngineLabels[8] = { "LOD Distance", "Detail Culling", "Shader LOD", "Shadow Range", "Ambient Mesh Density", "Ambient Mesh Culling", "Detail Culling Distance", "Shadow Distance" };
         static const char* const kEngineTips[8] = {
            "Mesh LOD switch distance. Higher keeps detailed models farther out. Applies live.",
            "Small detail object culling distance. Applies live.",
            "Shader LOD distance. Applies live.",
            "Shadow draw range scale. Applies live.",
            "Grass/ambient mesh density. Values above 1 crash the game, so it can only be reduced. Applies live.",
            "Grass/ambient mesh culling distance. Applies live.",
            "Script-level detail culling distance; game caps it at 2. Applies live.",
            "Shadow scale/distance; game caps it at 2. May need a moment to update. Applies live."
         };
         for (int i = 0; i < 8; i++)
         {
            BLDrawDistance::EngineDetail& e = BLDrawDistance::g_engine[i];
            ImGui::PushID(i);
            DrawSettingCheckbox(kEngineLabels[i], e.override_key, e.override_on, false,
               "Overrides the value below. Off = the game's own value. Applies live.");
            ImGui::NewLine();
            ImGui::BeginDisabled(!e.override_on);
            DrawSettingSlider("##value", e.key, e.value, e.def, e.lo, e.hi, kEngineTips[i]);
            ImGui::EndDisabled();
            ImGui::PopID();
         }
         DrawSettingCheckbox("Force High-Res Texture Mips", "ForceTextureHighMips", force_texture_mips, false,
            "Engine's ForceTextureHighMips switch: always use the sharpest texture mips (more VRAM). Experimental. Applies live.");
         if (ImGui::Button("Reset Engine Detail"))
         {
            for (BLDrawDistance::EngineDetail& e : BLDrawDistance::g_engine)
            {
               e.override_on = false;
               reshade::set_config_value(runtime, NAME, e.override_key, false);
            }
            force_texture_mips = false;
            reshade::set_config_value(runtime, NAME, "ForceTextureHighMips", false);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Turns all engine detail overrides and the texture mip switch off. The game defaults return on the next frame. Applies live.");
      }

      ImGui::SeparatorText("Video");
      // Stored as float in the cbuffer, saved to ReShade.ini as a bool-like value
      bool video_auto_hdr = cb_luma_global_settings.GameSettings.VideoAutoHDREnable > 0.f;
      if (ImGui::Checkbox("Video AutoHDR", &video_auto_hdr))
      {
         cb_luma_global_settings.GameSettings.VideoAutoHDREnable = video_auto_hdr ? 1.f : 0.f;
         reshade::set_config_value(runtime, NAME, "VideoAutoHDREnable", cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Expands the highlights of the FMV videos into HDR. Only the videos are affected; UI and the rest of the game are untouched. Applies live.");
      DrawResetButton(cb_luma_global_settings.GameSettings.VideoAutoHDREnable, default_luma_global_game_settings.VideoAutoHDREnable, "VideoAutoHDREnable", runtime);

      ImGui::BeginDisabled(cb_luma_global_settings.GameSettings.VideoAutoHDREnable <= 0.f);
      DrawSettingSlider("Video AutoHDR Strength", "VideoAutoHDR", cb_luma_global_settings.GameSettings.VideoAutoHDR, default_luma_global_game_settings.VideoAutoHDR, 0.f, 1.f,
         "How far the video highlights reach toward the display peak brightness. 0 = paper white (no expansion). Applies live.");
      ImGui::EndDisabled();

      ImGui::SeparatorText("Audio");
      {
         static const char* const kAudioOutputItems[] = { "Vanilla", "WASAPI" };
         static const char* const kAudioResamplerItems[] = { "Vanilla", "Linear", "Cubic", "Spline" };
         static const char* const kAudioSpeakerItems[] = { "Auto", "Stereo", "5.1", "7.1" };
         DrawSettingCombo("Output (restart)", "AudioOutput", audio_output, 1, kAudioOutputItems, IM_ARRAYSIZE(kAudioOutputItems),
            "WASAPI audio output instead of the game's own. Vanilla keeps the game's output. Requires a game restart.");
         DrawSettingCombo("Resampler (restart)", "AudioResampler", audio_resampler, 3, kAudioResamplerItems, IM_ARRAYSIZE(kAudioResamplerItems),
            "Resampler used when mixing audio. Spline is the highest quality. Requires a game restart.");
         DrawSettingCombo("Speaker Mode (restart)", "AudioSpeakerMode", audio_speaker_mode, 0, kAudioSpeakerItems, IM_ARRAYSIZE(kAudioSpeakerItems),
            "Speaker layout sent to the audio device. Auto keeps the game's choice. Requires a game restart.");
         DrawSettingCheckbox("Float Output (restart)", "AudioFloatOutput", audio_float_output, true,
            "Mix output as 32-bit float instead of 16-bit PCM. Requires a game restart.");
         if (audio_output != audio_output_launch || audio_resampler != audio_resampler_launch || audio_speaker_mode != audio_speaker_mode_launch || audio_float_output != audio_float_output_launch)
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f), "Restart the game to apply.");
      }

      ImGui::SeparatorText("Misc");
      DrawSettingCheckbox("Hide Cursor", "HideCursor", hide_cursor, false,
         "Hides the Windows mouse cursor over the game window (the game runs borderless, so Windows no longer hides it). Applies live.");

      ImGui::PopItemWidth();
   }

#if DEVELOPMENT
   static void OnInitSamplerLog(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler)
   {
      static std::mutex sampler_log_mutex;
      static std::set<std::tuple<uint32_t, float, float, float, float, uint32_t, uint32_t, uint32_t, uint32_t>> seen_samplers;
      const auto key = std::make_tuple(
         static_cast<uint32_t>(desc.filter), desc.max_anisotropy, desc.mip_lod_bias, desc.min_lod, desc.max_lod,
         static_cast<uint32_t>(desc.address_u), static_cast<uint32_t>(desc.address_v), static_cast<uint32_t>(desc.address_w),
         static_cast<uint32_t>(desc.compare_op));
      std::lock_guard<std::mutex> lock(sampler_log_mutex);
      if (seen_samplers.insert(key).second)
      {
         reshade::log::message(reshade::log::level::info, std::format("[BL] sampler #{}: filter=0x{:X} aniso={} bias={} lod=[{},{}] addr={}/{}/{} cmp={}",
            seen_samplers.size(), std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key), std::get<4>(key),
            std::get<5>(key), std::get<6>(key), std::get<7>(key), std::get<8>(key)).c_str());
      }
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Brutal Legend\" is developed by Ostad Gaming and is open source and free.\n"
         "It adds true HDR (PsychoV tonemapper with gamut expansion and vanilla hue restoration, windowed/borderless required), SMAA anti-aliasing, dgVoodoo MSAA with foliage alpha-to-coverage, XeGTAO ambient occlusion, contact-hardening (PCSS) shadows, anisotropic filtering upgrade, physically based highlights, film grain, video AutoHDR, animation interpolation and a frame pacing fix, draw distance and engine detail controls, a modern SDL2 controller fix with rumble, and a WASAPI/high-quality audio upgrade, on top of the game's upgraded fp16 rendering pipeline.\n"
         "Requires dgVoodoo2 (D3D9 to D3D11) and the 4GB (Large Address Aware) patch; if the exe is not patched, the Settings tab offers a one-click \"Apply 4GB patch\" button (keeps a backup, restart required).\n"
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
                  "\ndgVoodoo2 (Dege)"
                  "\nRenoDX (PsychoV35 tonemapper by Carlos Lopez Jr., MIT; film grain, MIT)"
                  "\nSMAA (Iryoku)"
                  "\nIntel XeGTAO (ambient occlusion)"
                  "\nSDL2 (controller support)");
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

       uint32_t mod_version = 1; // Increase this to reset the game settings and shader binaries after making large changes to your mod
       Globals::SetGlobals(cleared_project_name, "Brutal Legend Luma mod", "https://ko-fi.com/ostadgaming", mod_version);

       // scRGB fp16 swapchain (the game's backbuffer is 8-bit).
       swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
       swapchain_upgrade_type = SwapchainUpgradeType::scRGB;

       // Exclusive fullscreen is switched to borderless. "force_borderless" also covers leaving fullscreen,
       // so the window does not come back with a title bar after alt-tab.
       prevent_fullscreen_state = true;
       force_borderless = true;

       // Under dgVoodoo, changing a resource's creation format breaks the wrapper's bookkeeping (black screen, even in menus).
       // Only the two clip-capable formats are upgraded, indirectly: creation-time mirrors substituted at bind, keyed by format.
       // The broad template format list is not used here.
       texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
       enable_indirect_texture_format_upgrades = true; // creation-time mirrors, substituted at bind
       enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
       texture_upgrade_formats = {
          reshade::api::format::r8g8b8a8_typeless,     // dgVoodoo's D3D9 backbuffer surface (the LDR canvas)
          reshade::api::format::r16g16b16a16_typeless, // fp16 post/bloom targets the game VIEWS as unorm
       };
       // "No1Px" is required under dgVoodoo: the wrapper binds 1x1 placeholders in every unused sampler slot
       // and a 1x1 trivially passes the aspect filter, so core would mirror those too (and assert in DEVELOPMENT).
       texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomSize;
       texture_format_upgrades_2d_custom_sizes = { {512, 256}, {256, 128}, // the 2:1 bloom chain levels (0x29AA0B9B/0xC39CB7A1/0x3143AF6A), otherwise left 8-bit UNORM
          {256, 16}, {128, 8} }; // per-frame lighting ramps (0xC4399336 → 0xE9A53CDA; ambient by N.y)

       // Update samples to override the bias, based on the rendering resolution etc
       // Forces every game ANISOTROPIC sampler (in-game AF toggle) to 16x. Bias offset stays 0 (no TAA here).
       enable_samplers_upgrade = true;
       force_upgrade_linear_samplers = true; // Also trilinear (MIN_MAG_MIP_LINEAR) samplers: terrain/detail layers never get the game's AF

#if !DEVELOPMENT // Put shaders that a previous version of the mod used but has ever since been deleted here, so that users updating the mod from an older version won't accidentally load them
      old_shader_file_names.emplace("Bloom_0xDC9373A8.ps_5_0.hlsl");
#endif

      // Create your game sub-class instance (it will be automatically destroyed on exit).
      // You do not need to do this if you have no custom data to store.
      game = new BrutalLegend();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::init_sampler>(BrutalLegend::OnInitSamplerLog);
#endif
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
